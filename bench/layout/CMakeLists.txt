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

# splice: spl::variant and spl::visit, as skiff's nodes have them -- the
# same tag as skiff's.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)
CPMAddPackage(NAME splice GITHUB_REPOSITORY j4niwzis/splice GIT_TAG 8a7755a335917daf3bbfc54ef8a3a160f8445d15
  OPTIONS "SPLICE_TESTS OFF")

add_library(layout_bench_modules)
target_sources(layout_bench_modules PUBLIC FILE_SET CXX_MODULES FILES
  src/grammar.cc src/tree.cc src/engines.cc)
target_compile_definitions(layout_bench_modules PUBLIC LAYOUT_BENCH_ERASED=${layout_bench_erased})
target_compile_features(layout_bench_modules PUBLIC cxx_std_26)
target_link_libraries(layout_bench_modules PUBLIC splice)

add_executable(layout_bench src/main.cc)
target_link_libraries(layout_bench PRIVATE layout_bench_modules)

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

