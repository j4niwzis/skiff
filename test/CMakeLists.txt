cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/import-std.cmake)

# The same provider the library asks Skia through. This directory is a
# project of its own and configures the library as a subproject, so the
# top-level project() here is the one that installs it -- and a provider is
# only installed by the top-level one.
if(NOT COMMAND cme_declare_port AND NOT CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
      ${CMAKE_CURRENT_LIST_DIR}/../cmake/get_cme.cmake)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

project(skiffTests LANGUAGES C CXX)

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the installed skiff package" OFF)

include(../cmake/tools.cmake)
include(../cmake/CPM.cmake)

CPMAddPackage(
  NAME googletest_modules
  GITHUB_REPOSITORY j4niwzis/googletest-modules
  GIT_TAG 706920f1d230f0316d271466d8885735be3e300e
  OPTIONS
    "BUILD_GMOCK OFF"
    "gtest_build_modules ON"
    "gtest_build_tests OFF"
    "gtest_build_samples OFF"
)

if(TEST_INSTALLED_VERSION)
  find_package(skiff REQUIRED)
else()
  # The widgets' members called on what their combinators made: kept
  # concrete (Held's own behaviour is tested apart).
  set(SKIFF_CONCRETE_COMBINATORS ON CACHE BOOL "" FORCE)
  CPMAddPackage(NAME skiff SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/style.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/tracking.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/model.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/bind.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/compose.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/reconcile.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/rekey.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/reorder.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/views.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/row_layout.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/picker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/images.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/schema.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/takes.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/hook.cc)

target_link_libraries(${PROJECT_NAME} PRIVATE
  gtest
  gtest_main
  skiff::skiff)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 23
  CXX_SCAN_FOR_MODULES ON)

enable_testing()
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# Keep pointer-composition regressions quick to build and run on their own.
add_executable(skiffClickTests ${CMAKE_CURRENT_SOURCE_DIR}/src/click.cc)
target_link_libraries(skiffClickTests PRIVATE gtest gtest_main skiff::skiff)
set_target_properties(skiffClickTests PROPERTIES CXX_STANDARD 23 CXX_SCAN_FOR_MODULES ON)
add_test(NAME skiffClickTests COMMAND skiffClickTests)


if(ENABLE_TEST_COVERAGE)
  target_compile_options(skiff PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(skiff PRIVATE -fprofile-arcs -ftest-coverage)
endif()
