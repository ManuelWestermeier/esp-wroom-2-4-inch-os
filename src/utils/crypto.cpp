#include "Crypto.hpp"
#include <cstring>
#include <cstdio>

namespace Crypto
{
    namespace AES
    {
        void genIV(uint8_t iv[16])
        {
            for (int i = 0; i < 16; i++)
                iv[i] = esp_random() & 0xFF;
        }

        void pad(std::vector<uint8_t> &data)
        {
            size_t padLen = 16 - (data.size() % 16);
            data.insert(data.end(), padLen, (uint8_t)padLen);
        }

        void unpad(std::vector<uint8_t> &data)
        {
            if (data.empty())
                return;

            uint8_t padLen = data.back();
            if (padLen == 0 || padLen > 16 || padLen > data.size())
                return; // invalid padding

            data.resize(data.size() - padLen);
        }

        std::vector<uint8_t> encrypt(const std::vector<uint8_t> &input, const std::vector<uint8_t> &key)
        {
            std::vector<uint8_t> data = input;
            pad(data);

            uint8_t iv[16];
            genIV(iv);

            mbedtls_aes_context ctx;
            mbedtls_aes_init(&ctx);
            mbedtls_aes_setkey_enc(&ctx, key.data(), 256);

            std::vector<uint8_t> out(data.size() + 16);
            memcpy(out.data(), iv, 16); // prefix IV

            for (size_t i = 0; i < data.size(); i += 16)
            {
                uint8_t block[16];
                for (int j = 0; j < 16; j++)
                    block[j] = data[i + j] ^ iv[j];

                mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, block, block);
                memcpy(out.data() + 16 + i, block, 16);
                memcpy(iv, block, 16);
            }

            mbedtls_aes_free(&ctx);
            return out;
        }

        std::vector<uint8_t> decrypt(const std::vector<uint8_t> &cipher, const std::vector<uint8_t> &key)
        {
            if (cipher.size() < 16)
                return {};

            uint8_t iv[16];
            memcpy(iv, cipher.data(), 16);

            std::vector<uint8_t> data(cipher.size() - 16);
            memcpy(data.data(), cipher.data() + 16, data.size());

            mbedtls_aes_context ctx;
            mbedtls_aes_init(&ctx);
            mbedtls_aes_setkey_dec(&ctx, key.data(), 256);

            for (size_t i = 0; i < data.size(); i += 16)
            {
                uint8_t block[16];
                memcpy(block, data.data() + i, 16);
                uint8_t tmp[16];
                mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, block, tmp);

                for (int j = 0; j < 16; j++)
                    data[i + j] = tmp[j] ^ iv[j];

                memcpy(iv, block, 16);
            }

            mbedtls_aes_free(&ctx);
            unpad(data);
            return data;
        }
    }

    namespace HASH
    {
        String sha256String(const String &text)
        {
            uint8_t hash[32];
            mbedtls_sha256((const uint8_t *)text.c_str(), text.length(), hash, 0);

            char buf[3];
            String out;
            out.reserve(64);
            for (int i = 0; i < 32; i++)
            {
                sprintf(buf, "%02x", hash[i]);
                out += buf;
            }
            return out;
        }
    }
}
