#include "CertGenerator.h"
#include "common/Logger.hpp"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/ossl_typ.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

namespace {

struct EvpPkeyDeleter {
    void operator()( EVP_PKEY *p ) const {
        EVP_PKEY_free( p );
    }
};
struct X509Deleter {
    void operator()( X509 *p ) const {
        X509_free( p );
    }
};
struct BioDeleter {
    void operator()( BIO *p ) const {
        BIO_free( p );
    }
};
struct ConfDeleter {
    void operator()( CONF *p ) const {
        NCONF_free( p );
    }
};

using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using BioPtr = std::unique_ptr<BIO, BioDeleter>;
using ConfPtr = std::unique_ptr<CONF, ConfDeleter>;

std::string getOsslError() {
    std::ostringstream ss;
    unsigned long err = ERR_get_error();
    if ( err == 0 )
        return "unknown OpenSSL error";
    char buf[256];
    ERR_error_string_n( err, buf, sizeof( buf ) );
    ss << buf;
    while ( ( err = ERR_get_error() ) != 0 ) {
        ERR_error_string_n( err, buf, sizeof( buf ) );
        ss << "; " << buf;
    }
    return ss.str();
}

bool addExt( X509 *cert, int nid, const std::string &value ) {
    X509_EXTENSION *ext = X509V3_EXT_nconf_nid( nullptr, nullptr, nid, const_cast<char *>( value.c_str() ) );
    if ( !ext )
        return false;
    int rc = X509_add_ext( cert, ext, -1 );
    X509_EXTENSION_free( ext );
    return rc == 1;
}

} // namespace

std::string CertGenerator::getOpensslVersion() {
    return OPENSSL_VERSION_TEXT;
}

bool CertGenerator::isOpensslAvailable() {
    return OpenSSL_version( OPENSSL_VERSION ) != nullptr;
}

CertGenerator::CertResult CertGenerator::generate( const CertParams &params,
                                                   const std::string &outputDir,
                                                   const std::string &baseName,
                                                   bool overwrite ) {
    CertResult result;

    if ( params.commonName.empty() ) {
        result.error = "缺少 commonName(CN)";
        return result;
    }
    if ( outputDir.empty() ) {
        result.error = "缺少输出目录(outputDir)";
        return result;
    }
    if ( baseName.empty() ) {
        result.error = "baseName 不能为空";
        return result;
    }
    if ( baseName.find( '/' ) != std::string::npos ||
         baseName.find( '\\' ) != std::string::npos ||
         baseName.find( ".." ) != std::string::npos ) {
        result.error = "baseName 非法";
        return result;
    }

    std::error_code ec;
    if ( !fs::exists( outputDir, ec ) ) {
        std::error_code ec2;
        fs::create_directories( outputDir, ec2 );
        if ( ec2 ) {
            result.error = "无法创建输出目录: " + ec2.message();
            return result;
        }
    } else if ( !fs::is_directory( outputDir, ec ) ) {
        result.error = "输出路径不是目录";
        return result;
    }

    fs::path outDir( outputDir );
    fs::path keyPath = outDir / ( baseName + ".key" );
    fs::path certPath = outDir / ( baseName + ".crt" );

    if ( !overwrite ) {
        if ( fs::exists( keyPath, ec ) || fs::exists( certPath, ec ) ) {
            result.error = "目标已存在，开启「覆盖」后可重新生成";
            return result;
        }
    } else {
        std::error_code rmec;
        fs::remove( keyPath, rmec );
        fs::remove( certPath, rmec );
    }

    ERR_clear_error();

    // 1) 生成 RSA 密钥对
    EvpPkeyPtr pkey( nullptr );
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id( EVP_PKEY_RSA, nullptr );
        if ( !ctx ) {
            result.error = "EVP_PKEY_CTX_new_id 失败: " + getOsslError();
            return result;
        }

        if ( EVP_PKEY_keygen_init( ctx ) <= 0 ) {
            result.error = "EVP_PKEY_keygen_init 失败: " + getOsslError();
            EVP_PKEY_CTX_free( ctx );
            return result;
        }

        if ( EVP_PKEY_CTX_set_rsa_keygen_bits( ctx, params.keyBits > 0 ? params.keyBits : 2048 ) <= 0 ) {
            result.error = "设置密钥位数失败: " + getOsslError();
            EVP_PKEY_CTX_free( ctx );
            return result;
        }

        EVP_PKEY *rawPkey = nullptr;
        if ( EVP_PKEY_keygen( ctx, &rawPkey ) <= 0 ) {
            result.error = "RSA 密钥生成失败: " + getOsslError();
            EVP_PKEY_CTX_free( ctx );
            return result;
        }
        EVP_PKEY_CTX_free( ctx );

        pkey.reset( rawPkey );
    }

    // 2) 创建 X509 证书结构
    X509Ptr x509( X509_new() );
    if ( !x509 ) {
        result.error = "X509_new 失败: " + getOsslError();
        return result;
    }

    if ( X509_set_version( x509.get(), 2 ) != 1 ) {
        result.error = "X509_set_version 失败: " + getOsslError();
        return result;
    }

    // 设置序列号（随机 20 字节）
    {
        BIGNUM *bnSer = BN_new();
        if ( !bnSer ) {
            result.error = "BN_new (serial) 失败: " + getOsslError();
            return result;
        }
        if ( BN_rand( bnSer, 160, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY ) != 1 ) {
            BN_free( bnSer );
            result.error = "BN_rand (serial) 失败: " + getOsslError();
            return result;
        }
        ASN1_INTEGER *serial = ASN1_INTEGER_new();
        if ( !serial ) {
            BN_free( bnSer );
            result.error = "ASN1_INTEGER_new 失败: " + getOsslError();
            return result;
        }
        if ( BN_to_ASN1_INTEGER( bnSer, serial ) == nullptr ) {
            ASN1_INTEGER_free( serial );
            BN_free( bnSer );
            result.error = "BN_to_ASN1_INTEGER 失败: " + getOsslError();
            return result;
        }
        X509_set_serialNumber( x509.get(), serial );
        ASN1_INTEGER_free( serial );
        BN_free( bnSer );
    }

    // 设置有效期
    {
        int days = params.days > 0 ? params.days : 365;
        if ( X509_gmtime_adj( X509_getm_notBefore( x509.get() ), 0 ) == nullptr ||
             X509_gmtime_adj( X509_getm_notAfter( x509.get() ), static_cast<long>( days ) * 86400 ) == nullptr ) {
            result.error = "设置有效期失败: " + getOsslError();
            return result;
        }
    }

    // 设置公钥
    if ( X509_set_pubkey( x509.get(), pkey.get() ) != 1 ) {
        result.error = "X509_set_pubkey 失败: " + getOsslError();
        return result;
    }

    // 3) 设置 Subject / Issuer（自签名两者相同）
    {
        X509_NAME *name = X509_get_subject_name( x509.get() );
        auto addEntry = [&]( int nid, const std::string &val ) {
            if ( !val.empty() ) {
                X509_NAME_add_entry_by_NID( name, nid, MBSTRING_UTF8,
                                            reinterpret_cast<const unsigned char *>( val.c_str() ),
                                            static_cast<int>( val.size() ), -1, 0 );
            }
        };
        addEntry( NID_countryName, params.country );
        addEntry( NID_stateOrProvinceName, params.state );
        addEntry( NID_localityName, params.locality );
        addEntry( NID_organizationName, params.organization );
        addEntry( NID_organizationalUnitName, params.organizationalUnit );
        addEntry( NID_commonName, params.commonName );

        if ( X509_set_issuer_name( x509.get(), name ) != 1 ) {
            result.error = "X509_set_issuer_name 失败: " + getOsslError();
            return result;
        }
    }

    // 4) 添加 X.509 扩展
    {
        if ( !addExt( x509.get(), NID_basic_constraints, "critical, CA:FALSE" ) ) {
            result.error = "添加 basicConstraints 扩展失败: " + getOsslError();
            return result;
        }
        if ( !addExt( x509.get(), NID_key_usage, "critical, digitalSignature, keyEncipherment" ) ) {
            result.error = "添加 keyUsage 扩展失败: " + getOsslError();
            return result;
        }
        if ( !addExt( x509.get(), NID_ext_key_usage, "serverAuth, clientAuth" ) ) {
            result.error = "添加 extendedKeyUsage 扩展失败: " + getOsslError();
            return result;
        }

        // Subject Alternative Name
        if ( !params.altNamesIp.empty() || !params.altNamesDns.empty() ) {
            std::ostringstream san;
            bool first = true;
            for ( const auto &ip : params.altNamesIp ) {
                if ( ip.empty() )
                    continue;
                if ( !first )
                    san << ",";
                san << "IP:" << ip;
                first = false;
            }
            for ( const auto &dns : params.altNamesDns ) {
                if ( dns.empty() )
                    continue;
                if ( !first )
                    san << ",";
                san << "DNS:" << dns;
                first = false;
            }
            std::string sanStr = san.str();
            if ( !sanStr.empty() ) {
                if ( !addExt( x509.get(), NID_subject_alt_name, sanStr ) ) {
                    result.error = "添加 subjectAltName 扩展失败: " + getOsslError();
                    return result;
                }
            }
        }
    }

    // 5) 自签名
    {
        EVP_MD_CTX *mdCtx = EVP_MD_CTX_new();
        if ( !mdCtx ) {
            result.error = "EVP_MD_CTX_new 失败: " + getOsslError();
            return result;
        }
        const EVP_MD *md = EVP_sha256();
        if ( X509_sign( x509.get(), pkey.get(), md ) <= 0 ) {
            EVP_MD_CTX_free( mdCtx );
            result.error = "证书签名失败: " + getOsslError();
            return result;
        }
        EVP_MD_CTX_free( mdCtx );
    }

    // 6) 写入 PEM 文件
    if ( !writePemKey( keyPath.string(), pkey.get() ) ) {
        result.error = "写入私钥文件失败: " + getOsslError();
        return result;
    }
    if ( !writePemCert( certPath.string(), x509.get() ) ) {
        std::error_code rmec;
        fs::remove( keyPath, rmec );
        result.error = "写入证书文件失败: " + getOsslError();
        return result;
    }

    result.success = true;
    result.certPath = certPath.string();
    result.keyPath = keyPath.string();

    LOG_INFO << "自签名证书生成成功(API) dir=" << outputDir << " cn=" << params.commonName;
    return result;
}

bool CertGenerator::writePemKey( const std::string &path, void *pkey ) {
    BioPtr bio( BIO_new_file( path.c_str(), "wb" ) );
    if ( !bio )
        return false;
    return PEM_write_bio_PrivateKey( bio.get(), static_cast<EVP_PKEY *>( pkey ), nullptr, nullptr, 0, nullptr, nullptr ) == 1;
}

bool CertGenerator::writePemCert( const std::string &path, void *x509 ) {
    BioPtr bio( BIO_new_file( path.c_str(), "wb" ) );
    if ( !bio )
        return false;
    return PEM_write_bio_X509( bio.get(), static_cast<X509 *>( x509 ) ) == 1;
}