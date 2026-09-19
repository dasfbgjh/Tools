#ifndef CERT_GENERATOR_H
#define CERT_GENERATOR_H

#include <string>
#include <vector>

class CertGenerator {
public:
    struct CertParams {
        std::string commonName;
        std::string organization;
        std::string organizationalUnit;
        std::string country;
        std::string state;
        std::string locality;
        int days = 365;
        int keyBits = 2048;
        std::vector<std::string> altNamesIp;
        std::vector<std::string> altNamesDns;
    };

    struct CertResult {
        bool success = false;
        std::string error;
        std::string certPath;
        std::string keyPath;
    };

    static std::string getOpensslVersion();

    static bool isOpensslAvailable();

    static CertResult generate( const CertParams &params,
                                const std::string &outputDir,
                                const std::string &baseName,
                                bool overwrite = false );

private:
    static bool writePemKey( const std::string &path, void *pkey );
    static bool writePemCert( const std::string &path, void *x509 );
};

#endif // CERT_GENERATOR_H