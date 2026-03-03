/**
 * @file registry.hpp
 * @brief Named channel registry that lives inside shared memory.
 *
 * The registry is a flat, fixed-capacity table of ChannelEntry records,
 * stored at the start of the shared memory segment (after the allocator header).
 * Protected by a Spinlock for concurrent registration from multiple processes.
 *
 * Topics are referenced by a numeric `topic_id` (assigned at registration)
 * to avoid repeated string comparisons in the hot path.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <expected>
#include <vector>
#include <string>
#include <atomic>

#include <semaphore.h>

#include "axon/error.hpp"
#include "axon/sync/spinlock.hpp"
#include "axon/platform/cpu.hpp"

namespace axon
{

inline constexpr std::size_t kMaxTopics = 256;
inline constexpr std::size_t kMaxTopicLen = 128;
inline constexpr std::size_t kMaxSubs = 64;
inline constexpr uint32_t kRegistryMagic = 0xAE610001;

/// Per-subscriber notification slot stored in the registry.
struct SubSlot
{
  std::atomic<bool> active{false};
  std::atomic<uint32_t> tail{0}; ///< offsets to OffsetQueue
  pid_t pid{0};
  sem_t wakeup_sem;
};

/// One registered channel.
struct ChannelEntry
{
  char topic[kMaxTopicLen]{};
  uint32_t topic_id{0};
  uint64_t type_hash{0};
  uint32_t chunk_size{0}; ///< byte size of each chunk (header + payload)
  uint32_t queue_cap{0};  ///< ring queue capacity (number of entries)
  uint32_t queue_off{0};  ///< shm offset to the MpmcQueue of chunk offsets
  bool active{false};
  alignas(8) SubSlot subs[kMaxSubs]{};
};

/// The registry header that lives at a fixed offset in the shm segment.
struct alignas(64) RegistryHeader
{
  std::atomic<uint32_t> magic{0};
  std::atomic<uint32_t> count{0};
  Spinlock lock;
  ChannelEntry entries[kMaxTopics]{};
};

} // namespace axon
