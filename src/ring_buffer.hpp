#pragma once

#include <array>
#include <cstddef>

namespace balder {

/// Fixed-capacity FIFO with no heap. When full, push() overwrites the oldest
/// item.
template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N > 0);

public:
    /// Returns false if the oldest item was overwritten to make room.
    [[nodiscard]] bool push(const T& item) {
        items_[head_ + count_] = item;
        if (count_ == N) {
            head_ = (head_ + 1) % N;
            return false;
        }
        ++count_;
        return true;
    }

    /// front() and pop() require !empty().
    [[nodiscard]] const T& front() const { return items_[head_]; }
    void pop() {
        head_ = (head_ + 1) % N;
        --count_;
    }

    [[nodiscard]] std::size_t size() const { return count_; }
    [[nodiscard]] bool empty() const { return count_ == 0; }

private:
    std::array<T, N> items_{};
    std::size_t head_{0};
    std::size_t count_{0};
};

} // namespace balder
