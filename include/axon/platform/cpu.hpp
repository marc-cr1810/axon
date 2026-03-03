/**
 * @file cpu.hpp
 * @brief Architecture detection, cache-line constants, and branch hints.
 *
 * Supports x86-64, ARM64, and ARM32 (Cortex-A series).
 */
#pragma once

#include <cstddef>
#include <new>

namespace axon
{

// ── Architecture detection ────────────────────────────────────────────────────
#if defined(__x86_64__) || defined(_M_X64)
#define AXON_ARCH_X86_64 1
#define AXON_CACHE_LINE 64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define AXON_ARCH_ARM64 1
#define AXON_CACHE_LINE 64
#elif defined(__arm__) || defined(_M_ARM)
#define AXON_ARCH_ARM32 1
#define AXON_CACHE_LINE 32
#else
#define AXON_ARCH_UNKNOWN 1
#define AXON_CACHE_LINE 64
#endif

/// Cache line size used for padding to prevent false sharing.
inline constexpr std::size_t kCacheLineSize = AXON_CACHE_LINE;

// ── Branch prediction hints ────────────────────────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
#define AXON_LIKELY(x) __builtin_expect(!!(x), 1)
#define AXON_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define AXON_LIKELY(x) (x)
#define AXON_UNLIKELY(x) (x)
#endif

// ── CPU pause / spin-wait hint ────────────────────────────────────────────────
/// Emit the appropriate spin-wait hint for the current architecture.
/// Reduces power consumption and contention during tight spin loops.
inline void cpu_pause() noexcept
{
#if defined(AXON_ARCH_X86_64)
  __asm__ volatile("pause" ::: "memory");
#elif defined(AXON_ARCH_ARM64)
  __asm__ volatile("yield" ::: "memory");
#elif defined(AXON_ARCH_ARM32)
  __asm__ volatile("yield" ::: "memory");
#else
  __asm__ volatile("" ::: "memory");
#endif
}

// ── Big.LITTLE efficiency core detection ─────────────────────────────────────
#include <vector>

/// Returns a list of CPU indices identified as efficiency (LITTLE / Atom) cores.
/// Falls back to empty on non-heterogeneous systems.
std::vector<int> find_efficiency_cores();

/// Pins the calling thread to the given set of CPU cores.
/// No-op if the list is empty.
void pin_thread_to_cores(const std::vector<int> &cores);

} // namespace axon
