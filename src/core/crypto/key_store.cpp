#include "key_store.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <shared_mutex>

namespace nemu::core::crypto {

namespace {

std::string ToLower(std::string_view str) {
    std::string result(str);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return result;
}

std::string Trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    size_t last = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(first, (last - first + 1)));
}

} // namespace

KeyStore::KeyStore() = default;

std::optional<std::vector<u8>> KeyStore::HexToBytes(std::string_view hex) {
    std::string clean;
    clean.reserve(hex.size());
    for (char c : hex) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            clean.push_back(c);
        }
    }

    if (clean.size() % 2 != 0) {
        return std::nullopt;
    }

    std::vector<u8> bytes;
    bytes.reserve(clean.size() / 2);

    for (size_t i = 0; i < clean.size(); i += 2) {
        auto CharToNibble = [](char c) -> u8 {
            if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
            return 0;
        };

        u8 hi = CharToNibble(clean[i]);
        u8 lo = CharToNibble(clean[i + 1]);
        bytes.push_back(static_cast<u8>((hi << 4) | lo));
    }

    return bytes;
}

std::string KeyStore::BytesToHex(std::span<const u8> bytes) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (u8 b : bytes) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

bool KeyStore::LoadFromText(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::string line;
    size_t loaded_count = 0;

    std::unique_lock lock(mutex_);

    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key_name = ToLower(Trim(line.substr(0, eq_pos)));
        std::string key_hex = Trim(line.substr(eq_pos + 1));

        auto bytes = HexToBytes(key_hex);
        if (bytes.has_value() && !bytes->empty()) {
            keys_[key_name] = std::move(*bytes);
            ++loaded_count;
        }
    }

    return loaded_count > 0;
}

bool KeyStore::LoadFromFile(std::string_view file_path) {
    std::ifstream file(std::string(file_path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return LoadFromText(ss.str());
}

void KeyStore::SetKey(std::string_view key_name, std::span<const u8> key_bytes) {
    std::unique_lock lock(mutex_);
    keys_[ToLower(key_name)] = std::vector<u8>(key_bytes.begin(), key_bytes.end());
}

std::optional<std::vector<u8>> KeyStore::GetKey(std::string_view key_name) const {
    std::shared_lock lock(mutex_);
    auto it = keys_.find(ToLower(key_name));
    if (it != keys_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool KeyStore::HasKey(std::string_view key_name) const {
    std::shared_lock lock(mutex_);
    return keys_.find(ToLower(key_name)) != keys_.end();
}

size_t KeyStore::Count() const {
    std::shared_lock lock(mutex_);
    return keys_.size();
}

std::optional<std::vector<u8>> KeyStore::GetHeaderKey() const {
    return GetKey("header_key");
}

std::optional<std::vector<u8>> KeyStore::GetKeyAreaKey(u8 generation, u8 type) const {
    std::ostringstream ss;
    const char* type_str = (type == 1) ? "ocean" : (type == 2 ? "system" : "application");
    ss << "key_area_key_" << type_str << "_" << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(generation);
    return GetKey(ss.str());
}

} // namespace nemu::core::crypto
