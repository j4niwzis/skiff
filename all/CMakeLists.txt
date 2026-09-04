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

project(SkiffBuildAll LANGUAGES CXX)

include(../cmake/tools.cmake)

enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test
                 ${CMAKE_BINARY_DIR}/test)
