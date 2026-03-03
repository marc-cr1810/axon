// System / STL headers BEFORE any axon header
#include <cstring>
#include <chrono>
#include <thread>

#include <axon/node.hpp>
#include <axon/shm/shm_allocator.hpp>
#include <axon/transport/registry.hpp>

using namespace std::chrono_literals;

namespace axon
{

std::expected<Node, Error> Node::create(std::string_view name, std::size_t seg_size) noexcept
{
  auto seg = ShmSegment::create(std::string{"/axon_"} + std::string{name}, seg_size);
  if (!seg)
    return std::unexpected(Error::ShmCreateFailed);

  void *base = seg->base();
  std::memset(base, 0, seg_size);

  auto *alloc = ShmAllocator::init(base, seg_size);

  auto *reg = reinterpret_cast<RegistryHeader *>(reinterpret_cast<char *>(base) + kRegistryOffset);
  alloc->reserve(sizeof(RegistryHeader));

  new (&reg->lock) Spinlock{};
  new (&reg->count) std::atomic<uint32_t>{0};
  new (&reg->magic) std::atomic<uint32_t>{0};
  reg->magic.store(kRegistryMagic, std::memory_order_release);

  return Node{std::move(*seg), alloc, reg};
}

std::expected<Node, Error> Node::join(std::string_view name, std::size_t seg_size) noexcept
{
  auto seg = ShmSegment::open(std::string{"/axon_"} + std::string{name}, seg_size);
  if (!seg)
    return std::unexpected(Error::ShmOpenFailed);

  void *base = seg->base();

  ShmAllocator *alloc = nullptr;
  for (int i = 0; i < 100 && !alloc; ++i)
  {
    alloc = ShmAllocator::from(base);
    if (!alloc)
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  if (!alloc)
    return std::unexpected(Error::ShmNotReady);

  auto *reg = reinterpret_cast<RegistryHeader *>(reinterpret_cast<char *>(base) + kRegistryOffset);
  for (int i = 0; i < 100 && reg->magic.load(std::memory_order_acquire) != kRegistryMagic; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  if (reg->magic.load(std::memory_order_acquire) != kRegistryMagic)
    return std::unexpected(Error::ShmNotReady);

  return Node{std::move(*seg), alloc, reg};
}

} // namespace axon
