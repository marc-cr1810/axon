/**
 * @file spinlock.hpp
 * @brief Cache-line padded spinlock with exponential backoff.
 *
 * Uses std::atomic<bool> with a pause/yield hint per architecture.
 * Padded to a full cache line to prevent false sharing with adjacent data.
 */
#pragma once

#include <atomic>
#include <axon/platform/cpu.hpp>

namespace axon
{

class Spinlock
{
public:
  Spinlock() noexcept : locked_{false}
  {
  }
  Spinlock(const Spinlock &) = delete;
  Spinlock &operator=(const Spinlock &) = delete;

  void lock() noexcept
  {
    for (unsigned spin = 0;;)
    {
      // Optimistic fast path — avoid CAS if the lock looks free
      if (!locked_.load(std::memory_order_relaxed) && !locked_.exchange(true, std::memory_order_acquire))
        return;

      // Exponential backoff — reduces bus traffic during contention
      ++spin;
      for (unsigned i = 0; i < (1u << (spin < 8 ? spin : 8)); ++i)
        cpu_pause();
    }
  }

  [[nodiscard]] bool try_lock() noexcept
  {
    return !locked_.load(std::memory_order_relaxed) && !locked_.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept
  {
    locked_.store(false, std::memory_order_release);
  }

private:
  alignas(kCacheLineSize) std::atomic<bool> locked_;
  [[maybe_unused]] char _pad[kCacheLineSize - sizeof(std::atomic<bool>)]{};
};

/// RAII guard for Spinlock
class SpinlockGuard
{
public:
  explicit SpinlockGuard(Spinlock &lk) noexcept : lk_{lk}
  {
    lk_.lock();
  }
  ~SpinlockGuard() noexcept
  {
    lk_.unlock();
  }
  SpinlockGuard(const SpinlockGuard &) = delete;
  SpinlockGuard &operator=(const SpinlockGuard &) = delete;

private:
  Spinlock &lk_;
};

} // namespace axon
