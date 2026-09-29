#pragma once

#include "core/types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>
#include <unordered_map>
#include <mutex>

namespace nemu::platform {

struct BrokeredDriveInfo {
    std::string drive_letter;       // e.g. "D:/"
    std::string display_name;       // e.g. "USB Drive 1 (Games & ROMs)"
    std::string badge;              // e.g. "[USB]"
    bool is_online{false};
    u64 total_bytes{0};
    u64 free_bytes{0};
    std::filesystem::path host_root;
};

class XboxStorageBroker {
public:
    XboxStorageBroker();
    ~XboxStorageBroker() = default;

    /// Refresh and enumerate all accessible storage volumes (Internal LocalState + External USBs D:, E:, F:, G:)
    std::vector<BrokeredDriveInfo> EnumerateDrives();

    /// Register a folder token in the persistent FutureAccessList simulation/cache.
    bool RegisterFolderToken(std::string_view token, const std::filesystem::path& folder_path);

    /// Resolve a token from FutureAccessList back to an accessible filesystem path.
    [[nodiscard]] std::optional<std::filesystem::path> ResolveToken(std::string_view token) const;

    /// Check if a virtual path (e.g. "D:/games/zelda.nsp") can be streamed directly without staging.
    [[nodiscard]] bool CanDirectStream(std::string_view virtual_path) const noexcept;

    /// Resolves a virtual drive path to the physical host path, verifying that it resides on authorized storage.
    [[nodiscard]] std::optional<std::filesystem::path> ResolveBrokeredPath(std::string_view virtual_path) const;

    /// Returns persistent tokens registered in FutureAccessList.
    [[nodiscard]] std::vector<std::string> GetRegisteredTokens() const;

    /// Clear all registered FutureAccessList tokens.
    void ClearTokens();

private:
    mutable std::mutex broker_mutex_;
    std::unordered_map<std::string, std::filesystem::path> future_access_list_;
    std::vector<BrokeredDriveInfo> cached_drives_;
};

} // namespace nemu::platform
