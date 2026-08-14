#ifndef SHA256_H
#define SHA256_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// SHA-256 implementation (FIPS 180-4)
class Sha256 {
private:
    uint32_t h_[8];
    uint64_t total_;
    size_t bufLen_;
    uint8_t buf_[64];

public:
    Sha256();

    void update( const uint8_t *data, size_t len );
    void update( const std::string &s );

    static std::vector<uint8_t> hash( const uint8_t *data, size_t len );
    static std::vector<uint8_t> hash( const std::string &s );

    // HMAC-SHA256
    static std::vector<uint8_t> hmacSha256( const uint8_t *key, size_t keyLen,
                                            const uint8_t *msg, size_t msgLen );

    static std::vector<uint8_t> hmacSha256( const std::string &key, const std::string &msg );

    // PBKDF2-HMAC-SHA256
    static std::vector<uint8_t> pbkdf2Sha256( const std::string &password,
                                              const uint8_t *salt, size_t saltLen,
                                              int iterations, size_t dkLen );

private:
    static uint32_t rotr( uint32_t x, uint32_t n );
    void reset();
    std::vector<uint8_t> finalize();
    void processBlock( const uint8_t *block );
};

#endif
// SHA256_H
