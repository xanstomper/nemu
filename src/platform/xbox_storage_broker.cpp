#include "xbox_storage_broker.hpp"
#include "logger.hpp"
#include <algorithm>

namespace nemu::platform {

XboxStorageBroker::XboxStorageBroker() {
    // Populate standard Xbox Developer Mode drive roots
    EnumerateDrives();
}

std::vector<BrokeredDriveInfo> XboxStorageBroker::EnumerateDrives() {
    std::lock_guard lock(broker_mutex_);
    cached_drives_.clear();

    struct DriveSpec {
        std::string letter;
        std::string name;
        std::string badge;
        std::filesystem::path host_path;
    };

    const std::vector<DriveSpec> potential_drives = {
        {"LOCAL:/", "Internal LocalState Storage", "[LOCAL]", std::filesystem::path("./")},
        {"sdmc:/",  "Virtual SD Card",             "[SDMC]",  std::filesystem::path("./sdmc")},
        {"save:/",  "Persistent Saves & Configs",  "[SAVE]",  std::filesystem::path("./save")},
        {"D:/",     "External USB Drive 1",        "[USB]",   std::filesystem::path("D:/")},
        {"E:/",     "External USB Drive 2",        "[USB]",   std::filesystem::path("E:/")},
        {"F:/",     "External USB Drive 3",        "[USB]",   std::filesystem::path("F:/")},
        {"G:/",     "External USB Drive 4",        "[USB]",   std::filesystem::path("G:/")}
    };

    std::error_code ec;
    for (const auto& spec : potential_drives) {
        BrokeredDriveInfo info{
            .drive_letter = spec.letter,
            .display_name = spec.name,
            .badge = spec.badge,
            .is_online = false,
            .total_bytes = 0,
            .free_bytes = 0,
            .host_root = spec.host_path
        };

        if (std::filesystem::exists(spec.host_path, ec)) {
            info.is_online = true;
            auto space = std::filesystem::space(spec.host_path, ec);
            if (!ec) {
                info.total_bytes = space.capacity;
                info.free_bytes = space.available;
            }
        }

        cached_drives_.push_back(std::move(info));
    }

    NEMU_LOG_INFO("StorageBroker", "Discovered {} storage mount targets on Xbox platform", cached_drives_.size());
    return cached_drives_;
}

bool XboxStorageBroker::RegisterFolderToken(std::string_view token, const std::filesystem::path& folder_path) {
    std::lock_guard lock(broker_mutex_);
    std::error_code ec;
    if (!std::filesystem::exists(folder_path, ec)) {
        NEMU_LOG_WARN("StorageBroker", "Cannot register token '{}': folder path does not exist ({})",
                      token, folder_path.string());
        return false;
    }

    future_access_list_[std::string(token)] = folder_path;
    NEMU_LOG_INFO("StorageBroker", "Registered FutureAccessList token '{}' -> '{}'",
                  token, folder_path.string());
    return true;
}

std::optional<std::filesystem::path> XboxStorageBroker::ResolveToken(std::string_view token) const {
    std::lock_guard lock(broker_mutex_);
    auto it = future_access_list_.find(std::string(token));
    if (it != future_access_list_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool XboxStorageBroker::CanDirectStream(std::string_view virtual_path) const noexcept {
    // Large games on external USBs or SDMC can always be direct-streamed via 64KB chunked I/O
    if (virtual_path.starts_with("D:/") || virtual_path.starts_with("E:/") ||
        virtual_path.starts_with("F:/") || virtual_path.starts_with("G:/") ||
        virtual_path.starts_with("sdmc:/") || virtual_path.starts_with("LOCAL:/")) {
        return true;
    }
    return false;
}

std::optional<std::filesystem::path> XboxStorageBroker::ResolveBrokeredPath(std::string_view virtual_path) const {
    std::lock_guard lock(broker_mutex_);
    for (const auto& drive : cached_drives_) {
        if (virtual_path.starts_with(drive.drive_letter)) {
            std::string sub = std::string(virtual_path.substr(drive.drive_letter.size()));
            if (!sub.empty() && sub.front() == '/') sub.erase(sub.begin());
            return drive.host_root / sub;
        }
    }
    return std::nullopt;
}

std::vector<std::string> XboxStorageBroker::GetRegisteredTokens() const {
    std::lock_guard lock(broker_mutex_);
    std::vector<std::string> res;
    res.reserve(future_access_list_.size());
    for (const auto& [token, _] : future_access_list_) {
        res.push_back(token);
    }
    std::sort(res.begin(), res.end());
    return res;
}

void XboxStorageBroker::ClearTokens() {
    std::lock_guard lock(broker_mutex_);
    future_access_list_.clear();
    NEMU_LOG_INFO("StorageBroker", "Cleared FutureAccessList persistent authorization tokens");
}

} // namespace nemu::platform
