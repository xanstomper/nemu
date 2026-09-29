#include "nvnflinger.hpp"
#include "core/gpu/pipeline/graphics_optimizer.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cstring>

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

void Nvnflinger::SetTargetResolution(u32 width, u32 height) {
    std::unique_lock lock(mutex_);
    target_width_ = std::max(1u, width);
    target_height_ = std::max(1u, height);
    composite_buffer_.assign(static_cast<size_t>(target_width_) * target_height_, 0);
    NEMU_LOG_INFO("Nvnflinger", "Target presentation resolution set to {}x{}", target_width_, target_height_);
}

[[nodiscard]] std::pair<u32, u32> Nvnflinger::GetTargetResolution() const noexcept {
    std::unique_lock lock(mutex_);
    return {target_width_, target_height_};
}

void Nvnflinger::SetLayerTransform(u64 layer_id, u32 crop_x, u32 crop_y, u32 crop_w, u32 crop_h, u32 target_w, u32 target_h) {
    std::unique_lock lock(mutex_);
    auto it = layers_.find(layer_id);
    if (it != layers_.end()) {
        it->second.crop_x = crop_x;
        it->second.crop_y = crop_y;
        it->second.crop_w = crop_w;
        it->second.crop_h = crop_h;
        it->second.target_w = target_w;
        it->second.target_h = target_h;
    }
}

void Nvnflinger::SetLayerOpacity(u64 layer_id, float opacity) {
    std::unique_lock lock(mutex_);
    auto it = layers_.find(layer_id);
    if (it != layers_.end()) {
        it->second.opacity = std::clamp(opacity, 0.0f, 1.0f);
    }
}

bool Nvnflinger::ComposeAndPresent() {
    std::vector<std::pair<Layer, std::shared_ptr<BufferQueue>>> layers_to_compose;
    u32 target_w = 1280;
    u32 target_h = 720;
    pipeline::UpscalerMode scale_mode = pipeline::UpscalerMode::Bilinear;

    {
        std::unique_lock lock(mutex_);
        target_w = target_width_;
        target_h = target_height_;
        scale_mode = scaling_mode_;

        for (auto& [id, layer] : layers_) {
            if (layer.is_visible && layer.buffer_queue && layer.buffer_queue->GetQueuedCount() > 0) {
                layers_to_compose.emplace_back(layer, layer.buffer_queue);
            }
        }
    }

    if (layers_to_compose.empty()) return false;

    // Ensure composite output buffer is sized for target resolution
    const size_t target_pixels = static_cast<size_t>(target_w) * target_h;
    {
        std::unique_lock lock(mutex_);
        if (composite_buffer_.size() != target_pixels) {
            composite_buffer_.assign(target_pixels, 0);
        }
    }

    bool presented = false;
    for (auto& [layer, bq] : layers_to_compose) {
        auto slot_opt = bq->AcquireBuffer();
        if (!slot_opt.has_value()) continue;
        const s32 slot = *slot_opt;

        auto slot_meta = bq->GetSlot(slot);
        if (slot_meta.has_value() && !slot_meta->pixel_data.empty() && slot_meta->width > 0 && slot_meta->height > 0) {
            const u32 src_w = slot_meta->width;
            const u32 src_h = slot_meta->height;
            const auto* src_pixels = reinterpret_cast<const u32*>(slot_meta->pixel_data.data());
            const size_t src_count = slot_meta->pixel_data.size() / sizeof(u32);

            std::unique_lock lock(mutex_);
            if (src_w == target_w && src_h == target_h && src_count >= target_pixels) {
                // Direct copy / blend
                std::memcpy(composite_buffer_.data(), src_pixels, target_pixels * sizeof(u32));
            } else if (src_count >= static_cast<size_t>(src_w) * src_h) {
                // Apply VIC scaling pass (Bilinear, Bicubic, or FSR)
                pipeline::GraphicsOptimizer::ApplyUpscale(
                    std::span<const u32>(src_pixels, src_w * src_h),
                    src_w, src_h,
                    std::span<u32>(composite_buffer_.data(), target_pixels),
                    target_w, target_h,
                    scale_mode
                );
            }
        }

        if (backend_) {
            backend_->BeginFrame();
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
