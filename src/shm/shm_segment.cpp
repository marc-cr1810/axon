// System headers FIRST — before any axon header that opens namespace axon,
// otherwise std:: resolves to axon::std inside the namespace block.
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <string>
#include <thread>

#include <axon/shm/shm_segment.hpp>
#include <axon/platform/memory.hpp>

using namespace std::chrono_literals;

namespace axon
{

std::expected<ShmSegment, ShmError> ShmSegment::create(std::string_view name, std::size_t size) noexcept
{
  std::string n{name};
  ::shm_unlink(n.c_str()); // remove any stale segment

  int fd = ::shm_open(n.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    return std::unexpected(ShmError::CreateFailed);

  if (::ftruncate(fd, static_cast<off_t>(size)) < 0)
  {
    ::close(fd);
    ::shm_unlink(n.c_str());
    return std::unexpected(ShmError::TruncateFailed);
  }

  void *ptr = alloc_shm_backed(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd);
  ::close(fd);
  if (ptr == MAP_FAILED)
  {
    ::shm_unlink(n.c_str());
    return std::unexpected(ShmError::MmapFailed);
  }

  return ShmSegment{ptr, size, n, /*owner=*/true};
}

std::expected<ShmSegment, ShmError> ShmSegment::open(std::string_view name, std::size_t size) noexcept
{
  std::string n{name};
  int fd = -1;
  for (int i = 0; i < 50 && fd < 0; ++i)
  {
    fd = ::shm_open(n.c_str(), O_RDWR, 0666);
    if (fd < 0)
      std::this_thread::sleep_for(100ms);
  }
  if (fd < 0)
    return std::unexpected(ShmError::OpenFailed);

  void *ptr = alloc_shm_backed(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd);
  ::close(fd);
  if (ptr == MAP_FAILED)
    return std::unexpected(ShmError::MmapFailed);

  return ShmSegment{ptr, size, n, /*owner=*/false};
}

ShmSegment::~ShmSegment() noexcept
{
  if (ptr_)
  {
    ::munmap(ptr_, size_);
    if (owner_)
      ::shm_unlink(name_.c_str());
  }
}

ShmSegment::ShmSegment(ShmSegment &&o) noexcept : ptr_{o.ptr_}, size_{o.size_}, name_{std::move(o.name_)}, owner_{o.owner_}
{
  o.ptr_ = nullptr;
  o.size_ = 0;
  o.owner_ = false;
}

ShmSegment &ShmSegment::operator=(ShmSegment &&o) noexcept
{
  if (this != &o)
  {
    if (ptr_)
    {
      ::munmap(ptr_, size_);
      if (owner_)
        ::shm_unlink(name_.c_str());
    }
    ptr_ = o.ptr_;
    o.ptr_ = nullptr;
    size_ = o.size_;
    o.size_ = 0;
    name_ = std::move(o.name_);
    owner_ = o.owner_;
    o.owner_ = false;
  }
  return *this;
}

} // namespace axon
