/**
 * @file shared_state.hpp
 * @brief Seqlock-protected shared state for single-writer, multi-reader access.
 *
 * SharedStateOwner<T>  — writes state with atomic commit, change notifications
 * SharedStateReader<T> — reads without blocking, seqlock-protected
 *
 * The state lives in its own named shm segment (independent of the ring segment).
 * A change notification is published as a typed message on "state/<name>/changed".
 */
#pragma once

#include <string>
#include <string_view>
#include <expected>
#include <functional>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdio>

#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

#include <axon/shm/shm_segment.hpp>
#include <axon/sync/seqlock.hpp>
#include <axon/transport/channel.hpp>
#include <axon/error.hpp>
#include <axon/platform/clock.hpp>

using namespace std::chrono_literals;

namespace axon
{

static constexpr uint32_t kStateMagic = 0xA64B5A00;

struct alignas(64) SharedStateHeader
{
  std::atomic<uint32_t> magic{0};
  uint32_t schema_version{0};
  uint32_t state_size{0};
  std::atomic<uint32_t> version{0};
  char name[128]{};
  pid_t owner_pid{0};
  alignas(64) std::atomic<bool> owner_active{false};
  alignas(64) std::atomic<bool> ready{false};
  alignas(64) std::atomic<int64_t> committed_at{0};
  alignas(64) Seqlock lock;
};

// ── SharedStateOwner ─────────────────────────────────────────────────────────
template <Serialisable T> class SharedStateOwner
{
public:
  static std::expected<SharedStateOwner<T>, Error> create(std::string_view name, uint32_t schema_version = 1) noexcept
  {
    auto shm = ShmSegment::create(std::string{"/axon_state_"} + sanitise(name), sizeof(SharedStateHeader) + sizeof(T));
    if (!shm)
      return std::unexpected(Error::ShmCreateFailed);

    auto *hdr = shm->as<SharedStateHeader>();

    bool expected = false;
    if (!hdr->owner_active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      return std::unexpected(Error::OwnerAlreadyExists);

    hdr->schema_version = schema_version;
    hdr->state_size = sizeof(T);
    hdr->version.store(0, std::memory_order_relaxed);
    hdr->owner_pid = ::getpid();
    name.copy(hdr->name, sizeof(hdr->name) - 1);

    T *state_ptr = reinterpret_cast<T *>(reinterpret_cast<char *>(hdr) + sizeof(SharedStateHeader));
    new (state_ptr) T{};

    hdr->magic.store(kStateMagic, std::memory_order_release);

    return SharedStateOwner<T>{std::move(*shm), hdr, state_ptr, std::string{name}};
  }

  ~SharedStateOwner()
  {
    if (hdr_)
      hdr_->owner_active.store(false, std::memory_order_release);
  }

  SharedStateOwner(SharedStateOwner &&) = default;
  SharedStateOwner &operator=(SharedStateOwner &&) = default;
  SharedStateOwner(const SharedStateOwner &) = delete;
  SharedStateOwner &operator=(const SharedStateOwner &) = delete;

  T *operator->() noexcept
  {
    return &staging_;
  }
  T &operator*() noexcept
  {
    return staging_;
  }

  /// Atomically commit the staging value into shared memory and increment version.
  void commit() noexcept
  {
    hdr_->lock.write_begin();
    std::memcpy(state_ptr_, &staging_, sizeof(T));
    hdr_->version.fetch_add(1, std::memory_order_relaxed);
    hdr_->lock.write_end();
    hdr_->committed_at.store(now_ns(), std::memory_order_release);
    hdr_->ready.store(true, std::memory_order_release);
  }

  /// Only commit if the staging value differs from the committed value.
  bool commit_if_changed() noexcept
  {
    if (std::memcmp(state_ptr_, &staging_, sizeof(T)) == 0)
      return false;
    commit();
    return true;
  }

  [[nodiscard]] uint32_t version() const noexcept
  {
    return hdr_->version.load(std::memory_order_relaxed);
  }

private:
  SharedStateOwner(ShmSegment shm, SharedStateHeader *hdr, T *state_ptr, std::string name) noexcept : shm_{std::move(shm)}, hdr_{hdr}, state_ptr_{state_ptr}, name_{std::move(name)}, staging_{*state_ptr}
  {
  }

  static std::string sanitise(std::string_view n)
  {
    std::string s{n};
    for (char &c : s)
      if (c == '/')
        c = '_';
    return s;
  }

  ShmSegment shm_;
  SharedStateHeader *hdr_{nullptr};
  T *state_ptr_{nullptr};
  std::string name_;
  T staging_{};
};

// ── SharedStateReader ─────────────────────────────────────────────────────────
template <Serialisable T> class SharedStateReader
{
public:
  static std::expected<SharedStateReader<T>, Error> open(std::string_view name, uint32_t schema_version = 1) noexcept
  {
    auto shm = ShmSegment::open(std::string{"/axon_state_"} + sanitise(name), sizeof(SharedStateHeader) + sizeof(T));
    if (!shm)
      return std::unexpected(Error::ShmOpenFailed);

    auto *hdr = shm->as<SharedStateHeader>();
    for (int i = 0; i < 50 && hdr->magic.load(std::memory_order_acquire) != kStateMagic; ++i)
      std::this_thread::sleep_for(100ms);
    if (hdr->magic.load(std::memory_order_acquire) != kStateMagic)
      return std::unexpected(Error::ShmNotReady);

    if (hdr->schema_version != schema_version || hdr->state_size != sizeof(T))
      return std::unexpected(Error::SchemaMismatch);

    T *state_ptr = reinterpret_cast<T *>(reinterpret_cast<char *>(hdr) + sizeof(SharedStateHeader));

    return SharedStateReader<T>{std::move(*shm), hdr, state_ptr, std::string{name}};
  }

  SharedStateReader(SharedStateReader &&) = default;
  SharedStateReader &operator=(SharedStateReader &&) = default;
  SharedStateReader(const SharedStateReader &) = delete;
  SharedStateReader &operator=(const SharedStateReader &) = delete;

  /// Block until the first commit(), then read.
  std::expected<T, Error> read(std::chrono::milliseconds timeout = 5000ms) const noexcept
  {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!hdr_->ready.load(std::memory_order_acquire))
    {
      if (std::chrono::steady_clock::now() >= deadline)
        return std::unexpected(Error::StateNotReady);
      std::this_thread::sleep_for(10ms);
    }
    return seqlock_read(hdr_->lock, state_ptr_);
  }

  /// Non-blocking read; fails if no commit has happened yet.
  std::expected<T, Error> try_read() const noexcept
  {
    if (!hdr_->ready.load(std::memory_order_acquire))
      return std::unexpected(Error::StateNotReady);
    return seqlock_read(hdr_->lock, state_ptr_);
  }

  /// Read only if last commit is younger than max_age.
  std::expected<T, Error> read_if_fresh(std::chrono::milliseconds max_age) const noexcept
  {
    if (!hdr_->ready.load(std::memory_order_acquire))
      return std::unexpected(Error::StateNotReady);
    if (age() > max_age)
      return std::unexpected(Error::StateTooStale);
    return seqlock_read(hdr_->lock, state_ptr_);
  }

  [[nodiscard]] uint32_t version() const noexcept
  {
    return hdr_->version.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool changed_since(uint32_t v) const noexcept
  {
    return version() > v;
  }

  [[nodiscard]] std::chrono::milliseconds age() const noexcept
  {
    if (!hdr_->ready.load(std::memory_order_acquire))
      return std::chrono::milliseconds::max();
    const int64_t committed = hdr_->committed_at.load(std::memory_order_acquire);
    return std::chrono::milliseconds{(now_ns() - committed) / 1'000'000LL};
  }

  [[nodiscard]] bool owner_alive() const noexcept
  {
    return ::kill(hdr_->owner_pid, 0) == 0;
  }

private:
  SharedStateReader(ShmSegment shm, SharedStateHeader *hdr, T *state_ptr, std::string name) noexcept : shm_{std::move(shm)}, hdr_{hdr}, state_ptr_{state_ptr}, name_{std::move(name)}
  {
  }

  static std::string sanitise(std::string_view n)
  {
    std::string s{n};
    for (char &c : s)
      if (c == '/')
        c = '_';
    return s;
  }

  ShmSegment shm_;
  SharedStateHeader *hdr_{nullptr};
  T *state_ptr_{nullptr};
  std::string name_;
};

} // namespace axon
