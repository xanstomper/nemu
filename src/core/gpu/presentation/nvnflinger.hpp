#pragma once

#include "core/types.hpp"
#include "buffer_queue.hpp"
#include "core/gpu/gpu_interface.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

namespace nemu::core::gpu::presentation {

struct Layer {
    u64 layer_id{0};
    u64 display_id{0};
    std::shared_ptr<BufferQueue> buffer_queue;
    bool is_visible{true};
    u32 z_order{0};
};

class Nvnflinger {
public:
    explicit Nvnflinger(std::shared_ptr<IGpuBackend> backend);
    ~Nvnflinger() = default;

    Nvnflinger(const Nvnflinger&) = delete;
    Nvnflinger& operator=(const Nvnflinger&) = delete;

    /// Open or find a display ("Default", "External"). Returns display ID.
    u64 OpenDisplay(std::string_view name);

    /// Close display.
    bool CloseDisplay(u64 display_id);

    /// Create a layer on a display. Returns layer ID.
    u64 CreateLayer(u64 display_id);

    /// Destroy a layer.
    bool DestroyLayer(u64 layer_id);

    /// Retrieve the BufferQueue for a layer.
    [[nodiscard]] std::shared_ptr<BufferQueue> GetBufferQueue(u64 layer_id) const;

    /// Compose queued buffers across layers and present to the graphics swapchain.
    /// Returns true if at least one frame was presented.
    bool ComposeAndPresent();

    [[nodiscard]] u64 GetTotalFramesPresented() const noexcept { return total_frames_presented_; }
    [[nodiscard]] std::shared_ptr<IGpuBackend> GetBackend() const noexcept { return backend_; }

private:
    std::shared_ptr<IGpuBackend> backend_;
    mutable std::mutex mutex_;
    u64 next_display_id_{1};
    u64 next_layer_id_{1};
    u64 total_frames_presented_{0};

    std::unordered_map<std::string, u64> display_names_;
    std::unordered_map<u64, std::string> displays_;
    std::unordered_map<u64, Layer> layers_;
};

} // namespace nemu::core::gpu::presentation
