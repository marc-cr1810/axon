/**
 * @file chunk.hpp
 * @brief Shared memory chunk layout.
 *
 * A chunk is a fixed-size block in shared memory composed of:
 *   ChunkHeader  — metadata (ref-count, sequence, timestamp, type hash, sizes)
 *   T payload[]  — immediately follows the header
 *
 * The publisher obtains a pointer to the payload (loan), writes into it, then
 * publishes by placing the chunk's shm offset into a per-topic ring queue.
 * The subscriber receives the offset and maps it to a typed pointer — no copy.
 * When the subscriber is done it calls release(), which atomically decrements
 * the ref-count; when it reaches 0 the chunk is returned to the free pool.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <axon/platform/cpu.hpp>
#include <axon/platform/clock.hpp>

namespace axon
{

static constexpr uint32_t kChunkMagic = 0xC80AC80A;

struct alignas(kCacheLineSize) ChunkHeader
{
  uint32_t magic{kChunkMagic};
  uint32_t payload_size{};           ///< size of the user payload in bytes
  uint32_t chunk_size{};             ///< total size of header + payload (rounded up)
  uint32_t topic_id{};               ///< matched against channel's registered id
  uint64_t type_hash{};              ///< compile-time FNV hash of T
  uint64_t sequence{};               ///< monotonically increasing per-channel
  int64_t timestamp_ns{};            ///< CLOCK_MONOTONIC_RAW at publish time
  std::atomic<int32_t> ref_count{0}; ///< active readers; 0 → return to free pool
  // ── padding to fill the cache line ───────────────────────────────────────
  char _pad[kCacheLineSize - sizeof(uint32_t) * 3 - sizeof(uint32_t) - sizeof(uint64_t) * 2 - sizeof(int64_t) - sizeof(std::atomic<int32_t>)]{};
};
static_assert(sizeof(ChunkHeader) == kCacheLineSize, "ChunkHeader must fit exactly in one cache line");

/// Payload pointer from a chunk offset in shared memory (no copy).
/// `base` is the mmap base of the segment; `offset` is from allocate().
inline void *chunk_payload(void *base, uint32_t offset) noexcept
{
  return reinterpret_cast<char *>(base) + offset + sizeof(ChunkHeader);
}
inline const void *chunk_payload(const void *base, uint32_t offset) noexcept
{
  return reinterpret_cast<const char *>(base) + offset + sizeof(ChunkHeader);
}
inline ChunkHeader *chunk_header(void *base, uint32_t offset) noexcept
{
  return reinterpret_cast<ChunkHeader *>(reinterpret_cast<char *>(base) + offset);
}
inline const ChunkHeader *chunk_header(const void *base, uint32_t offset) noexcept
{
  return reinterpret_cast<const ChunkHeader *>(reinterpret_cast<const char *>(base) + offset);
}

/// Total allocation size needed for a chunk carrying `payload_size` bytes.
inline constexpr uint32_t chunk_alloc_size(uint32_t payload_size) noexcept
{
  return static_cast<uint32_t>(sizeof(ChunkHeader)) + payload_size;
}

} // namespace axon
