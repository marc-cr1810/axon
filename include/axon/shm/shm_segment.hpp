/**
 * @file shm_segment.hpp
 * @brief RAII wrapper for a POSIX shared memory segment (shm_open + mmap).
 *
 * The creator role creates and owns the segment (will shm_unlink on destruction).
 * The reader role maps an existing segment (will only munmap on destruction).
 */
#pragma once

#include <string>
#include <string_view>
#include <cstddef>
#include <expected>

namespace axon
{

enum class ShmError
{
  CreateFailed,
  OpenFailed,
  TruncateFailed,
  MmapFailed,
  NotReady,
};

class ShmSegment
{
public:
  ShmSegment() = default;

  /// Create and own a new shared memory segment of `size` bytes.
  /// Any existing segment with the same name is unlinked first.
  static std::expected<ShmSegment, ShmError> create(std::string_view name, std::size_t size) noexcept;

  /// Map an existing segment created by another process.
  /// Retries for up to ~5 s before returning ShmError::OpenFailed.
  static std::expected<ShmSegment, ShmError> open(std::string_view name, std::size_t size) noexcept;

  ~ShmSegment() noexcept;

  ShmSegment(ShmSegment &&) noexcept;
  ShmSegment &operator=(ShmSegment &&) noexcept;
  ShmSegment(const ShmSegment &) = delete;
  ShmSegment &operator=(const ShmSegment &) = delete;

  template <typename T> [[nodiscard]] T *as() noexcept
  {
    return static_cast<T *>(ptr_);
  }
  template <typename T> [[nodiscard]] const T *as() const noexcept
  {
    return static_cast<const T *>(ptr_);
  }

  [[nodiscard]] void *base() noexcept
  {
    return ptr_;
  }
  [[nodiscard]] const void *base() const noexcept
  {
    return ptr_;
  }
  [[nodiscard]] std::size_t size() const noexcept
  {
    return size_;
  }
  [[nodiscard]] bool valid() const noexcept
  {
    return ptr_ != nullptr;
  }
  [[nodiscard]] std::string_view name() const noexcept
  {
    return name_;
  }

private:
  ShmSegment(void *ptr, std::size_t size, std::string name, bool owner) noexcept : ptr_{ptr}, size_{size}, name_{std::move(name)}, owner_{owner}
  {
  }

  void *ptr_{nullptr};
  std::size_t size_{0};
  std::string name_;
  bool owner_{false};
};

} // namespace axon
