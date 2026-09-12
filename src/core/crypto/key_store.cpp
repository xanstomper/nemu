#include "key_store.hpp"
#include "aes.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
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

        std::string key_name;
        std::string key_hex;

        // Try '=' delimiter (standard prod.keys / title.keys)
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            key_name = ToLower(Trim(line.substr(0, eq_pos)));
            key_hex = Trim(line.substr(eq_pos + 1));
        } else {
            // Try '|' delimiter (title database format: rights_id|title_key|title_name)
            size_t pipe_pos = line.find('|');
            if (pipe_pos != std::string::npos) {
                key_name = ToLower(Trim(line.substr(0, pipe_pos)));
                std::string remainder = line.substr(pipe_pos + 1);
                size_t next_pipe = remainder.find('|');
                if (next_pipe != std::string::npos) {
                    key_hex = Trim(remainder.substr(0, next_pipe));
                } else {
                    key_hex = Trim(remainder);
                }
            } else {
                // Try ',' delimiter (CSV format: rights_id,title_key,title_name)
                size_t comma_pos = line.find(',');
                if (comma_pos != std::string::npos) {
                    key_name = ToLower(Trim(line.substr(0, comma_pos)));
                    std::string remainder = line.substr(comma_pos + 1);
                    size_t next_comma = remainder.find(',');
                    if (next_comma != std::string::npos) {
                        key_hex = Trim(remainder.substr(0, next_comma));
                    } else {
                        key_hex = Trim(remainder);
                    }
                }
            }
        }

        if (key_name.empty() || key_hex.empty()) {
            continue;
        }

        auto bytes = HexToBytes(key_hex);
        if (bytes.has_value() && !bytes->empty()) {
            keys_[key_name] = *bytes;
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

std::optional<std::vector<u8>> KeyStore::GetTitleKey(std::string_view rights_id_hex) const {
    std::string hex_str = ToLower(Trim(rights_id_hex));
    if (hex_str.empty()) return std::nullopt;

    // 1. Direct match with rights ID
    auto key = GetKey(hex_str);
    if (key.has_value()) return key;

    // 2. Prefixed with title_key_
    key = GetKey("title_key_" + hex_str);
    if (key.has_value()) return key;

    // 3. Prefixed with titlekey_
    key = GetKey("titlekey_" + hex_str);
    if (key.has_value()) return key;

    // 4. Try matching 16-character Title ID prefix
    if (hex_str.size() >= 16) {
        std::string tid = hex_str.substr(0, 16);
        key = GetKey(tid);
        if (key.has_value()) return key;
        key = GetKey("title_key_" + tid);
        if (key.has_value()) return key;
        key = GetKey("titlekey_" + tid);
        if (key.has_value()) return key;

        // Try base revision (all zeros suffix)
        key = GetKey(tid + "0000000000000000");
        if (key.has_value()) return key;
        key = GetKey("title_key_" + tid + "0000000000000000");
        if (key.has_value()) return key;
        key = GetKey("titlekey_" + tid + "0000000000000000");
        if (key.has_value()) return key;
    }

    // 5. Search keys for any entry sharing the same 16-character Title ID prefix
    if (hex_str.size() >= 16) {
        std::string tid = hex_str.substr(0, 16);
        std::shared_lock lock(mutex_);
        for (const auto& [name, kbytes] : keys_) {
            if (name.size() == 32 && name.rfind(tid, 0) == 0) {
                return kbytes;
            }
            if (name.rfind("title_key_" + tid, 0) == 0 || name.rfind("titlekey_" + tid, 0) == 0) {
                return kbytes;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::vector<u8>> KeyStore::GetTitleKeyDecrypted(
    std::string_view rights_id_hex,
    std::span<const u8> encrypted_title_key,
    std::optional<u8> key_generation) const {
    // 1. Already-derived key wins.
    auto derived = GetTitleKey(rights_id_hex);
    if (derived.has_value()) return derived;

    // 2. Otherwise decrypt an encrypted title-key blob with the matching
    //    titlekek. The title key is AES-128-ECB-encrypted under the titlekek;
    //    decryption recovers the plaintext key used to decrypt the NCA body.
    if (encrypted_title_key.size() != 16) {
        return std::nullopt;
    }
    auto kek = GetTitleKek(rights_id_hex, key_generation);
    if (!kek.has_value() || kek->size() != 16) {
        return std::nullopt;
    }
    crypto::Aes128 kek_cipher(std::span<const u8, 16>(kek->data(), 16));
    std::array<u8, 16> plain{};
    kek_cipher.DecryptBlock(std::span<const u8, 16>(encrypted_title_key.data(), 16), plain);
    return std::vector<u8>(plain.begin(), plain.end());
}

/// Resolve the titlekek for a Rights ID (titlekek_<index> or titlekek_index,
/// falling back to the base titlekek name).
std::optional<std::vector<u8>> KeyStore::GetTitleKek(
    std::string_view rights_id_hex,
    std::optional<u8> key_generation) const {
    // 1. If key generation index is explicitly given, try titlekek_%02x and titlekek_%x
    if (key_generation.has_value()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "titlekek_%02x", *key_generation);
        auto by_gen = GetKey(buf);
        if (by_gen.has_value()) return by_gen;

        std::snprintf(buf, sizeof(buf), "titlekek_%x", *key_generation);
        by_gen = GetKey(buf);
        if (by_gen.has_value()) return by_gen;
    }

    // 2. If rights ID is 32 hex chars, characters 30..31 specify the master key revision
    if (rights_id_hex.size() >= 32) {
        std::string idx = ToLower(std::string(rights_id_hex.substr(30, 2)));
        auto by_idx = GetKey("titlekek_" + idx);
        if (by_idx.has_value()) return by_idx;
    }

    // 3. If rights_id_hex is a short 1-2 char index string
    if (rights_id_hex.size() <= 2 && !rights_id_hex.empty()) {
        std::string idx = ToLower(std::string(rights_id_hex));
        auto by_idx = GetKey("titlekek_" + idx);
        if (by_idx.has_value()) return by_idx;
    }

    // 4. Fall back to titlekek_00, titlekek_index, titlekek
    auto by_00 = GetKey("titlekek_00");
    if (by_00.has_value()) return by_00;

    auto by_index = GetKey("titlekek_index");
    if (by_index.has_value()) return by_index;

    return GetKey("titlekek");
}

bool KeyStore::RegisterTicket(std::span<const u8> ticket_data, std::string_view filename_hint) {
    if (ticket_data.size() < 0x190) {
        return false;
    }

    // Determine body offset based on signature type
    u32 sig_type = 0;
    std::memcpy(&sig_type, ticket_data.data(), sizeof(u32));

    size_t body_offset = 0x140; // Default RSA-2048
    if (sig_type == 0x010000 || sig_type == 0x00000100 ||
        sig_type == 0x010003 || sig_type == 0x03000100) {
        body_offset = 0x240; // RSA-4096
    } else if (sig_type == 0x010001 || sig_type == 0x01000100 ||
               sig_type == 0x010004 || sig_type == 0x04000100) {
        body_offset = 0x140; // RSA-2048
    } else if (sig_type == 0x010002 || sig_type == 0x02000100 ||
               sig_type == 0x010005 || sig_type == 0x05000100) {
        body_offset = 0x80;  // ECDSA
    }

    if (ticket_data.size() < body_offset + 0x50) {
        return false;
    }

    // 1. Extract 16-byte encrypted title key (offset 0x40 from body start)
    const u8* key_ptr = ticket_data.data() + body_offset + 0x40;
    std::array<u8, 16> encrypted_title_key{};
    std::memcpy(encrypted_title_key.data(), key_ptr, 16);

    // 2. Extract key generation / master key revision (offset 0x55 from body start)
    std::optional<u8> key_gen;
    if (ticket_data.size() > body_offset + 0x55) {
        key_gen = ticket_data[body_offset + 0x55];
    }

    // 3. Extract Rights ID (offset 0x160 from body start)
    std::string rights_id_hex;
    if (ticket_data.size() >= body_offset + 0x160 + 16) {
        std::array<u8, 16> rights_id_bytes{};
        std::memcpy(rights_id_bytes.data(), ticket_data.data() + body_offset + 0x160, 16);
        bool has_non_zero = std::any_of(rights_id_bytes.begin(), rights_id_bytes.end(), [](u8 b) { return b != 0; });
        if (has_non_zero) {
            rights_id_hex = BytesToHex(rights_id_bytes);
            if (!key_gen.has_value() || *key_gen == 0) {
                if (rights_id_bytes[15] != 0) {
                    key_gen = rights_id_bytes[15];
                }
            }
        }
    }

    // If rights ID in ticket body is empty / all zeros, check filename hint
    if (rights_id_hex.empty() && !filename_hint.empty()) {
        std::string fn(filename_hint);
        for (size_t i = 0; i + 32 <= fn.size(); ++i) {
            std::string sub = fn.substr(i, 32);
            if (std::all_of(sub.begin(), sub.end(), [](char c) {
                return std::isxdigit(static_cast<unsigned char>(c));
            })) {
                rights_id_hex = ToLower(sub);
                break;
            }
        }
    }

    if (rights_id_hex.empty()) {
        return false;
    }

    // 4. Decrypt the title key using titlekek
    auto decrypted_key = GetTitleKeyDecrypted(rights_id_hex, encrypted_title_key, key_gen);
    std::span<const u8> final_key = decrypted_key.has_value() ?
        std::span<const u8>(*decrypted_key) :
        std::span<const u8>(encrypted_title_key);

    // Register under Rights ID
    SetKey(rights_id_hex, final_key);
    SetKey("title_key_" + rights_id_hex, final_key);
    SetKey("titlekey_" + rights_id_hex, final_key);

    // If 32-character Rights ID, also register under the 16-character Title ID
    if (rights_id_hex.size() == 32) {
        std::string title_id_hex = rights_id_hex.substr(0, 16);
        SetKey(title_id_hex, final_key);
        SetKey("title_key_" + title_id_hex, final_key);
        SetKey("titlekey_" + title_id_hex, final_key);
    }

    return true;
}

bool KeyStore::LoadTicketFromFile(std::string_view file_path) {
    std::ifstream file(std::string(file_path), std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    auto size = file.tellg();
    if (size < 0x190) return false;
    std::vector<u8> buffer(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    std::filesystem::path p(file_path);
    return RegisterTicket(buffer, p.filename().string());
}

KeyStore::KeyCompleteness KeyStore::GetKeyCompleteness() const {
    KeyCompleteness c;
    c.has_header_key = HasKey("header_key") || HasKey("header_key_00");
    for (u8 g = 0; g < 32 && !c.has_master_key; ++g) {
        if (HasKey("master_key_00") || HasKey("kermit_master_key_00")) {
            c.has_master_key = true;
        }
    }
    for (u8 g = 0; g < 32 && !c.has_key_area_key; ++g) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "key_area_key_application_%02X", g);
        if (HasKey(buf)) {
            c.has_key_area_key = true;
        }
    }
    c.has_titlekek = HasKey("titlekek") || HasKey("titlekek_index") ||
                     HasKey("titlekek_00") || HasKey("titlekek_01") ||
                     HasKey("titlekek_02") || HasKey("titlekek_03");
    for (const auto& [name, bytes] : keys_) {
        (void)bytes;
        // Title keys may be stored under the bare rights-id hex or a
        // title_key_/titlekey_ prefix.
        if (name.size() == 32 || name.rfind("title_key_", 0) == 0 || name.rfind("titlekey_", 0) == 0) {
            ++c.title_key_count;
        }
    }
    return c;
}

bool KeyStore::LoadDefaultKeys() {
    bool any_loaded = false;

    const char* env_path = std::getenv("NEMU_KEYS_PATH");
    if (env_path != nullptr && env_path[0] != '\0') {
        if (LoadFromFile(env_path)) any_loaded = true;
    }

    static constexpr const char* const kFixedPaths[] = {
        "prod.keys",
        "title.keys",
        "keys/prod.keys",
        "keys/title.keys",
        "switch/prod.keys",
        "switch/title.keys",
        "save/keys/prod.keys",
        "save/keys/title.keys"
    };

    for (const char* path : kFixedPaths) {
        if (LoadFromFile(path)) {
            any_loaded = true;
        }
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        std::string home_str(home);
        if (LoadFromFile(home_str + "/.switch/prod.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.switch/title.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.config/yuzu/keys/prod.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.config/yuzu/keys/title.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.local/share/yuzu/keys/prod.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.local/share/yuzu/keys/title.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.config/Ryujinx/system/prod.keys")) any_loaded = true;
        if (LoadFromFile(home_str + "/.config/Ryujinx/system/title.keys")) any_loaded = true;
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile != nullptr && user_profile[0] != '\0') {
        std::string up_str(user_profile);
        if (LoadFromFile(up_str + "/.switch/prod.keys")) any_loaded = true;
        if (LoadFromFile(up_str + "/.switch/title.keys")) any_loaded = true;
    }

    const char* app_data = std::getenv("APPDATA");
    if (app_data != nullptr && app_data[0] != '\0') {
        std::string ad_str(app_data);
        if (LoadFromFile(ad_str + "/yuzu/keys/prod.keys")) any_loaded = true;
        if (LoadFromFile(ad_str + "/yuzu/keys/title.keys")) any_loaded = true;
        if (LoadFromFile(ad_str + "/Ryujinx/system/prod.keys")) any_loaded = true;
        if (LoadFromFile(ad_str + "/Ryujinx/system/title.keys")) any_loaded = true;
    }

    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data != nullptr && local_app_data[0] != '\0') {
        std::string lad_str(local_app_data);
        if (LoadFromFile(lad_str + "/nemu/keys/prod.keys")) any_loaded = true;
        if (LoadFromFile(lad_str + "/nemu/keys/title.keys")) any_loaded = true;
        if (LoadFromFile(lad_str + "/yuzu/keys/prod.keys")) any_loaded = true;
    }

    static constexpr const char* const kTicketPaths[] = {
        "title.tik",
        "keys/title.tik",
        "save/keys/title.tik",
        "switch/title.tik"
    };

    for (const char* path : kTicketPaths) {
        if (LoadTicketFromFile(path)) {
            any_loaded = true;
        }
    }

    static constexpr const char* const kKeyDirs[] = {
        "keys",
        "save/keys",
        "switch"
    };

    for (const char* dir_path : kKeyDirs) {
        std::error_code ec;
        if (std::filesystem::is_directory(dir_path, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
                if (!ec && entry.is_regular_file() && entry.path().extension() == ".tik") {
                    if (LoadTicketFromFile(entry.path().string())) {
                        any_loaded = true;
                    }
                }
            }
        }
    }

    return any_loaded;
}

} // namespace nemu::core::crypto
