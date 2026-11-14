#pragma once

// Rolo's stand-in for OpenSSL's digest header (ADR 0024). Rolo builds Axmol
// without OpenSSL. Axmol's core mentions these names in one place only
// (utils::computeDigest and the MD5 helpers built on it), which Rolo never
// calls. This is Rolo's own code; it contains no OpenSSL code.
//
// No digest is known here: EVP_get_digestbyname() finds nothing, which
// Axmol already handles (computeDigest returns an empty string).

#include <cstddef>

#define EVP_MAX_MD_SIZE 64

struct EVP_MD;
struct EVP_MD_CTX;

inline void OpenSSL_add_all_digests() {}

inline const EVP_MD* EVP_get_digestbyname(const char* /*name*/)
{
    return nullptr;
}

inline EVP_MD_CTX* EVP_MD_CTX_create()
{
    return nullptr;
}

inline void EVP_MD_CTX_destroy(EVP_MD_CTX* /*context*/) {}

inline int EVP_DigestInit(EVP_MD_CTX* /*context*/, const EVP_MD* /*digest*/)
{
    return 0;   // failure
}

inline int EVP_DigestUpdate(EVP_MD_CTX* /*context*/, const void* /*data*/, std::size_t /*size*/)
{
    return 0;
}

inline int EVP_DigestFinal(EVP_MD_CTX* /*context*/, unsigned char* /*out*/, unsigned int* size)
{
    if (size != nullptr) {
        *size = 0;
    }
    return 0;
}
