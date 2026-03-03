/**
 * @file channel.hpp
 * @brief Typed, zero-copy pub/sub channel over shared memory.
 *
 * A Channel<T> provides:
 *   loan()     → T*     — borrow a fresh chunk from the allocator
 *   publish(T*) → bool  — enqueue chunk offset, notify all subscribers
 *   take()     → T*     — dequeue and map next chunk (subscriber)
 *   release(T*) → void  — decrement ref-count; returns chunk to pool at 0
 *
 * The hot path (loan/publish and take/release) is entirely lock-free.
 * Subscriber registration touches the registry spinlock once per topic join.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <optional>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>   // Added for fprintf
#include <signal.h> // Added as per instruction
#include <cerrno>   // Added as per instruction

#include <axon/transport/chunk.hpp>
#include <axon/transport/registry.hpp>
#include <axon/sync/atomic_queue.hpp>
#include <axon/sync/notifier.hpp>
#include <axon/shm/shm_allocator.hpp>
#include <axon/platform/clock.hpp>
#include <axon/platform/cpu.hpp>
#include <axon/error.hpp>

namespace axon
{

// ── Topic type-hash (compile-time FNV-1a on __PRETTY_FUNCTION__) ─────────────
template <typename T> consteval uint64_t type_hash() noexcept
{
  constexpr std::string_view name = __PRETTY_FUNCTION__;
  uint64_t h = 0xcbf29ce484222325ULL;
  for (char c : name)
  {
    h ^= static_cast<uint64_t>(c);
    h *= 0x100000001b3ULL;
  }
  return h;
}

// ── TopicName compile-time string ─────────────────────────────────────────────
template <std::size_t N> struct TopicName
{
  char data[N]{};
  consteval TopicName(const char (&s)[N])
  {
    for (std::size_t i = 0; i < N; ++i)
      data[i] = s[i];
  }
  consteval std::string_view view() const
  {
    return {data, N - 1};
  }
  template <std::size_t M> consteval bool operator==(const TopicName<M> &o) const
  {
    return view() == o.view();
  }
};

// ── Compile-time TopicRegistry ────────────────────────────────────────────────
template <typename... Defs> struct TopicRegistry
{
  template <TopicName Name, typename T> static consteval void assert_type()
  {
    constexpr bool found = (... || Defs::template matches<Name>());
    static_assert(found, "Topic not found in compile-time TopicRegistry.");
    constexpr bool type_ok = (... || (Defs::template matches<Name>() && std::is_same_v<typename Defs::type, T>));
    static_assert(type_ok, "Type mismatch for this topic in TopicRegistry.");
  }
};

template <TopicName Name, typename T> struct TopicDef
{
  static constexpr auto name = Name;
  using type = T;
  template <TopicName Q> static consteval bool matches()
  {
    return Name == Q;
  }
};

// ── Serialisable concept ──────────────────────────────────────────────────────
template <typename T>
concept Serialisable = std::is_trivially_copyable_v<T>;

// ── Per-topic in-shm SPSC queue of chunk offsets ─────────────────────────────
// One queue per subscriber slot; producer writes, subscriber reads.
static constexpr std::size_t kDefaultQueueCap = 64;
using OffsetQueue = SpscQueue<uint32_t, kDefaultQueueCap>;

// ── ChannelWriter (used by Publisher) ────────────────────────────────────────
struct ChannelWriter
{
  ShmAllocator *alloc{nullptr};
  void *base{nullptr};
  ChannelEntry *entry{nullptr};
  uint64_t sequence{0};
  uint32_t topic_id{0};
  uint64_t type_hash_v{0};
  uint32_t chunk_size{0}; ///< sizeof(ChunkHeader) + sizeof(T)

  // Stats
  std::atomic<uint64_t> published{0};
  std::atomic<uint64_t> dropped{0};

  ChannelWriter() = default;
  ~ChannelWriter() = default;

  /// Borrow a chunk. Returns raw payload pointer.
  [[nodiscard]] void *loan() noexcept
  {
    uint32_t off = alloc->allocate(chunk_size);
    if (off == ShmAllocator::kInvalidOffset)
      return nullptr;
    auto *hdr = chunk_header(base, off);
    hdr->magic = kChunkMagic;
    hdr->payload_size = chunk_size - sizeof(ChunkHeader);
    hdr->chunk_size = chunk_size;
    hdr->topic_id = topic_id;
    hdr->type_hash = type_hash_v;
    hdr->sequence = sequence++;
    hdr->timestamp_ns = now_ns();
    hdr->ref_count.store(0, std::memory_order_relaxed);
    return chunk_payload(base, off);
  }

  /// Publish a loaned payload pointer; enqueue its offset to all active subscribers.
  /// Returns number of subscribers notified.
  uint32_t publish(void *payload) noexcept
  {
    // Recover offset from payload pointer
    uint32_t off = static_cast<uint32_t>(reinterpret_cast<char *>(payload) - reinterpret_cast<char *>(base) - sizeof(ChunkHeader));

    auto *hdr = chunk_header(base, off);

    uint32_t notified = 0;
    for (std::size_t i = 0; i < kMaxSubs; ++i)
    {
      auto &sub = entry->subs[i];
      if (!sub.active.load(std::memory_order_acquire))
        continue;

      // Death detection: Check if the subscriber process is still alive.
      // If kill(pid, 0) fails with ESRCH, the process crashed or exited
      // without doing a clean teardown.
      if (::kill(sub.pid, 0) == -1 && errno == ESRCH)
      {
        sub.active.store(false, std::memory_order_release);
        uint32_t stale_q_off = sub.tail.exchange(0, std::memory_order_relaxed);
        if (stale_q_off)
          alloc->deallocate(stale_q_off, sizeof(OffsetQueue));
        continue;
      }

      // Each subscriber has its own OffsetQueue in shm
      uint32_t q_off = sub.tail.load(std::memory_order_relaxed);
      if (q_off == 0)
        continue;

      auto *q = reinterpret_cast<OffsetQueue *>(reinterpret_cast<char *>(base) + q_off);
      if (q && q->push(off))
      {
        hdr->ref_count.fetch_add(1, std::memory_order_relaxed);
        ++notified;

        // Wake subscriber via shared semaphore
        ::sem_post(&sub.wakeup_sem);
      }
      else
      {
        if (dropped.load() == 0)
        {
          std::fprintf(stderr, "[pub-debug] drop in queue q_off=%u, tail_=%zu, head_=%zu\n", q_off, q ? q->tail_.load() : 0, q ? q->head_.load() : 0);
        }
        dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }

    if (notified == 0)
    {
      // No subscribers — return chunk immediately
      alloc->deallocate(off, chunk_size);
    }
    published.fetch_add(1, std::memory_order_relaxed);
    return notified;
  }

  /// Return a chunk to the allocator (called when ref_count reaches 0).
  void reclaim(uint32_t off) noexcept
  {
    alloc->deallocate(off, chunk_size);
  }
};

// ── ChannelReader (used by Subscriber) ───────────────────────────────────────
struct ChannelReader
{
  ShmAllocator *alloc{nullptr};
  void *base{nullptr};
  ChannelEntry *entry{nullptr};
  SubSlot *slot{nullptr};
  OffsetQueue *queue{nullptr};
  Notifier notifier;
  uint32_t chunk_size{0};

  std::atomic<uint64_t> received{0};
  std::atomic<uint64_t> dropped{0};

  /// Take next available chunk.  Returns nullptr if empty.
  [[nodiscard]] void *try_take() noexcept
  {
    auto off = queue->pop();
    if (!off)
      return nullptr;
    received.fetch_add(1, std::memory_order_relaxed);
    return chunk_payload(base, *off);
  }

  /// Block until a chunk is available or timeout expires.
  [[nodiscard]] void *take(std::chrono::milliseconds timeout) noexcept
  {
    auto *p = try_take();
    if (p)
      return p;

    // Hybrid polling: Spin for ~10us before falling asleep
    // Assuming each cpu_pause() + loop overhead is ~10ns
    for (int i = 0; i < 1000; ++i)
    {
      cpu_pause();
      p = try_take();
      if (p)
        return p;
    }

    if (notifier.wait_for(timeout))
      return try_take();
    return nullptr;
  }

  /// Release a chunk; if ref_count reaches 0 the memory is reclaimed.
  void release(const void *payload) noexcept
  {
    uint32_t off = static_cast<uint32_t>(reinterpret_cast<const char *>(payload) - reinterpret_cast<const char *>(base) - sizeof(ChunkHeader));
    auto *hdr = chunk_header(base, off);
    if (hdr->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
      alloc->deallocate(off, chunk_size);
  }
};

} // namespace axon
