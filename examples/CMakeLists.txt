cmake_minimum_required(VERSION 3.21)

function(axon_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE axon::axon)
endfunction()

axon_example(ping_pong)
