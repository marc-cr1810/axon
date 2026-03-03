/**
 * @file atomic_queue.hpp
 * @brief Lock-free SPSC and MPMC ring queues.
 *
 * SpscQueue<T, N>  — single-producer single-consumer, lowest latency.
 * MpmcQueue<T, N>  — multi-producer multi-consumer (Dmitry Vyukov's design).
 *
 * N must be a power of 2.  Both queues store values by copy; for zero-copy
 * use with uint32_t/uint64_t offset handles into shared memory.
 *
 * All state lives in shared memory — only std::atomic, no OS primitives.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <axon/platform/cpu.hpp>

namespace axon
{

// ── Helpers ───────────────────────────────────────────────────────────────────
namespace detail
{
template <std::size_t N> constexpr bool is_power_of_two = (N > 0) && ((N & (N - 1)) == 0);
} // namespace detail

// ── SPSC Ring Queue ───────────────────────────────────────────────────────────
/**
 * Single-producer, single-consumer bounded ring queue.
 *
 * The head (write) and tail (read) indices are kept on separate cache lines
 * so the producer and consumer threads never false-share.
 *
 * Capacity is compile-time; the struct can be placed directly in shared memory.
 */
template <typename T, std::size_t N> struct SpscQueue
{
  static_assert(detail::is_power_of_two<N>, "SpscQueue capacity must be a power of 2");
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

  static constexpr std::size_t kCapacity = N;
  static constexpr std::size_t kMask = N - 1;

  // ── Producer API ─────────────────────────────────────────────────────────
  /// Try to push a value.  Returns false if the queue is full.
  [[nodiscard]] bool push(const T &value) noexcept
  {
    const std::size_t h = head_.load(std::memory_order_relaxed);
    if (AXON_UNLIKELY(h - tail_.load(std::memory_order_acquire) >= kCapacity))
      return false;
    slots_[h & kMask] = value;
    head_.store(h + 1, std::memory_order_release);
    return true;
  }

  // ── Consumer API ─────────────────────────────────────────────────────────
  /// Try to pop a value.  Returns std::nullopt if the queue is empty.
  [[nodiscard]] std::optional<T> pop() noexcept
  {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    if (AXON_UNLIKELY(head_.load(std::memory_order_acquire) == t))
      return std::nullopt;
    T value = slots_[t & kMask];
    tail_.store(t + 1, std::memory_order_release);
    return value;
  }

  [[nodiscard]] bool empty() const noexcept
  {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t size() const noexcept
  {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

public:
  alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
  T slots_[N]{};
};

// ── MPMC Ring Queue ───────────────────────────────────────────────────────────
/**
 * Multi-producer, multi-consumer bounded ring queue.
 *
 * Based on Dmitry Vyukov's sequence-number ring buffer.
 * Each slot carries a sequence counter that encodes its state:
 *   seq == pos       → slot is empty, producer may claim it
 *   seq == pos + 1   → slot is full, consumer may claim it
 *   seq == pos + N   → slot has been consumed, producer recycles
 */
template <typename T, std::size_t N> struct MpmcQueue
{
  static_assert(detail::is_power_of_two<N>, "MpmcQueue capacity must be a power of 2");
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

  static constexpr std::size_t kCapacity = N;
  static constexpr std::size_t kMask = N - 1;

  MpmcQueue() noexcept
  {
    for (std::size_t i = 0; i < N; ++i)
      slots_[i].seq.store(i, std::memory_order_relaxed);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  // ── Producer API ─────────────────────────────────────────────────────────
  [[nodiscard]] bool push(const T &value) noexcept
  {
    Slot *slot;
    std::size_t pos = head_.load(std::memory_order_relaxed);
    for (;;)
    {
      slot = &slots_[pos & kMask];
      std::size_t seq = slot->seq.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
      if (diff == 0)
      {
        if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
          break;
      }
      else if (diff < 0)
      {
        return false; // full
      }
      else
      {
        pos = head_.load(std::memory_order_relaxed);
      }
    }
    slot->data = value;
    slot->seq.store(pos + 1, std::memory_order_release);
    return true;
  }

  // ── Consumer API ─────────────────────────────────────────────────────────
  [[nodiscard]] std::optional<T> pop() noexcept
  {
    Slot *slot;
    std::size_t pos = tail_.load(std::memory_order_relaxed);
    for (;;)
    {
      slot = &slots_[pos & kMask];
      std::size_t seq = slot->seq.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
      if (diff == 0)
      {
        if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
          break;
      }
      else if (diff < 0)
      {
        return std::nullopt; // empty
      }
      else
      {
        pos = tail_.load(std::memory_order_relaxed);
      }
    }
    T value = slot->data;
    slot->seq.store(pos + kMask + 1, std::memory_order_release);
    return value;
  }

  [[nodiscard]] bool empty() const noexcept
  {
    std::size_t h = head_.load(std::memory_order_acquire);
    std::size_t t = tail_.load(std::memory_order_acquire);
    return h == t;
  }

private:
  struct alignas(kCacheLineSize) Slot
  {
    std::atomic<std::size_t> seq{};
    T data{};
  };

  alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
  Slot slots_[N]{};
};

} // namespace axon
