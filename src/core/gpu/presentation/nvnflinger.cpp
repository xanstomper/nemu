#include "nvnflinger.hpp"
#include "platform/logger.hpp"

namespace nemu::core::gpu::presentation {

Nvnflinger::Nvnflinger(std::shared_ptr<IGpuBackend> backend)
    : backend_(std::move(backend)) {
    OpenDisplay("Default");
}

u64 Nvnflinger::OpenDisplay(std::string_view name) {
    std::unique_lock lock(mutex_);
    const std::string s_name(name);
    auto it = display_names_.find(s_name);
    if (it != display_names_.end()) {
        return it->second;
    }
    const u64 id = next_display_id_++;
    display_names_[s_name] = id;
    displays_[id] = s_name;
    NEMU_LOG_DEBUG("Nvnflinger", "Opened display '{}' -> id={}", s_name, id);
    return id;
}

bool Nvnflinger::CloseDisplay(u64 display_id) {
    std::unique_lock lock(mutex_);
    auto it = displays_.find(display_id);
    if (it == displays_.end()) return false;
    display_names_.erase(it->second);
    displays_.erase(it);
    return true;
}

u64 Nvnflinger::CreateLayer(u64 display_id) {
    std::unique_lock lock(mutex_);
    const u64 layer_id = next_layer_id_++;
    Layer layer{};
    layer.layer_id = layer_id;
    layer.display_id = display_id;
    layer.buffer_queue = std::make_shared<BufferQueue>();
    layer.is_visible = true;
    layers_[layer_id] = std::move(layer);
    NEMU_LOG_DEBUG("Nvnflinger", "Created layer {} on display {}", layer_id, display_id);
    return layer_id;
}

bool Nvnflinger::DestroyLayer(u64 layer_id) {
    std::unique_lock lock(mutex_);
    auto it = layers_.find(layer_id);
    if (it == layers_.end()) return false;
    layers_.erase(it);
    NEMU_LOG_DEBUG("Nvnflinger", "Destroyed layer {}", layer_id);
    return true;
}

std::shared_ptr<BufferQueue> Nvnflinger::GetBufferQueue(u64 layer_id) const {
    std::unique_lock lock(mutex_);
    auto it = layers_.find(layer_id);
    return (it != layers_.end()) ? it->second.buffer_queue : nullptr;
}

bool Nvnflinger::ComposeAndPresent() {
    std::vector<std::shared_ptr<BufferQueue>> queues_to_present;
    {
        std::unique_lock lock(mutex_);
        for (auto& [id, layer] : layers_) {
            if (layer.is_visible && layer.buffer_queue && layer.buffer_queue->GetQueuedCount() > 0) {
                queues_to_present.push_back(layer.buffer_queue);
            }
        }
    }

    if (queues_to_present.empty()) return false;

    bool presented = false;
    for (auto& bq : queues_to_present) {
        auto slot_opt = bq->AcquireBuffer();
        if (!slot_opt.has_value()) continue;
        const s32 slot = *slot_opt;

        if (backend_) {
            backend_->BeginFrame();
            // Clear or render layer frame
            backend_->EndFrame();
            backend_->Present();
            presented = true;
        }

        bq->ReleaseBuffer(slot);
    }

    if (presented) {
        std::unique_lock lock(mutex_);
        ++total_frames_presented_;
    }

    return presented;
}

} // namespace nemu::core::gpu::presentation
