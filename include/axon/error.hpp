/**
 * @file error.hpp
 * @brief Central error enum and description for the Axon library.
 */
#pragma once

#include <string_view>

namespace axon
{

enum class Error
{
  // Shared memory
  ShmCreateFailed,
  ShmOpenFailed,
  ShmTruncateFailed,
  MmapFailed,
  ShmNotReady,
  // Allocation
  OutOfMemory,
  PayloadTooLarge,
  // Topology
  TopicNotFound,
  RegistryFull,
  SubscriberSlotsFull,
  // Data integrity
  TypeMismatch,
  ChecksumFailed,
  SchemaMismatch,
  // Liveness
  PublisherDead,
  StaleSegment,
  OwnerAlreadyExists,
  // State
  StateNotReady,
  StateTooStale,
  // General
  Timeout,
  NotifierFailed,
};

constexpr std::string_view describe(Error e) noexcept
{
  switch (e)
  {
  case Error::ShmCreateFailed:
    return "Failed to create shared memory segment";
  case Error::ShmOpenFailed:
    return "Failed to open shared memory segment";
  case Error::ShmTruncateFailed:
    return "Failed to size shared memory segment";
  case Error::MmapFailed:
    return "mmap() failed";
  case Error::ShmNotReady:
    return "Shared memory not initialised in time";
  case Error::OutOfMemory:
    return "Shared memory allocator out of space";
  case Error::PayloadTooLarge:
    return "Payload exceeds maximum chunk size";
  case Error::TopicNotFound:
    return "Topic not found in registry";
  case Error::RegistryFull:
    return "Global topic registry is full";
  case Error::SubscriberSlotsFull:
    return "Topic has reached maximum subscribers";
  case Error::TypeMismatch:
    return "Type hash mismatch — wrong type for topic";
  case Error::ChecksumFailed:
    return "Message checksum mismatch";
  case Error::SchemaMismatch:
    return "Schema version or state size mismatch";
  case Error::PublisherDead:
    return "Publisher heartbeat timed out";
  case Error::StaleSegment:
    return "SHM exists but owner process is dead";
  case Error::OwnerAlreadyExists:
    return "A SharedStateOwner already exists for this name";
  case Error::StateNotReady:
    return "Shared state has not been committed yet";
  case Error::StateTooStale:
    return "Shared state is older than the requested max age";
  case Error::Timeout:
    return "Timed out waiting for data";
  case Error::NotifierFailed:
    return "Failed to create eventfd notifier";
  }
  return "Unknown error";
}

} // namespace axon
