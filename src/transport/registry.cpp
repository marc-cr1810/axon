#include <axon/transport/registry.hpp>
// Registry logic is header-only (flat struct in shm).
// This file exists to satisfy CMake's STATIC library requirement
// and for any future non-inline registry operations.
