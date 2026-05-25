#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

namespace galahad
{
template <typename T, size_t Capacity>
class SpscQueue
{
public:
    static_assert(Capacity > 1, "SPSC queue capacity must be greater than one.");
    static_assert(std::is_trivially_copyable_v<T>, "SPSC queue items must be trivially copyable.");

    bool tryPush(const T& item) noexcept
    {
        const auto write = writeIndex_.value.load(std::memory_order_relaxed);
        const auto next = increment(write);

        if (next == readIndex_.value.load(std::memory_order_acquire))
        {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        buffer_[write] = item;
        writeIndex_.value.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(T& item) noexcept
    {
        const auto read = readIndex_.value.load(std::memory_order_relaxed);

        if (read == writeIndex_.value.load(std::memory_order_acquire))
            return false;

        item = buffer_[read];
        readIndex_.value.store(increment(read), std::memory_order_release);
        return true;
    }

    size_t droppedCount() const noexcept
    {
        return droppedCount_.load(std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        readIndex_.value.store(0, std::memory_order_relaxed);
        writeIndex_.value.store(0, std::memory_order_relaxed);
        droppedCount_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr size_t CacheLineSize = std::hardware_destructive_interference_size;

    struct alignas(CacheLineSize) CacheSeparatedIndex
    {
        std::atomic<size_t> value{ 0 };
    };

    static constexpr size_t increment(size_t index) noexcept
    {
        return (index + 1) % Capacity;
    }

    alignas(CacheLineSize) std::array<T, Capacity> buffer_{};
    CacheSeparatedIndex readIndex_{};
    CacheSeparatedIndex writeIndex_{};
    alignas(CacheLineSize) std::atomic<size_t> droppedCount_{ 0 };
};
} // namespace galahad
