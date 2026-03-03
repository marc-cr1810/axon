#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <axon/shm/shm_allocator.hpp>
#include <cstdlib>
#include <vector>
#include <thread>
#include <set>
#include <mutex>

using namespace axon;

static constexpr std::size_t kSegSize = 4 * 1024 * 1024; // 4 MB

// Allocate a properly aligned heap buffer to stand in for shared memory
struct FakeShm
{
  void *buf;
  std::size_t sz;
  ShmAllocator *alloc;
  explicit FakeShm(std::size_t size = kSegSize) : sz{size}
  {
    buf = std::aligned_alloc(64, size); // ShmAllocator has alignas(64) fields
    std::memset(buf, 0, size);
    alloc = ShmAllocator::init(buf, size);
  }
  ~FakeShm()
  {
    std::free(buf);
  }
};

TEST_CASE("allocate and deallocate small blocks")
{
  FakeShm shm;
  auto *a = shm.alloc;
  REQUIRE(a != nullptr);

  uint32_t off = a->allocate(32);
  CHECK(off != ShmAllocator::kInvalidOffset);
  CHECK(off >= ShmAllocator::kHeaderSize);

  a->deallocate(off, 32);

  // After free, the next alloc of the same size should recycle
  uint32_t off2 = a->allocate(32);
  CHECK(off2 == off);
}

TEST_CASE("allocate multiple size classes")
{
  FakeShm shm;
  auto *a = shm.alloc;

  std::vector<uint32_t> offsets;
  for (uint32_t sz : {32u, 64u, 128u, 256u, 512u, 1024u})
  {
    uint32_t off = a->allocate(sz);
    CHECK(off != ShmAllocator::kInvalidOffset);
    offsets.push_back(off);
  }

  // All offsets must be unique (no overlap)
  std::set<uint32_t> seen(offsets.begin(), offsets.end());
  CHECK(seen.size() == offsets.size());

  const uint32_t sizes[] = {32u, 64u, 128u, 256u, 512u, 1024u};
  for (std::size_t i = 0; i < offsets.size(); ++i)
    a->deallocate(offsets[i], sizes[i]);
}

TEST_CASE("concurrent allocation stress")
{
  FakeShm shm;
  auto *a = shm.alloc;

  constexpr int kThreads = 4;
  constexpr int kPerThread = 1000;
  std::mutex mtx;
  std::vector<uint32_t> all_offsets;
  std::vector<std::thread> threads;

  for (int t = 0; t < kThreads; ++t)
  {
    threads.emplace_back(
        [&]
        {
          std::vector<uint32_t> local;
          for (int i = 0; i < kPerThread; ++i)
          {
            uint32_t off = a->allocate(64);
            if (off != ShmAllocator::kInvalidOffset)
              local.push_back(off);
          }
          for (auto off : local)
            a->deallocate(off, 64);
          std::lock_guard lock{mtx};
          all_offsets.insert(all_offsets.end(), local.begin(), local.end());
        });
  }
  for (auto &th : threads)
    th.join();
  CHECK(!all_offsets.empty());
}
