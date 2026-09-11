#pragma once

// Clean-room reimplementation of a fixed-capacity thread-safe (multi-producer /
// multi-consumer) queue. This is the general producer-consumer building block
// used throughout Switch emulators (Eden/yuzu's bounded-threadsafe queue, audio
// and IPC worker hand-off). Written fresh in Nemu's style; no source text copied.

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace nemu::core::common {

/// A fixed-capacity (bounded) FIFO safe for use from multiple threads. Enqueue
/// and dequeue can both block (when full / empty) or be used as "try" variants
/// that never block. The backing is a std::deque growing up to `capacity`;
/// producers wait when it is full, consumers wait when it is empty.
template <typename T>
class BoundedThreadsafeQueue {
public:
    explicit BoundedThreadsafeQueue(std::size_t capacity = 128) : capacity_(capacity) {}

    BoundedThreadsafeQueue(const BoundedThreadsafeQueue&) = delete;
    BoundedThreadsafeQueue& operator=(const BoundedThreadsafeQueue&) = delete;

    /// Blocking push; waits while the queue is at capacity.
    void Emplace(T&& value) {
        {
            std::unique_lock lock(mutex_);
            cv_not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
            queue_.emplace(std::forward<T>(value));
        }
        cv_not_empty_.notify_one();
    }

    void Push(const T& value) { Emplace(T(value)); }

    /// Non-blocking push; returns false if the queue is full.
    bool TryEmplace(T&& value) {
        {
            std::lock_guard lock(mutex_);
            if (queue_.size() >= capacity_) {
                return false;
            }
            queue_.emplace(std::forward<T>(value));
        }
        cv_not_empty_.notify_one();
        return true;
    }

    bool TryPush(const T& value) { return TryEmplace(T(value)); }

    /// Blocking pop; waits while the queue is empty.
    T Pop() {
        T value;
        {
            std::unique_lock lock(mutex_);
            cv_not_empty_.wait(lock, [this] { return !queue_.empty(); });
            value = std::move(queue_.front());
            queue_.pop();
        }
        cv_not_full_.notify_one();
        return value;
    }

    /// Non-blocking pop; returns nullopt if the queue is empty.
    std::optional<T> TryPop() {
        std::optional<T> result;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) {
                return std::nullopt;
            }
            result.emplace(std::move(queue_.front()));
            queue_.pop();
        }
        cv_not_full_.notify_one();
        return result;
    }

    /// Clear all pending items (not thread-safe against concurrent push/pop;
    /// intended for shutdown ordering).
    void Clear() {
        std::lock_guard lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

    [[nodiscard]] std::size_t Size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] bool Empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_not_full_;
    mutable std::condition_variable cv_not_empty_;
    std::queue<T> queue_;
};

} // namespace nemu::core::common