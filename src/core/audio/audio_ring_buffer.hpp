#pragma once

#include "core/types.hpp"
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstring>

namespace nemu::core::audio {

template <typename T>
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity)
        : capacity_(RoundUpPowerOfTwo(capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {
    }

    ~AudioRingBuffer() = default;

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    size_t Push(const T* data, size_t count) {
        if (!data || count == 0) return 0;

        const size_t write_idx = write_index_.load(std::memory_order_relaxed);
        const size_t read_idx = read_index_.load(std::memory_order_acquire);
        const size_t available_space = capacity_ - (write_idx - read_idx);

        const size_t to_write = std::min(count, available_space);
        if (to_write == 0) return 0;

        for (size_t i = 0; i < to_write; ++i) {
            buffer_[(write_idx + i) & mask_] = data[i];
        }

        write_index_.store(write_idx + to_write, std::memory_order_release);
        return to_write;
    }

    size_t Pop(T* out_data, size_t count) {
        if (!out_data || count == 0) return 0;

        const size_t read_idx = read_index_.load(std::memory_order_relaxed);
        const size_t write_idx = write_index_.load(std::memory_order_acquire);
        const size_t available_data = write_idx - read_idx;

        const size_t to_read = std::min(count, available_data);
        if (to_read == 0) return 0;

        for (size_t i = 0; i < to_read; ++i) {
            out_data[i] = buffer_[(read_idx + i) & mask_];
        }

        read_index_.store(read_idx + to_read, std::memory_order_release);
        return to_read;
    }

    [[nodiscard]] size_t GetAvailableRead() const noexcept {
        const size_t write_idx = write_index_.load(std::memory_order_acquire);
        const size_t read_idx = read_index_.load(std::memory_order_acquire);
        return write_idx - read_idx;
    }

    [[nodiscard]] size_t GetAvailableWrite() const noexcept {
        return capacity_ - GetAvailableRead();
    }

    [[nodiscard]] size_t GetCapacity() const noexcept {
        return capacity_;
    }

    void Clear() noexcept {
        const size_t write_idx = write_index_.load(std::memory_order_acquire);
        read_index_.store(write_idx, std::memory_order_release);
    }

private:
    static size_t RoundUpPowerOfTwo(size_t val) {
        size_t p = 1;
        while (p < val) {
            p <<= 1;
        }
        return p;
    }

    const size_t capacity_;
    const size_t mask_;
    std::vector<T> buffer_;

    alignas(64) std::atomic<size_t> write_index_{0};
    alignas(64) std::atomic<size_t> read_index_{0};
};

} // namespace nemu::core::audio
