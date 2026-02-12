#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace daedalus::data {

/// Per-signal rolling history ring buffer.
/// Stores paired (time, value) samples for plotting with ImPlot.
/// Thread safety: render thread only — no synchronization.
class SignalBuffer {
  public:
    static constexpr size_t kDefaultCapacity = 18000; // 5 min at 60 Hz

    explicit SignalBuffer(size_t capacity = kDefaultCapacity)
        : capacity_(std::max<size_t>(capacity, 1)), times_(capacity_), values_(capacity_) {}

    void push(double time, double value) {
        times_[write_pos_] = time;
        values_[write_pos_] = value;
        write_pos_ = (write_pos_ + 1) % capacity_;
        if (count_ < capacity_) {
            ++count_;
        }
    }

    [[nodiscard]] size_t size() const { return count_; }
    [[nodiscard]] size_t capacity() const { return capacity_; }
    [[nodiscard]] bool full() const { return count_ == capacity_; }
    [[nodiscard]] bool empty() const { return count_ == 0; }

    void clear() {
        write_pos_ = 0;
        count_ = 0;
    }

    /// Access sample by logical index (0 = oldest).
    [[nodiscard]] double time_at(size_t i) const { return times_[physical_index(i)]; }

    [[nodiscard]] double value_at(size_t i) const { return values_[physical_index(i)]; }

    /// Copy data into contiguous staging buffers for ImPlot.
    /// Caller provides pre-allocated vectors of at least size() elements.
    void copy_to(std::vector<double> &out_times, std::vector<double> &out_values) const {
        const size_t n = count_;
        out_times.resize(n);
        out_values.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const size_t idx = physical_index(i);
            out_times[i] = times_[idx];
            out_values[i] = values_[idx];
        }
    }

    /// Last pushed value (most recent). UB if empty.
    [[nodiscard]] double last_value() const {
        const size_t idx = (write_pos_ + capacity_ - 1) % capacity_;
        return values_[idx];
    }

    [[nodiscard]] double last_time() const {
        const size_t idx = (write_pos_ + capacity_ - 1) % capacity_;
        return times_[idx];
    }

  private:
    [[nodiscard]] size_t physical_index(size_t logical) const {
        if (count_ < capacity_) {
            return logical;
        }
        return (write_pos_ + logical) % capacity_;
    }

    size_t capacity_;
    size_t write_pos_ = 0;
    size_t count_ = 0;
    std::vector<double> times_;
    std::vector<double> values_;
};

} // namespace daedalus::data
