# The layout schedule benchmark: box layout said as an attribute grammar,
# its passes checked while compiling, and run three ways -- all again, two
# dirty bits, a queue in the tree's order -- over a chat as it is edited.
# A project of its own, with no Skia: built and run where skiff is not.
#
#   cmake -S bench/layout -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-bench && build-bench/layout_bench
#
# A Debug build reaches the kinds through tables, as skiff's erased walks do.
cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/import-std.cmake)
# skiff's dependencies come through cmake-everywhere, as skiff's tests have them.
if(NOT COMMAND cme_declare_port AND NOT CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../cmake/get_cme.cmake)
endif()
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
project(layout_bench LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(layout_bench_erased 1)
else()
  set(layout_bench_erased 0)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)
# skiff itself, for the Google Benchmark cases on its real scene: without
# Skia (its skia-lite stand-in: real geometry, text measured, nothing
# drawn), without the web backend, without text shaping.
option(LAYOUT_BENCH_SKIFF "Build the Google Benchmark cases on skiff's own scene" ON)
if(LAYOUT_BENCH_SKIFF)
  set(SKIFF_SKIA OFF CACHE BOOL "" FORCE)
  set(SKIFF_WEB OFF CACHE BOOL "" FORCE)
  set(SKIFF_INSTALL OFF CACHE BOOL "" FORCE)
  set(SKIFF_TEXT_SHAPING OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. skiff EXCLUDE_FROM_ALL)
endif()
# splice: spl::variant and spl::visit, as skiff's nodes have them -- skiff's
# own where skiff is built, else the same tag.
if(NOT TARGET splice)
  CPMAddPackage(NAME splice GITHUB_REPOSITORY j4niwzis/splice GIT_TAG 8a7755a335917daf3bbfc54ef8a3a160f8445d15
    OPTIONS "SPLICE_TESTS OFF")
endif()

add_library(layout_bench_modules)
target_sources(layout_bench_modules PUBLIC FILE_SET CXX_MODULES FILES
  src/grammar.cc src/tree.cc src/engines.cc src/edits.cc)
target_compile_definitions(layout_bench_modules PUBLIC LAYOUT_BENCH_ERASED=${layout_bench_erased})
target_compile_features(layout_bench_modules PUBLIC cxx_std_26)
target_link_libraries(layout_bench_modules PUBLIC splice)

add_executable(layout_bench src/main.cc)
target_link_libraries(layout_bench PRIVATE layout_bench_modules)

# The same on Google Benchmark, with skiff's own scene beside the engines.
if(LAYOUT_BENCH_SKIFF)
  CPMAddPackage(NAME benchmark VERSION 1.9.4 URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
    OPTIONS "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_INSTALL OFF" "BENCHMARK_ENABLE_WERROR OFF")
  # A C++17 library: no std module for it (CMAKE_CXX_MODULE_STD is on for
  # everything here).
  # And its own -pedantic-errors meets clang 23's word on __COUNTER__.
  set_target_properties(benchmark benchmark_main PROPERTIES CXX_MODULE_STD OFF)
  target_compile_options(benchmark PRIVATE -Wno-c2y-extensions)
  target_compile_options(benchmark_main PRIVATE -Wno-c2y-extensions)
  add_executable(layout_gbench src/gbench.cc)
  target_link_libraries(layout_gbench PRIVATE layout_bench_modules skiff::skiff benchmark::benchmark)
endif()

# What the schedule's check costs while compiling: the same file with 16, 64
# and 256 kinds, checked and not. compile_time.sh builds each and times it.
foreach(kinds 16 64 256)
  foreach(checked 0 1)
    add_executable(layout_compile_k${kinds}_c${checked} src/compile_kinds.cc)
    target_link_libraries(layout_compile_k${kinds}_c${checked} PRIVATE layout_bench_modules)
    target_compile_definitions(layout_compile_k${kinds}_c${checked} PRIVATE LAYOUT_BENCH_KINDS=${kinds} LAYOUT_BENCH_CHECK=${checked})
    set_target_properties(layout_compile_k${kinds}_c${checked} PROPERTIES EXCLUDE_FROM_ALL ON)
  endforeach()
endforeach()

