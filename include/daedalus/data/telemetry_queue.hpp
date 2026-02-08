#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace daedalus::data {

/// Single-producer single-consumer lock-free ring buffer.
/// Producer: network thread (IXWebSocket callbacks).
/// Consumer: render thread (ImGui frame loop).
/// No mutexes — pure atomic acquire/release.
template <typename T> class SPSCQueue {
  public:
    explicit SPSCQueue(size_t capacity) : capacity_(capacity), buffer_(capacity) {}

    /// Push an item (producer only). Returns false if full.
    bool try_push(T &&item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) % capacity_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    /// Pop an item (consumer only). Returns false if empty.
    bool try_pop(T &item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        item = std::move(buffer_[head]);
        head_.store((head + 1) % capacity_, std::memory_order_release);
        return true;
    }

    /// Approximate number of items (not exact under concurrency).
    [[nodiscard]] size_t size_approx() const {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_relaxed);
        return (tail + capacity_ - head) % capacity_;
    }

    /// Usable capacity (one slot reserved for full/empty distinction).
    [[nodiscard]] size_t capacity() const { return capacity_ - 1; }

  private:
    size_t capacity_;
    std::vector<T> buffer_;

    // Separate cache lines to avoid false sharing
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

/// Binary telemetry frame queue (network → render thread).
using TelemetryQueue = SPSCQueue<std::vector<uint8_t>>;

/// JSON event string queue (network → render thread).
using EventQueue = SPSCQueue<std::string>;

} // namespace daedalus::data
