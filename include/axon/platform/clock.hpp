/**
 * @file clock.hpp
 * @brief High-resolution monotonic clock helpers using CLOCK_MONOTONIC_RAW.
 */
#pragma once

#include <cstdint>
#include <time.h>

namespace axon
{

/// Returns the current time in nanoseconds from CLOCK_MONOTONIC_RAW.
/// This clock is not affected by NTP adjustments and is ideal for latency measurement.
[[nodiscard]] inline int64_t now_ns() noexcept
{
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

/// Returns the current time in nanoseconds from CLOCK_REALTIME (wall clock).
/// Used for semaphore timeouts which require CLOCK_REALTIME.
[[nodiscard]] inline int64_t realtime_ns() noexcept
{
  struct timespec ts{};
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

} // namespace axon
