/**
 * @file axon.hpp
 * @brief Convenience single-header include for the Axon IPC library.
 */
#pragma once

#include <axon/error.hpp>
#include <axon/platform/cpu.hpp>
#include <axon/platform/clock.hpp>
#include <axon/platform/memory.hpp>
#include <axon/sync/spinlock.hpp>
#include <axon/sync/seqlock.hpp>
#include <axon/sync/atomic_queue.hpp>
#include <axon/sync/notifier.hpp>
#include <axon/shm/shm_segment.hpp>
#include <axon/shm/shm_allocator.hpp>
#include <axon/transport/chunk.hpp>
#include <axon/transport/registry.hpp>
#include <axon/transport/channel.hpp>
#include <axon/node.hpp>
#include <axon/publisher.hpp>
#include <axon/subscriber.hpp>
#include <axon/shared_state.hpp>
