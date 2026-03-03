cmake_minimum_required(VERSION 3.21)

# ── doctest (single-header, fetched inline) ───────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
FetchContent_MakeAvailable(doctest)

function(axon_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE axon::axon doctest::doctest)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

axon_test(test_shm_allocator)
axon_test(test_atomic_queue)
axon_test(test_channel)
