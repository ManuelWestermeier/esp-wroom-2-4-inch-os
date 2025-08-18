#include "enc-fs.hpp"

namespace EncFS
{
    // --- Internal AES key derived from Auth::password ---
    static std::vector<uint8_t> getKey()
    {
        String hashStr = Crypto::HASH::sha256String(Auth::password); // 64 hex chars
        std::vector<uint8_t> key;
        key.reserve(32);
        for (size_t i = 0; i < 64; i += 2)
            key.push_back((uint8_t)strtoul(hashStr.substring(i, i + 2).c_str(), nullptr, 16));
        return key; // 32 bytes for AES-256
    }

    // --- Convert bytes to hex string ---
    static String bytesToHex(const std::vector<uint8_t> &data)
    {
        String hex;
        for (uint8_t b : data)
        {
            if (b < 16)
                hex += '0';
            hex += String(b, HEX);
        }
        return hex;
    }

    // --- Convert hex string to bytes ---
    static std::vector<uint8_t> hexToBytes(const String &hex)
    {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.length(); i += 2)
        {
            bytes.push_back((uint8_t)strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16));
        }
        return bytes;
    }

    // --- Path encryption/decryption ---
    String encryptPath(const String &plainPath)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> plain(plainPath.begin(), plainPath.end());
        std::vector<uint8_t> enc = Crypto::AES::encrypt(plain, key);
        return bytesToHex(enc);
    }

    String decryptPath(const String &encPath)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> enc = hexToBytes(encPath);
        std::vector<uint8_t> dec = Crypto::AES::decrypt(enc, key);
        return String((char *)dec.data(), dec.size());
    }

    // --- File operations ---
    bool writeFile(const String &path, const std::vector<uint8_t> &data)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> enc = Crypto::AES::encrypt(data, key);

        // Convert encrypted bytes to String for SD_FS
        String encStr((char *)enc.data(), enc.size());
        return SD_FS::writeFile(encryptPath(path), encStr);
    }

    std::vector<uint8_t> readFile(const String &path)
    {
        std::vector<uint8_t> key = getKey();
        String encStr = SD_FS::readFile(encryptPath(path));

        // Convert String back to bytes
        std::vector<uint8_t> enc(encStr.begin(), encStr.end());
        return Crypto::AES::decrypt(enc, key);
    }
}
