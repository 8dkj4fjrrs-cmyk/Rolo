#pragma once

// Rolo's stand-in for OpenSSL's AES header (ADR 0024). Rolo builds Axmol
// without OpenSSL. Axmol's core mentions these names in one place only: the
// optional encryption of its settings file (UserDefault::setEncryptEnabled),
// which Rolo never switches on. This is Rolo's own code; it contains no
// OpenSSL code and does no encryption.
//
// Should anything ever ask for encryption, the program stops with a message:
// writing a file as plain text while calling it encrypted would be worse.

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#define AES_ENCRYPT 1
#define AES_DECRYPT 0

struct AES_KEY {
    int unused = 0;
};

inline int AES_set_encrypt_key(const unsigned char* /*userKey*/, int /*bits*/, AES_KEY* /*key*/)
{
    return -1;   // failure
}

[[noreturn]] inline void AES_cfb128_encrypt(const unsigned char* /*in*/, unsigned char* /*out*/, std::size_t /*size*/,
                                            const AES_KEY* /*key*/, unsigned char* /*iv*/, int* /*num*/, int /*enc*/)
{
    std::fputs("Rolo: Axmol's settings encryption was asked for, but Rolo is built without OpenSSL "
               "(docs/adr/0024-no-libpng-no-openssl.md).\n",
               stderr);
    std::abort();
}
