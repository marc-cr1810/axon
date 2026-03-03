/**
 * @file node.hpp
 * @brief Process-level node — owns the shared memory segment and top-level objects.
 *
 * One Node per process.  The first process to call Node::create() becomes the
 * creator and will shm_unlink the segment on destruction.  All subsequent
 * processes call Node::join() to map the existing segment.
 *
 * The segment layout is:
 *   [0 .. ShmAllocator::kHeaderSize)  — ShmAllocator control block
 *   [kHeaderSize .. kRegistryOffset)  — (unused padding)
 *   [kRegistryOffset .. ...)          — RegistryHeader + ChannelEntry table
 *   [dynamic via allocator]           — Chunks, OffsetQueues, SharedState data
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <expected>

#include "axon/error.hpp"
#include "axon/shm/shm_allocator.hpp"
#include "axon/shm/shm_segment.hpp"
#include "axon/transport/registry.hpp"
#include <axon/shm/shm_segment.hpp>
#include <axon/shm/shm_allocator.hpp>
#include <axon/transport/registry.hpp>

namespace axon
{

// Segment layout offsets
static constexpr std::size_t kRegistryOffset = ShmAllocator::kHeaderSize;
static constexpr std::size_t kDefaultSegSize = 64 * 1024 * 1024; // 64 MB

class Node
{
public:
  /// Create a new shared memory segment and initialise all internal structures.
  /// Should be called by the first process (publisher / daemon).
  static std::expected<Node, Error> create(std::string_view name, std::size_t seg_size = kDefaultSegSize) noexcept;

  /// Join an existing segment created by another process.
  static std::expected<Node, Error> join(std::string_view name, std::size_t seg_size = kDefaultSegSize) noexcept;

  ~Node() = default;
  Node(Node &&) noexcept = default;
  Node &operator=(Node &&) noexcept = default;
  Node(const Node &) = delete;
  Node &operator=(const Node &) = delete;

  // ── Accessors (for Publisher/Subscriber internals) ────────────────────────
  [[nodiscard]] ShmAllocator *allocator() noexcept
  {
    return alloc_;
  }
  [[nodiscard]] void *base() noexcept
  {
    return seg_.base();
  }
  [[nodiscard]] RegistryHeader *registry() noexcept
  {
    return reg_;
  }
  [[nodiscard]] std::string_view name() const noexcept
  {
    return seg_.name();
  }

private:
  Node(ShmSegment seg, ShmAllocator *alloc, RegistryHeader *reg) noexcept : seg_{std::move(seg)}, alloc_{alloc}, reg_{reg}
  {
  }

  ShmSegment seg_;
  ShmAllocator *alloc_{nullptr};
  RegistryHeader *reg_{nullptr};
};

} // namespace axon
