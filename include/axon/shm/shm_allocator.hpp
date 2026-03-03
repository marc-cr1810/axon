/**
 * @file shm_allocator.hpp
 * @brief Lock-free free-list allocator that lives entirely inside shared memory.
 *
 * All pointers are stored as byte offsets from the segment base so they
 * remain valid after mmap() in any process (different virtual base addresses).
 *
 * Design
 * ──────
 * • Power-of-2 size classes: 32, 64, 128, … 65536 bytes.
 * • Each size class has an intrusive free-list head stored as an atomic
 *   uint64_t {version[32] | offset[32]} (ABA-prevention via version tag).
 * • The bump pointer advances through the segment for fresh allocations when
 *   no free-list entry is available.
 * • Thread-safe and cross-process-safe — uses only std::atomic<uint64_t>.
 *
 * Usage
 * ──────
 *   ShmAllocator* alloc = ShmAllocator::init(segment_base, segment_size);
 *   uint32_t off = alloc->allocate(200);   // => returns offset into segment
 *   alloc->deallocate(off, 200);
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <axon/platform/cpu.hpp>

namespace axon
{

class ShmAllocator
{
public:
  // ── Constants ─────────────────────────────────────────────────────────────
  static constexpr uint32_t kInvalidOffset = ~uint32_t{0};
  static constexpr uint32_t kMinClass = 5;  // 2^5  = 32 bytes
  static constexpr uint32_t kMaxClass = 16; // 2^16 = 65536 bytes
  static constexpr uint32_t kNumClasses = kMaxClass - kMinClass + 1;
  static constexpr uint32_t kMagic = 0xA10CA10C;
  static constexpr uint32_t kHeaderSize = 512; // reserved for ShmAllocator itself

  // ── Lifecycle ─────────────────────────────────────────────────────────────

  /// Initialise a fresh allocator at `base`.  Must only be called once by the
  /// segment creator, before any other process maps the segment.
  static ShmAllocator *init(void *base, std::size_t total_size) noexcept
  {
    auto *self = new (base) ShmAllocator{};
    self->total_size_ = static_cast<uint32_t>(total_size);
    // Bump pointer starts right after the allocator header
    self->bump_.store(kHeaderSize, std::memory_order_relaxed);
    for (auto &fl : self->free_lists_)
      fl.store(encode(0, kInvalidOffset), std::memory_order_relaxed);
    self->magic_.store(kMagic, std::memory_order_release);
    return self;
  }

  /// Obtain the allocator from an already-initialised segment.
  /// Returns nullptr if the magic number is not yet set (segment not ready).
  static ShmAllocator *from(void *base) noexcept
  {
    auto *self = static_cast<ShmAllocator *>(base);
    if (self->magic_.load(std::memory_order_acquire) != kMagic)
      return nullptr;
    return self;
  }

  // ── Allocation ────────────────────────────────────────────────────────────

  /// Allocate `size` bytes and return the offset from segment base.
  /// Returns kInvalidOffset on OOM or if size exceeds kMaxClass.
  [[nodiscard]] uint32_t allocate(uint32_t size) noexcept
  {
    const uint32_t cls = size_class(size);
    if (cls == kNumClasses)
      return kInvalidOffset; // too large

    // Try free-list first
    auto &fl = free_lists_[cls];
    uint64_t head = fl.load(std::memory_order_acquire);
    while (offset_of(head) != kInvalidOffset)
    {
      uint32_t off = offset_of(head);
      // Read next pointer from the block itself
      uint32_t next = *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(this) + off);
      uint64_t desired = encode(version_of(head) + 1, next);
      if (fl.compare_exchange_weak(head, desired, std::memory_order_acq_rel, std::memory_order_acquire))
        return off;
    }

    // Bump allocate
    const uint32_t slot_size = 1u << (cls + kMinClass);
    uint32_t pos = bump_.fetch_add(slot_size, std::memory_order_relaxed);
    if (pos + slot_size > total_size_)
    {
      // Undo — best-effort; another thread might sneak in but that is
      // still safe (it will OOM on its own attempt).
      bump_.fetch_sub(slot_size, std::memory_order_relaxed);
      return kInvalidOffset;
    }
    return pos;
  }

  /// Return a previously allocated block back to the free-list.
  void deallocate(uint32_t offset, uint32_t size) noexcept
  {
    const uint32_t cls = size_class(size);
    if (cls == kNumClasses || offset == kInvalidOffset)
      return;

    auto &fl = free_lists_[cls];
    uint64_t head = fl.load(std::memory_order_acquire);
    for (;;)
    {
      // Write next pointer into the block itself
      *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(this) + offset) = offset_of(head);
      uint64_t desired = encode(version_of(head) + 1, offset);
      if (fl.compare_exchange_weak(head, desired, std::memory_order_acq_rel, std::memory_order_acquire))
        return;
    }
  }

  /// Manually advance the bump pointer (used for static reservations like RegistryHeader)
  void reserve(uint32_t size) noexcept
  {
    uint32_t current = bump_.load(std::memory_order_relaxed);
    uint32_t target = current + size;
    // Align up to 4096 (page size) to ensure all subsequent bump allocations are well-aligned
    target = (target + 4095) & ~4095;
    bump_.store(target, std::memory_order_relaxed);
  }

private:
  // ── Encoding helpers ──────────────────────────────────────────────────────
  static uint64_t encode(uint32_t ver, uint32_t off) noexcept
  {
    return (static_cast<uint64_t>(ver) << 32) | off;
  }
  static uint32_t version_of(uint64_t v) noexcept
  {
    return static_cast<uint32_t>(v >> 32);
  }
  static uint32_t offset_of(uint64_t v) noexcept
  {
    return static_cast<uint32_t>(v);
  }

  /// Map size → size-class index (0 = 32B, 1 = 64B, …)
  static uint32_t size_class(uint32_t size) noexcept
  {
    if (size == 0)
      size = 1;
    // Round up to next power of 2
    uint32_t p = kMinClass;
    while ((1u << p) < size && p <= kMaxClass)
      ++p;
    if (p > kMaxClass)
      return kNumClasses; // sentinel: too large
    return p - kMinClass;
  }

  // ── Fields (live in shared memory) ────────────────────────────────────────
  alignas(kCacheLineSize) std::atomic<uint32_t> magic_{0};
  alignas(kCacheLineSize) std::atomic<uint32_t> bump_{0};
  alignas(kCacheLineSize) uint32_t total_size_{0};
  alignas(kCacheLineSize) std::atomic<uint64_t> free_lists_[kNumClasses]{};
};

} // namespace axon
