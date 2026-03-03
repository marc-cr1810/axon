/**
 * test_channel.cpp
 * In-process channel test: verifies zero-copy semantics and ref-count reclamation
 * using a fake shared memory buffer (heap allocation that mimics shm layout).
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <axon/shm/shm_allocator.hpp>
#include <axon/transport/chunk.hpp>
#include <axon/sync/atomic_queue.hpp>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace axon;

struct FakeShm
{
  std::vector<char> buf;
  ShmAllocator *alloc;
  explicit FakeShm(std::size_t sz = 4 * 1024 * 1024) : buf(sz, 0)
  {
    alloc = ShmAllocator::init(buf.data(), sz);
  }
  void *base()
  {
    return buf.data();
  }
};

struct Point
{
  float x, y, z;
};

TEST_CASE("chunk header layout fits in one cache line")
{
  CHECK(sizeof(ChunkHeader) == kCacheLineSize);
}

TEST_CASE("chunk alloc/free round-trip")
{
  FakeShm shm;
  auto *alloc = shm.alloc;
  void *base = shm.base();

  const uint32_t csz = chunk_alloc_size(sizeof(Point));
  uint32_t off = alloc->allocate(csz);
  REQUIRE(off != ShmAllocator::kInvalidOffset);

  // Initialise header
  auto *hdr = chunk_header(base, off);
  hdr->magic = kChunkMagic;
  hdr->payload_size = sizeof(Point);
  hdr->chunk_size = csz;
  hdr->ref_count.store(1, std::memory_order_relaxed);

  // Write payload via zero-copy pointer
  auto *payload = static_cast<Point *>(chunk_payload(base, off));
  *payload = {1.0f, 2.0f, 3.0f};

  // Read back
  auto *read_payload = static_cast<const Point *>(chunk_payload(base, off));
  CHECK(read_payload->x == doctest::Approx(1.0f));
  CHECK(read_payload->y == doctest::Approx(2.0f));
  CHECK(read_payload->z == doctest::Approx(3.0f));

  // Decrement ref and reclaim
  if (hdr->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
    alloc->deallocate(off, csz);

  // Should be able to allocate again at the same offset (from free-list)
  uint32_t off2 = alloc->allocate(csz);
  CHECK(off2 == off);
}

TEST_CASE("SpscQueue as offset ring")
{
  SpscQueue<uint32_t, 64> ring;

  // Simulate publisher: push offset
  CHECK(ring.push(0x1000u));
  CHECK(ring.push(0x2000u));

  // Simulate subscriber: pop offset
  auto v1 = ring.pop();
  REQUIRE(v1.has_value());
  CHECK(*v1 == 0x1000u);

  auto v2 = ring.pop();
  REQUIRE(v2.has_value());
  CHECK(*v2 == 0x2000u);

  CHECK(!ring.pop().has_value());
}
