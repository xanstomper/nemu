#include "nvdevice.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <vector>

namespace nemu::core::gpu::nvhost {

namespace {

// /dev/nvmap Device File
class NvMapDevice final : public NvDeviceFile {
public:
    explicit NvMapDevice(std::shared_ptr<NvMap> nvmap)
        : nvmap_(std::move(nvmap)) {}

    u32 Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) override {
        switch (cmd) {
            case 0xC0180101: { // NVMAP_IOC_CREATE
                struct CreateArgs {
                    u32 size;
                    u32 handle;
                };
                if (in_buf.size() < sizeof(CreateArgs)) return 0xF101;
                CreateArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.handle = nvmap_->Create(args.size);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC0200103: { // NVMAP_IOC_ALLOC
                struct AllocArgs {
                    u32 handle;
                    u32 heap_mask;
                    u32 flags;
                    u32 align;
                    u8  kind;
                    u8  pad[7];
                    u64 addr;
                };
                if (in_buf.size() < sizeof(AllocArgs)) return 0xF101;
                AllocArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                const bool ok = nvmap_->Alloc(args.handle, args.heap_mask, args.flags,
                                              args.align, args.kind, args.addr);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return ok ? 0 : 0xF101;
            }
            case 0xC0080104: { // NVMAP_IOC_FREE
                struct FreeArgs {
                    u32 handle;
                    u32 pad;
                    u64 flags;
                };
                if (in_buf.size() < 4) return 0xF101;
                u32 handle = 0;
                std::memcpy(&handle, in_buf.data(), sizeof(handle));
                const bool ok = nvmap_->Free(handle);
                FreeArgs out{handle, 0, 0};
                if (out_buf.size() >= sizeof(out)) {
                    std::memcpy(out_buf.data(), &out, sizeof(out));
                }
                return ok ? 0 : 0xF401;
            }
            case 0xC00C0105: { // NVMAP_IOC_PARAM
                struct ParamArgs {
                    u32 handle;
                    u32 param;
                    u32 result;
                };
                if (in_buf.size() < sizeof(ParamArgs)) return 0xF101;
                ParamArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                const bool ok = nvmap_->GetParam(args.handle, args.param, args.result);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return ok ? 0 : 0xF101;
            }
            case 0xC008010E: { // NVMAP_IOC_GET_ID
                struct GetIdArgs {
                    u32 id;
                    u32 handle;
                };
                if (in_buf.size() < sizeof(GetIdArgs)) return 0xF101;
                GetIdArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.id = nvmap_->GetId(args.handle);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            default:
                NEMU_LOG_WARN("NvMapDevice", "Unhandled ioctl cmd 0x{:X}", cmd);
                return 0;
        }
    }

private:
    std::shared_ptr<NvMap> nvmap_;
};

// /dev/nvhost-ctrl Device File
class NvHostCtrlDevice final : public NvDeviceFile {
public:
    explicit NvHostCtrlDevice(std::shared_ptr<SyncpointManager> syncpoints)
        : syncpoints_(std::move(syncpoints)) {}

    u32 Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) override {
        switch (cmd) {
            case 0xC0080014: { // NVHOST_IOCTL_CTRL_SYNCPT_READ
                struct SyncptReadArgs {
                    u32 id;
                    u32 value;
                };
                if (in_buf.size() < sizeof(SyncptReadArgs)) return 0xF101;
                SyncptReadArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.value = syncpoints_->Read(args.id);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0x40040015: { // NVHOST_IOCTL_CTRL_SYNCPT_INCR
                if (in_buf.size() < sizeof(u32)) return 0xF101;
                u32 id = 0;
                std::memcpy(&id, in_buf.data(), sizeof(id));
                syncpoints_->Increment(id);
                return 0;
            }
            case 0xC0100016: { // NVHOST_IOCTL_CTRL_SYNCPT_WAIT
                struct SyncptWaitArgs {
                    u32 id;
                    u32 thresh;
                    s32 timeout;
                    u32 value;
                };
                if (in_buf.size() < sizeof(SyncptWaitArgs)) return 0xF101;
                SyncptWaitArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                syncpoints_->Wait(args.id, args.thresh, args.timeout);
                args.value = syncpoints_->Read(args.id);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC010001D: { // NVHOST_IOCTL_CTRL_EVENT_WAIT
                struct EventWaitArgs {
                    u32 syncpt_id;
                    u32 thresh;
                    s32 timeout;
                    u32 value;
                };
                if (in_buf.size() < sizeof(EventWaitArgs)) return 0xF101;
                EventWaitArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                syncpoints_->Wait(args.syncpt_id, args.thresh, args.timeout);
                args.value = syncpoints_->Read(args.syncpt_id);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC004001E: { // NVHOST_IOCTL_CTRL_EVENT_REGISTER
                if (in_buf.size() < sizeof(u32)) return 0xF101;
                u32 event_id = 0;
                std::memcpy(&event_id, in_buf.data(), sizeof(event_id));
                syncpoints_->RegisterUserEvent(event_id, 0, 0);
                return 0;
            }
            default:
                NEMU_LOG_WARN("NvHostCtrlDevice", "Unhandled ioctl cmd 0x{:X}", cmd);
                return 0;
        }
    }

private:
    std::shared_ptr<SyncpointManager> syncpoints_;
};

// /dev/nvhost-as-gpu Device File
class NvHostAsGpuDevice final : public NvDeviceFile {
public:
    explicit NvHostAsGpuDevice(std::shared_ptr<AddressSpace> address_space)
        : address_space_(std::move(address_space)) {}

    u32 Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) override {
        switch (cmd) {
            case 0x40044101: { // NVGPU_AS_IOCTL_BIND_CHANNEL
                return 0;
            }
            case 0xC0184102: { // NVGPU_AS_IOCTL_ALLOC_SPACE
                struct AllocSpaceArgs {
                    u32 pages;
                    u32 page_size;
                    u32 flags;
                    u32 pad;
                    u64 offset;
                };
                if (in_buf.size() < sizeof(AllocSpaceArgs)) return 0xF101;
                AllocSpaceArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.offset = address_space_->AllocSpace(args.pages, args.page_size, args.flags, args.offset);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC0104103: { // NVGPU_AS_IOCTL_FREE_SPACE
                struct FreeSpaceArgs {
                    u64 offset;
                    u32 pages;
                    u32 page_size;
                };
                if (in_buf.size() < sizeof(FreeSpaceArgs)) return 0xF101;
                FreeSpaceArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                address_space_->FreeSpace(args.offset, args.pages, args.page_size);
                return 0;
            }
            case 0xC0284106: { // NVGPU_AS_IOCTL_MAP_BUFFER_EX
                struct MapBufferExArgs {
                    u32 flags;
                    u32 nvmap_handle;
                    u32 page_size;
                    u32 pad;
                    u64 offset;
                };
                if (in_buf.size() < sizeof(MapBufferExArgs)) return 0xF101;
                MapBufferExArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.offset = address_space_->MapBufferEx(args.nvmap_handle, args.flags, args.page_size, args.offset);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC0084105: { // NVGPU_AS_IOCTL_UNMAP_BUFFER
                struct UnmapBufferArgs {
                    u64 offset;
                };
                if (in_buf.size() < sizeof(UnmapBufferArgs)) return 0xF101;
                UnmapBufferArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                address_space_->UnmapBuffer(args.offset);
                return 0;
            }
            default:
                NEMU_LOG_WARN("NvHostAsGpuDevice", "Unhandled ioctl cmd 0x{:X}", cmd);
                return 0;
        }
    }

private:
    std::shared_ptr<AddressSpace> address_space_;
};

// /dev/nvhost-gpu Device File
class NvHostGpuDevice final : public NvDeviceFile {
public:
    NvHostGpuDevice(std::shared_ptr<Channel> channel, memory::VirtualMemory* memory)
        : channel_(std::move(channel)), memory_(memory) {}

    u32 Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) override {
        switch (cmd) {
            case 0x40044801: { // NVGPU_IOCTL_CHANNEL_SET_NVHOST_PID
                if (in_buf.size() < sizeof(u32)) return 0xF101;
                u32 pid = 0;
                std::memcpy(&pid, in_buf.data(), sizeof(pid));
                channel_->SetNvhostPid(pid);
                return 0;
            }
            case 0xC0104805: { // NVGPU_IOCTL_CHANNEL_ALLOC_GPFIFO
                struct AllocGpfifoArgs {
                    u32 num_entries;
                    u32 flags;
                    u64 fence_addr;
                };
                if (in_buf.size() < sizeof(AllocGpfifoArgs)) return 0xF101;
                AllocGpfifoArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.fence_addr = channel_->AllocGpfifo(args.num_entries, args.flags);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC0284808: { // NVGPU_IOCTL_CHANNEL_SUBMIT_GPFIFO
                struct SubmitGpfifoArgs {
                    u64 gpfifo_ptr;
                    u32 num_entries;
                    u32 flags;
                    u32 fence_id;
                    u32 fence_value;
                };
                if (in_buf.size() < sizeof(SubmitGpfifoArgs)) return 0xF101;
                SubmitGpfifoArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));

                if (args.num_entries > 0 && args.gpfifo_ptr != 0 && memory_) {
                    std::vector<u64> entries(args.num_entries);
                    if (memory_->ReadBlock(args.gpfifo_ptr, entries.data(), args.num_entries * sizeof(u64))) {
                        auto [f_id, f_val] = channel_->SubmitGpfifo(entries, args.flags);
                        args.fence_id = f_id;
                        args.fence_value = f_val;
                    }
                }

                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0xC0104809: { // NVGPU_IOCTL_CHANNEL_ALLOC_OBJ_CTX
                struct AllocObjCtxArgs {
                    u32 class_num;
                    u32 flags;
                    u64 obj_id;
                };
                if (in_buf.size() < sizeof(AllocObjCtxArgs)) return 0xF101;
                AllocObjCtxArgs args{};
                std::memcpy(&args, in_buf.data(), sizeof(args));
                args.obj_id = channel_->AllocObjCtx(args.class_num, args.flags);
                if (out_buf.size() >= sizeof(args)) {
                    std::memcpy(out_buf.data(), &args, sizeof(args));
                }
                return 0;
            }
            case 0x4004480D: { // NVGPU_IOCTL_CHANNEL_SET_PRIORITY
                if (in_buf.size() < sizeof(u32)) return 0xF101;
                u32 prio = 0;
                std::memcpy(&prio, in_buf.data(), sizeof(prio));
                channel_->SetPriority(prio);
                return 0;
            }
            case 0xC010480B: // ZCULL_BIND
            case 0xC018480C: // SET_ERROR_NOTIFIER
                return 0;
            default:
                NEMU_LOG_WARN("NvHostGpuDevice", "Unhandled ioctl cmd 0x{:X}", cmd);
                return 0;
        }
    }

private:
    std::shared_ptr<Channel> channel_;
    memory::VirtualMemory* memory_{nullptr};
};

} // anonymous namespace

NvDeviceManager::NvDeviceManager(std::shared_ptr<Maxwell3D> maxwell_3d, memory::VirtualMemory* memory)
    : maxwell_3d_(std::move(maxwell_3d))
    , memory_(memory)
    , nvmap_(std::make_shared<NvMap>())
    , syncpoints_(std::make_shared<SyncpointManager>())
    , address_space_(std::make_shared<AddressSpace>(nvmap_))
    , channel_(std::make_shared<Channel>(0, syncpoints_, address_space_, maxwell_3d_, memory_)) {}

s32 NvDeviceManager::Open(std::string_view path) {
    std::unique_lock lock(mutex_);
    std::shared_ptr<NvDeviceFile> file;

    if (path == "/dev/nvmap") {
        file = std::make_shared<NvMapDevice>(nvmap_);
    } else if (path == "/dev/nvhost-ctrl") {
        file = std::make_shared<NvHostCtrlDevice>(syncpoints_);
    } else if (path == "/dev/nvhost-as-gpu") {
        file = std::make_shared<NvHostAsGpuDevice>(address_space_);
    } else if (path == "/dev/nvhost-gpu" || path == "/dev/nvhost-ctrl-gpu") {
        file = std::make_shared<NvHostGpuDevice>(channel_, memory_);
    } else {
        NEMU_LOG_WARN("NvDeviceManager", "Open: unknown device '{}'", path);
        return -1;
    }

    const s32 fd = next_fd_++;
    files_[fd] = std::move(file);
    NEMU_LOG_DEBUG("NvDeviceManager", "Opened '{}' -> fd={}", path, fd);
    return fd;
}

u32 NvDeviceManager::Ioctl(s32 fd, u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) {
    std::shared_ptr<NvDeviceFile> file;
    {
        std::unique_lock lock(mutex_);
        auto it = files_.find(fd);
        if (it == files_.end()) {
            NEMU_LOG_WARN("NvDeviceManager", "Ioctl: invalid fd {}", fd);
            return 0xF101;
        }
        file = it->second;
    }
    return file->Ioctl(cmd, in_buf, out_buf);
}

bool NvDeviceManager::Close(s32 fd) {
    std::unique_lock lock(mutex_);
    auto it = files_.find(fd);
    if (it == files_.end()) return false;
    it->second->Close();
    files_.erase(it);
    NEMU_LOG_DEBUG("NvDeviceManager", "Closed fd {}", fd);
    return true;
}

} // namespace nemu::core::gpu::nvhost
