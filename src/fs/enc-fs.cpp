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
        {
            uint8_t byte = (uint8_t)strtoul(hashStr.substring(i, i + 2).c_str(), nullptr, 16);
            key.push_back(byte);
        }
        return key; // 32 bytes for AES-256
    }

    // --- Path encryption/decryption ---
    String encryptPath(const String &plainPath)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> plain(plainPath.begin(), plainPath.end());
        std::vector<uint8_t> enc = Crypto::AES::encrypt(plain, key);

        String hex;
        for (uint8_t b : enc)
        {
            if (b < 16)
                hex += '0';
            hex += String(b, HEX);
        }
        return hex;
    }

    String decryptPath(const String &encPath)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> enc;
        for (size_t i = 0; i < encPath.length(); i += 2)
            enc.push_back((uint8_t)strtoul(encPath.substring(i, i + 2).c_str(), nullptr, 16));

        std::vector<uint8_t> dec = Crypto::AES::decrypt(enc, key);
        return String((char *)dec.data(), dec.size());
    }

    // --- Directory operations ---
    vector<String> readDir(const String &path)
    {
        vector<File> encList = SD_FS::readDir(encryptPath(path));
        vector<String> decList;

        for (auto &f : encList)
            decList.push_back(decryptPath(f.name()));

        return decList;
    }

    bool createDir(const String &path) { return SD_FS::createDir(encryptPath(path)); }
    bool deleteDir(const String &path) { return SD_FS::deleteDir(encryptPath(path)); }

    // --- File operations ---
    bool writeFile(const String &path, const vector<uint8_t> &data)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> enc = Crypto::AES::encrypt(data, key);
        return SD_FS::writeFile(encryptPath(path), enc); // Use vector<uint8_t> version
    }

    vector<uint8_t> readFile(const String &path)
    {
        std::vector<uint8_t> key = getKey();
        vector<uint8_t> enc = SD_FS::readFile(encryptPath(path));
        return Crypto::AES::decrypt(enc, key);
    }

    bool appendFile(const String &path, const vector<uint8_t> &data)
    {
        std::vector<uint8_t> key = getKey();
        std::vector<uint8_t> enc = Crypto::AES::encrypt(data, key);
        return SD_FS::appendFile(encryptPath(path), enc);
    }

    bool deleteFile(const String &path) { return SD_FS::deleteFile(encryptPath(path)); }
    bool exists(const String &path) { return SD_FS::exists(encryptPath(path)); }
    bool isDirectory(const String &path) { return SD_FS::isDirectory(encryptPath(path)); }
    size_t fileSize(const String &path) { return readFile(path).size(); }
    void getFileInfo(const String &path) { SD_FS::getFileInfo(encryptPath(path)); }

    // --- Chunked file operations ---
    vector<uint8_t> readFileChunk(const String &path, size_t index, size_t len)
    {
        vector<uint8_t> full = readFile(path);
        if (index >= full.size())
            return {};
        size_t chunkLen = (index + len > full.size()) ? full.size() - index : len;
        return vector<uint8_t>(full.begin() + index, full.begin() + index + chunkLen);
    }

    bool writeFileChunk(const String &path, size_t index, const vector<uint8_t> &data)
    {
        vector<uint8_t> full = readFile(path);
        if (index > full.size())
            return false;
        if (index + data.size() > full.size())
            full.resize(index + data.size());
        std::copy(data.begin(), data.end(), full.begin() + index);
        return writeFile(path, full);
    }

    // --- Card info ---
    uint64_t getCardSize() { return SD_FS::getCardSize(); }
    uint64_t getUsedBytes() { return SD_FS::getUsedBytes(); }
    uint64_t getFreeBytes() { return SD_FS::getFreeBytes(); }
    void getUsageSummary() { SD_FS::getUsageSummary(); }
}
