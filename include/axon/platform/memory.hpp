/**
 * @file memory.hpp
 * @brief Hugepage allocation and mlock helpers.
 */
#pragma once

#include <cstddef>
#include <sys/mman.h>
#include <cerrno>

namespace axon
{

/// Allocate `size` bytes backed by transparent hugepages (MAP_HUGETLB) if
/// AXON_USE_HUGEPAGES is defined, otherwise falls back to a standard mmap.
/// Returns MAP_FAILED on failure.
inline void *alloc_shm_backed(void *addr, std::size_t size, int prot, int flags, int fd) noexcept
{
  void *ptr = ::mmap(addr, size, prot, flags, fd, 0);
  if (ptr == MAP_FAILED)
    return MAP_FAILED;

#ifdef AXON_USE_HUGEPAGES
  // Hint transparent huge pages — reduces TLB pressure (esp. on ARM64).
  // Falls back silently to 4 KB pages if THP is disabled.
  ::madvise(ptr, size, MADV_HUGEPAGE);
#else
  ::madvise(ptr, size, MADV_SEQUENTIAL);
#endif
  return ptr;
}

/// Attempt to lock `size` bytes starting at `ptr` into RAM (prevent page-out).
/// Silently ignores failures (caller may lack CAP_IPC_LOCK).
inline void try_mlock(void *ptr, std::size_t size) noexcept
{
  ::mlock(ptr, size);
}

} // namespace axon
