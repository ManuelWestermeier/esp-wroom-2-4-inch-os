#pragma once
#include <Arduino.h>
#include <vector>
#include <string>

#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"

namespace Crypto
{
    namespace AES
    {
        void genIV(uint8_t iv[16]);
        void pad(std::vector<uint8_t> &data);
        void unpad(std::vector<uint8_t> &data);

        // Encrypt raw bytes with AES-256-CBC, returns IV + ciphertext
        std::vector<uint8_t> encrypt(const std::vector<uint8_t> &data, const std::vector<uint8_t> &key);

        // Decrypt raw bytes with AES-256-CBC, expects IV + ciphertext
        std::vector<uint8_t> decrypt(const std::vector<uint8_t> &cipher, const std::vector<uint8_t> &key);
    }

    namespace HASH
    {
        String sha256String(const String &text);
        String sha256StringMul(const String &text, const int it);
    }
}
