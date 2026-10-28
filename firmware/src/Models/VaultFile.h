#ifndef VAULT_FILE_H
#define VAULT_FILE_H

#include <string>
#include <vector>
#include <algorithm>
#include <States/GlobalState.h>

class VaultFile {
private:
    std::string path; 
    std::vector<uint8_t> data; // raw data (salt + checksum + encrypted data)

    GlobalState& globalState = GlobalState::getInstance();

public:
    VaultFile(const std::string& filePath, const std::vector<uint8_t>& rawData)
        : path(filePath), data(rawData) {}

    const std::string& getPath() const { return path; }

    std::vector<uint8_t> getSalt() const {
        size_t saltSize = globalState.getSaltSize();
        if (data.size() >= saltSize) {
            return {data.begin(), data.begin() + saltSize};
        }
        return {};
    }

    std::vector<uint8_t> getChecksum() const {
        size_t saltSize = globalState.getSaltSize();
        size_t checksumSize = globalState.getChecksumSize();
        if (data.size() >= saltSize + checksumSize) {
            return {data.begin() + saltSize, data.begin() + saltSize + checksumSize};
        }
        return {};
    }

    std::vector<uint8_t> getData() const {
        return data;
    }

    std::vector<uint8_t> getEncryptedData() const {
        size_t saltSize = globalState.getSaltSize();
        size_t checksumSize = globalState.getChecksumSize();
        if (data.size() > saltSize + checksumSize) {
            return {data.begin() + saltSize + checksumSize, data.end()};
        }
        return {};
    }

    void setPath(const std::string& filePath) { path = filePath; }

    void setData(const std::vector<uint8_t>& rawData) { data = rawData; }

    void setSalt(const std::vector<uint8_t>& salt) {
        size_t saltSize = globalState.getSaltSize();
        if (data.size() < saltSize) {
            data.resize(saltSize, 0);
        }
        std::copy(salt.begin(), salt.begin() + std::min(salt.size(), saltSize), data.begin());
    }

    void setChecksum(const std::vector<uint8_t>& checksum) {
        size_t saltSize = globalState.getSaltSize();
        size_t checksumSize = globalState.getChecksumSize();
        if (data.size() < saltSize + checksumSize) {
            data.resize(saltSize + checksumSize, 0);
        }
        std::copy(checksum.begin(), checksum.begin() + std::min(checksum.size(), checksumSize), data.begin() + saltSize);
    }

    void setEncryptedData(const std::vector<uint8_t>& encryptedData) {
        size_t saltSize = globalState.getSaltSize();
        size_t checksumSize = globalState.getChecksumSize();

        if (data.size() < saltSize + checksumSize) {
            data.resize(saltSize + checksumSize, 0);
        }

        data.erase(data.begin() + saltSize + checksumSize, data.end());
        data.insert(data.end(), encryptedData.begin(), encryptedData.end());
    }
};

#endif // VAULT_FILE_H
