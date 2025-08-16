#pragma once
#include <Arduino.h>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"

namespace Crypto
{

    namespace AES
    {

        // Generate a random 16-byte IV
        inline void genIV(byte *iv)
        {
            for (int i = 0; i < 16; i++)
                iv[i] = esp_random() & 0xFF;
        }

        // PKCS7 padding
        inline void pad(uint8_t *&data, size_t &len)
        {
            size_t padLen = 16 - (len % 16);
            uint8_t *newData = new uint8_t[len + padLen];
            memcpy(newData, data, len);
            for (size_t i = 0; i < padLen; i++)
                newData[len + i] = padLen;
            delete[] data;
            data = newData;
            len += padLen;
        }

        inline void unpad(uint8_t *&data, size_t &len)
        {
            size_t padLen = data[len - 1];
            len -= padLen;
            uint8_t *newData = new uint8_t[len];
            memcpy(newData, data, len);
            delete[] data;
            data = newData;
        }

        // Encrypt a string, returns base64-like binary with IV prefixed
        inline String encryptString(const String &text, const String &keyStr)
        {
            size_t keyLen = 32;
            byte key[32] = {0};
            memcpy(key, keyStr.c_str(), min(keyStr.length(), keyLen));

            size_t len = text.length();
            uint8_t *data = new uint8_t[len];
            memcpy(data, text.c_str(), len);
            pad(data, len);

            byte iv[16];
            genIV(iv);

            mbedtls_aes_context ctx;
            mbedtls_aes_init(&ctx);
            mbedtls_aes_setkey_enc(&ctx, key, 256);

            for (size_t i = 0; i < len; i += 16)
            {
                byte block[16];
                for (int j = 0; j < 16; j++)
                    block[j] = data[i + j] ^ iv[j];
                mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, block, block);
                memcpy(data + i, block, 16);
                memcpy(iv, block, 16);
            }

            String out;
            out.reserve(len + 16);
            for (int i = 0; i < 16; i++)
                out += (char)iv[i]; // prefix IV
            for (size_t i = 0; i < len; i++)
                out += (char)data[i];

            delete[] data;
            mbedtls_aes_free(&ctx);
            return out;
        }

        // Decrypt string (IV-prefixed)
        inline String decryptString(const String &cipher, const String &keyStr)
        {
            if (cipher.length() < 16)
                return "";

            size_t keyLen = 32;
            byte key[32] = {0};
            memcpy(key, keyStr.c_str(), min(keyStr.length(), keyLen));

            size_t len = cipher.length() - 16;
            uint8_t *data = new uint8_t[len];
            memcpy(data, cipher.c_str() + 16, len);

            byte iv[16];
            memcpy(iv, cipher.c_str(), 16);

            mbedtls_aes_context ctx;
            mbedtls_aes_init(&ctx);
            mbedtls_aes_setkey_dec(&ctx, key, 256);

            for (size_t i = 0; i < len; i += 16)
            {
                byte block[16];
                memcpy(block, data + i, 16);
                byte tmp[16];
                mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, block, tmp);
                for (int j = 0; j < 16; j++)
                    data[i + j] = tmp[j] ^ iv[j];
                memcpy(iv, block, 16);
            }

            unpad(data, len);
            String out = "";
            for (size_t i = 0; i < len; i++)
                out += (char)data[i];
            delete[] data;
            mbedtls_aes_free(&ctx);
            return out;
        }
    }

    namespace HASH
    {
        String sha256String(const String &text)
        {
            byte hash[32];
            mbedtls_sha256((const byte *)text.c_str(), text.length(), hash, 0);
            String out = "";
            char buf[3];
            for (int i = 0; i < 32; i++)
            {
                sprintf(buf, "%02x", hash[i]);
                out += String(buf);
            }
            return out; // safe, returns by value
        }
    }
}
