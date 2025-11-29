cmake_minimum_required(VERSION 3.10)
project(hyperion_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization Flags: Maximum speed, strict warnings.
if(MSVC)
    add_compile_options(/O2 /W4 /permissive-)
else()
    add_compile_options(-O3 -Wall -Wextra -Werror -pthread -march=native)
endif()

set(HDRS
    src/hyperion.hpp
    src/arena.hpp
    src/index.hpp
    src/seqlock.hpp
)

# Target: Hyperion Engine (Sanity Check)
add_executable(hyperion_engine src/main.cpp ${HDRS})
target_include_directories(hyperion_engine PRIVATE src)

# Target: Hyperion Benchmark (Performance Metrics)
add_executable(hyperion_bench src/benchmark.cpp ${HDRS})
target_include_directories(hyperion_bench PRIVATE src)

# Platform Specific Linking
if(NOT MSVC)
    target_link_libraries(hyperion_engine pthread)
    target_link_libraries(hyperion_bench pthread)
endif()