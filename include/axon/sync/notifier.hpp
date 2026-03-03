/**
 * @file notifier.hpp
 * @brief Linux eventfd-based cross-process notification.
 *
 * Wraps a Linux eventfd in semaphore mode (EFD_SEMAPHORE).
 * The fd is created in the publisher and can be inherited across fork(),
 * or the fd number can be passed through the shared registry so any
 * process in the session can open it via /proc/<pid>/fd/<fd>.
 *
 * For the simple single-session case (same creator pid) just inherit the fd.
 */
#pragma once

#include <cstdint>
#include <cerrno>
#include <semaphore.h>
#include <time.h>
#include <chrono>

namespace axon
{

class Notifier
{
public:
  Notifier() noexcept = default;

  /// Adopt an already-initialized semaphore in shared memory.
  explicit Notifier(sem_t *sem) noexcept : sem_{sem}
  {
  }

  ~Notifier() = default;
  Notifier(Notifier &&) = default;
  Notifier &operator=(Notifier &&) = default;
  Notifier(const Notifier &) = default;
  Notifier &operator=(const Notifier &) = default;

  /// Post one wakeup token. Non-blocking.
  bool notify() noexcept
  {
    if (!sem_)
      return false;
    return ::sem_post(sem_) == 0;
  }

  /// Consume one wakeup token without blocking. Returns true if a token was available.
  [[nodiscard]] bool try_consume() noexcept
  {
    if (!sem_)
      return false;
    return ::sem_trywait(sem_) == 0;
  }

  /// Block until a wakeup token is available (no timeout).
  void wait() noexcept
  {
    if (!sem_)
      return;
    int r;
    do
    {
      r = ::sem_wait(sem_);
    } while (r == -1 && errno == EINTR);
  }

  /// Block until a wakeup token arrives or the timeout expires.
  /// Returns true if a token was consumed.
  [[nodiscard]] bool wait_for(std::chrono::milliseconds timeout) noexcept
  {
    if (!sem_)
      return false;

    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);

    auto s = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - s);

    ts.tv_sec += s.count();
    ts.tv_nsec += ns.count();
    if (ts.tv_nsec >= 1000000000)
    {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000;
    }

    int r;
    do
    {
      r = ::sem_timedwait(sem_, &ts);
    } while (r == -1 && errno == EINTR);

    return r == 0;
  }

  [[nodiscard]] bool valid() const noexcept
  {
    return sem_ != nullptr;
  }

private:
  sem_t *sem_{nullptr};
};

} // namespace axon
