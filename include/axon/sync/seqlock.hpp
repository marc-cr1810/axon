/**
 * @file seqlock.hpp
 * @brief Seqlock for single-writer, multi-reader shared state with zero reader blocking.
 *
 * Writers increment the sequence counter before and after writing.
 * Readers retry if they observe an odd counter (write in progress) or if
 * the counter changed during their read (torn read).
 *
 * All state lives in shared memory — only atomics, no OS primitives.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace axon
{

struct Seqlock
{
  std::atomic<uint32_t> seq{0};

  /// Call before writing state.
  void write_begin() noexcept
  {
    seq.fetch_add(1, std::memory_order_release);
  }

  /// Call after writing state.
  void write_end() noexcept
  {
    seq.fetch_add(1, std::memory_order_release);
  }

  /// Returns the current sequence snapshot; spins if a write is in progress.
  [[nodiscard]] uint32_t read_begin() const noexcept
  {
    uint32_t s;
    do
    {
      s = seq.load(std::memory_order_acquire);
    } while (s & 1u);
    return s;
  }

  /// Returns true if the sequence is still the same as the snapshot — read is consistent.
  [[nodiscard]] bool read_end(uint32_t snapshot) const noexcept
  {
    std::atomic_thread_fence(std::memory_order_acquire);
    return seq.load(std::memory_order_relaxed) == snapshot;
  }
};

/// Read `T` from `src` protected by a seqlock. Retries until a consistent copy is obtained.
template <typename T> [[nodiscard]] T seqlock_read(const Seqlock &lock, const T *src) noexcept
{
  T snapshot;
  uint32_t seq;
  do
  {
    seq = lock.read_begin();
    std::memcpy(&snapshot, src, sizeof(T));
  } while (!lock.read_end(seq));
  return snapshot;
}

} // namespace axon
