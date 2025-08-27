// /*
//  * rsa_aes_demo.cpp - Beispiel für ESP32 mit mbedTLS
//  *
//  * Features:
//  *  - RSA Keypair generieren (Public/Private)
//  *  - RSA Verschlüsseln / Entschlüsseln
//  *  - AES Key + IV generieren
//  *  - AES Verschlüsseln / Entschlüsseln (CBC Mode)
//  *
//  * Getestet mit ESP-IDF / Arduino-ESP32 (mbedTLS ist eingebaut)
//  */

// #include <stdio.h>
// #include <string.h>
// #include "mbedtls/pk.h"
// #include "mbedtls/rsa.h"
// #include "mbedtls/aes.h"
// #include "mbedtls/entropy.h"
// #include "mbedtls/ctr_drbg.h"

// #define AES_KEY_SIZE 32 // 256-bit
// #define AES_IV_SIZE 16

// // Globale AES Key + IV
// static uint8_t aesKey[AES_KEY_SIZE];
// static uint8_t aesIV[AES_IV_SIZE];

// // mbedTLS Entropy/DRBG
// mbedtls_entropy_context entropy;
// mbedtls_ctr_drbg_context ctr_drbg;

// // Init Random Generator
// void init_rng()
// {
//   mbedtls_entropy_init(&entropy);
//   mbedtls_ctr_drbg_init(&ctr_drbg);
//   const char *pers = "rsa_aes_demo";
//   if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
//                             (const unsigned char *)pers, strlen(pers)) != 0)
//   {
//     printf("Random init failed!\n");
//   }
// }

// // =============================
// // RSA FUNCTIONS
// // =============================

// void rsa_generate_keypair(mbedtls_pk_context *pk, int bits)
// {
//   mbedtls_pk_init(pk);
//   if (mbedtls_pk_setup(pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0)
//   {
//     printf("pk_setup failed\n");
//     return;
//   }
//   if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(*pk),
//                           mbedtls_ctr_drbg_random, &ctr_drbg,
//                           bits, 65537) != 0)
//   {
//     printf("RSA keygen failed\n");
//   }
// }

// int rsa_encrypt(mbedtls_pk_context *pk, const uint8_t *input, size_t inLen,
//                 uint8_t *output, size_t *outLen, size_t outBufSize)
// {
//   return mbedtls_pk_encrypt(pk, input, inLen, output, outLen, outBufSize,
//                             mbedtls_ctr_drbg_random, &ctr_drbg);
// }

// int rsa_decrypt(mbedtls_pk_context *pk, const uint8_t *input, size_t inLen,
//                 uint8_t *output, size_t *outLen, size_t outBufSize)
// {
//   return mbedtls_pk_decrypt(pk, input, inLen, output, outLen, outBufSize,
//                             mbedtls_ctr_drbg_random, &ctr_drbg);
// }

// // =============================
// // AES FUNCTIONS
// // =============================

// void aes_generate_key_iv()
// {
//   mbedtls_ctr_drbg_random(&ctr_drbg, aesKey, AES_KEY_SIZE);
//   mbedtls_ctr_drbg_random(&ctr_drbg, aesIV, AES_IV_SIZE);
// }

// void aes_encrypt(const uint8_t *input, size_t len, uint8_t *output)
// {
//   mbedtls_aes_context aes;
//   mbedtls_aes_init(&aes);
//   mbedtls_aes_setkey_enc(&aes, aesKey, AES_KEY_SIZE * 8);

//   uint8_t iv[AES_IV_SIZE];
//   memcpy(iv, aesIV, AES_IV_SIZE);

//   mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, len, iv, input, output);
//   mbedtls_aes_free(&aes);
// }

// void aes_decrypt(const uint8_t *input, size_t len, uint8_t *output)
// {
//   mbedtls_aes_context aes;
//   mbedtls_aes_init(&aes);
//   mbedtls_aes_setkey_dec(&aes, aesKey, AES_KEY_SIZE * 8);

//   uint8_t iv[AES_IV_SIZE];
//   memcpy(iv, aesIV, AES_IV_SIZE);

//   mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, input, output);
//   mbedtls_aes_free(&aes);
// }

// // =============================
// // DEMO MAIN
// // =============================

// int main()
// {
//   init_rng();

//   // 1. RSA Keypair generieren
//   mbedtls_pk_context pk;
//   rsa_generate_keypair(&pk, 2048);
//   printf("RSA Keypair generiert.\n");

//   // 2. AES Key+IV generieren
//   aes_generate_key_iv();
//   printf("AES Key+IV generiert.\n");

//   // 3. Beispieltext
//   const char *message = "Hallo Welt vom ESP32 mit RSA+AES!";
//   size_t msgLen = strlen(message) + 1; // inkl. Nullterminator

//   // 4. AES verschlüsseln
//   uint8_t aesEnc[128];
//   aes_encrypt((const uint8_t *)message, msgLen, aesEnc);
//   printf("AES encrypted done.\n");

//   // 5. AES entschlüsseln
//   uint8_t aesDec[128];
//   aes_decrypt(aesEnc, msgLen, aesDec);
//   printf("AES decrypted: %s\n", aesDec);

//   // 6. RSA verschlüsseln (AES Key+IV)
//   uint8_t keyBlock[AES_KEY_SIZE + AES_IV_SIZE];
//   memcpy(keyBlock, aesKey, AES_KEY_SIZE);
//   memcpy(keyBlock + AES_KEY_SIZE, aesIV, AES_IV_SIZE);

//   uint8_t rsaEnc[512];
//   size_t rsaEncLen;
//   rsa_encrypt(&pk, keyBlock, sizeof(keyBlock), rsaEnc, &rsaEncLen, sizeof(rsaEnc));
//   printf("RSA encrypted AES key+IV.\n");

//   // 7. RSA entschlüsseln (Key+IV wiederherstellen)
//   uint8_t rsaDec[512];
//   size_t rsaDecLen;
//   rsa_decrypt(&pk, rsaEnc, rsaEncLen, rsaDec, &rsaDecLen, sizeof(rsaDec));
//   printf("RSA decrypted AES key+IV (%d bytes).\n", (int)rsaDecLen);

//   return 0;
// }
// // V2
// #include "mbedtls/rsa.h"
// #include "mbedtls/pk.h"
// #include "mbedtls/ctr_drbg.h"
// #include "mbedtls/entropy.h"

// mbedtls_rsa_context rsa;
// mbedtls_entropy_context entropy;
// mbedtls_ctr_drbg_context ctr_drbg;

// void setup()
// {
//   Serial.begin(115200);

//   // Init
//   mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V15, 0);
//   mbedtls_ctr_drbg_init(&ctr_drbg);
//   mbedtls_entropy_init(&entropy);
//   mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);

//   // Generate 2048-bit RSA keypair
//   Serial.println("Generating RSA keypair...");
//   int ret = mbedtls_rsa_gen_key(&rsa, mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
//   if (ret != 0)
//   {
//     Serial.printf("Keygen failed! -0x%04X\n", -ret);
//     return;
//   }
//   Serial.println("Keypair generated.");

//   // Encrypt / Decrypt test
//   const char *message = "Hello RSA on ESP32!";
//   unsigned char encrypted[256];
//   unsigned char decrypted[256];

//   // Encrypt with public key
//   size_t olen;
//   ret = mbedtls_rsa_pkcs1_encrypt(&rsa, mbedtls_ctr_drbg_random, &ctr_drbg,
//                                   MBEDTLS_RSA_PUBLIC, strlen(message),
//                                   (const unsigned char *)message, encrypted);
//   if (ret == 0)
//   {
//     Serial.println("Encryption successful.");
//   }

//   // Decrypt with private key
//   ret = mbedtls_rsa_pkcs1_decrypt(&rsa, mbedtls_ctr_drbg_random, &ctr_drbg,
//                                   MBEDTLS_RSA_PRIVATE, &olen,
//                                   encrypted, decrypted, sizeof(decrypted));
//   if (ret == 0)
//   {
//     decrypted[olen] = '\0';
//     Serial.printf("Decrypted: %s\n", decrypted);
//   }
// }

// void loop() {}
// // V3
// #include "mbedtls/aes.h"

// void printHex(const char *label, const unsigned char *data, size_t len)
// {
//   Serial.print(label);
//   for (size_t i = 0; i < len; i++)
//   {
//     Serial.printf("%02X", data[i]);
//   }
//   Serial.println();
// }

// void setup()
// {
//   Serial.begin(115200);

//   // AES-256 key (32 bytes)
//   unsigned char key[32] = {
//       0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
//       0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
//       0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
//       0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4};

//   // Initialization Vector (16 bytes)
//   unsigned char iv[16] = {
//       0x00, 0x01, 0x02, 0x03,
//       0x04, 0x05, 0x06, 0x07,
//       0x08, 0x09, 0x0a, 0x0b,
//       0x0c, 0x0d, 0x0e, 0x0f};

//   const char *plaintext = "Hello AES256 on ESP32!";
//   size_t input_len = strlen(plaintext);

//   // AES requires blocks of 16 bytes → pad message
//   size_t padded_len = ((input_len + 15) / 16) * 16;
//   unsigned char input[64] = {0};
//   unsigned char encrypted[64] = {0};
//   unsigned char decrypted[64] = {0};

//   memcpy(input, plaintext, input_len);

//   // Encrypt
//   mbedtls_aes_context aes;
//   mbedtls_aes_init(&aes);
//   mbedtls_aes_setkey_enc(&aes, key, 256);
//   unsigned char iv_enc[16];
//   memcpy(iv_enc, iv, 16);
//   mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len, iv_enc, input, encrypted);

//   printHex("Encrypted: ", encrypted, padded_len);

//   // Decrypt
//   mbedtls_aes_setkey_dec(&aes, key, 256);
//   unsigned char iv_dec[16];
//   memcpy(iv_dec, iv, 16);
//   mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, padded_len, iv_dec, encrypted, decrypted);

//   Serial.printf("Decrypted: %s\n", decrypted);

//   mbedtls_aes_free(&aes);
// }

// void loop() {}
