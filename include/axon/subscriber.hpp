/**
 * @file subscriber.hpp
 * @brief Zero-copy subscriber API with async callback support.
 *
 * Usage (blocking):
 *   Subscriber<SensorReading> sub{node, "sensors/temp"};
 *   while (auto* p = sub.take(500ms)) {
 *       use(*p);
 *       sub.release(p);
 *   }
 *
 * Usage (async callback via thread pool):
 *   sub.on([](const SensorReading& r){ ... });
 *
 * The watchdog thread detects dead publishers via heartbeat timeout.
 */
#pragma once

#include <atomic>
#include <string_view>
#include <string>
#include <thread>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <semaphore.h>

#include "axon/node.hpp"
#include <axon/transport/channel.hpp>
#include <axon/platform/cpu.hpp>
#include <axon/error.hpp>

using namespace std::chrono_literals;

namespace axon
{

template <Serialisable T> class Subscriber
{
public:
  explicit Subscriber(Node &node, std::string_view topic) : node_{&node}, topic_{topic}
  {
    setup();
  }

  ~Subscriber()
  {
    stop_ = true;
    // Unregister slot
    if (reader_.slot)
    {
      reader_.slot->active.store(false, std::memory_order_release);
    }
    if (listener_.joinable())
      listener_.join();
    if (watchdog_.joinable())
      watchdog_.join();
  }

  Subscriber(Subscriber &&) = default;
  Subscriber(const Subscriber &) = delete;
  Subscriber &operator=(const Subscriber &) = delete;

  // ── Zero-copy API ─────────────────────────────────────────────────────────

  /// Take the next available chunk (non-blocking).  Returns nullptr if empty.
  [[nodiscard]] const T *try_take() noexcept
  {
    if (!reader_.queue)
      return nullptr;
    return static_cast<const T *>(reader_.try_take());
  }

  /// Take the next chunk, blocking until one arrives or the timeout expires.
  [[nodiscard]] const T *take(std::chrono::milliseconds timeout = 1000ms) noexcept
  {
    if (!reader_.queue)
      return nullptr;
    return static_cast<const T *>(reader_.take(timeout));
  }

  /// Release a chunk obtained from take()/try_take().
  void release(const T *payload) noexcept
  {
    if (payload)
      reader_.release(static_cast<const void *>(payload));
  }

  // ── Async callback ────────────────────────────────────────────────────────

  /// Register a callback invoked on each received chunk.
  /// Spawns a dedicated listener thread on efficiency cores.
  void on(std::function<void(const T &)> cb)
  {
    cb_ = std::move(cb);
    listener_ = std::thread(
        [this]
        {
          pin_thread_to_cores(find_efficiency_cores());
          while (!stop_)
          {
            const T *p = take(500ms);
            if (!p)
              continue;
            if (cb_)
              cb_(*p);
            release(p);
          }
        });
    start_watchdog();
  }

  // ── Stats ─────────────────────────────────────────────────────────────────
  uint64_t received() const noexcept
  {
    return reader_.received.load();
  }
  uint64_t dropped() const noexcept
  {
    return reader_.dropped.load();
  }

  bool publisher_alive() const noexcept
  {
    if (!reader_.entry)
      return false;
    return reader_.entry->active;
  }

private:
  void setup()
  {
    auto *reg = node_->registry();
    auto *alloc = node_->allocator();
    void *base = node_->base();

    // Wait for the topic to appear in the registry
    ChannelEntry *entry = nullptr;
    for (int retry = 0; retry < 100 && !entry; ++retry)
    {
      uint32_t count = reg->count.load(std::memory_order_acquire);
      for (uint32_t i = 0; i < count; ++i)
      {
        std::string_view current_topic{reg->entries[i].topic};
        if (current_topic == topic_)
        {
          entry = &reg->entries[i];
          break;
        }
      }
      if (!entry)
        std::this_thread::sleep_for(50ms);
    }
    if (!entry)
    {
      std::fprintf(stderr, "[sub] setup failed: topic not found in %u entries\n", reg->count.load());
      return;
    }

    // Find a free subscriber slot
    SubSlot *slot = nullptr;
    for (auto &s : entry->subs)
    {
      bool expected = false;
      if (s.active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      {
        slot = &s;
        break;
      }
    }
    if (!slot)
    {
      return;
    }

    // Reuse existing queue if possible to prevent leaks
    uint32_t q_off = slot->tail.load(std::memory_order_acquire);
    OffsetQueue *queue = nullptr;
    if (q_off != 0 && q_off != ShmAllocator::kInvalidOffset)
    {
      queue = reinterpret_cast<OffsetQueue *>(reinterpret_cast<char *>(base) + q_off);
      // Drain any stale messages from the old queue and reclaim them
      while (auto off = queue->pop())
      {
        auto *hdr = chunk_header(base, *off);
        if (hdr->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
          alloc->deallocate(*off, entry->chunk_size);
      }
    }
    else
    {
      q_off = alloc->allocate(sizeof(OffsetQueue));
      if (q_off == ShmAllocator::kInvalidOffset)
      {
        slot->active.store(false, std::memory_order_release);
        std::fprintf(stderr, "[sub] setup failed: out of memory for queue\n");
        return;
      }
      queue = new (reinterpret_cast<char *>(base) + q_off) OffsetQueue{};
      slot->tail.store(q_off, std::memory_order_release);
    }

    // Initialize the shared semaphore
    ::sem_init(&slot->wakeup_sem, 1, 0);

    // Store PID for death detection
    slot->pid = ::getpid();

    reader_.alloc = alloc;
    reader_.base = base;
    reader_.entry = entry;
    reader_.slot = slot;
    reader_.queue = queue;
    reader_.chunk_size = entry->chunk_size;
    reader_.notifier = Notifier{&slot->wakeup_sem};
  }

  void start_watchdog()
  {
    watchdog_ = std::thread(
        [this]
        {
          pin_thread_to_cores(find_efficiency_cores());
          while (!stop_)
          {
            std::this_thread::sleep_for(kHeartbeatInterval);
            if (reader_.entry && !reader_.entry->active)
              std::fprintf(stderr, "[axon][watchdog] Publisher on '%s' appears dead.\n", topic_.c_str());
          }
        });
  }

  Node *node_;
  std::string topic_;
  ChannelReader reader_{};
  std::function<void(const T &)> cb_;
  std::thread listener_;
  std::thread watchdog_;
  std::atomic<bool> stop_{false};
};

} // namespace axon
