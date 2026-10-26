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
  # concrete (Held's own behaviour is tested apart) -- or, with
  # SKIFF_TEST_ERASED, erased, as a program's Debug build has them.
  if(SKIFF_TEST_ERASED)
    set(SKIFF_CONCRETE_COMBINATORS OFF CACHE BOOL "" FORCE)
  else()
    set(SKIFF_CONCRETE_COMBINATORS ON CACHE BOOL "" FORCE)
  endif()
  CPMAddPackage(NAME skiff SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

enable_testing()

# The scene's tests, where the scene is built: they draw through Skia.
if(TARGET skiff::skiff)
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/style.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/tracking.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/model.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/bind.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_wiring.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_mounting_projections.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_tabs_pages.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_styles_named.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_windows.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_rows_arriving.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_row_menus_keys.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_pictures_later.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_swipes_drags.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_menus_badges.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_call_buttons.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_panels.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_said_buttons.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_emoji.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_scopes_clicks.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_wrappers.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_windows_short.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/reconcile.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/rekey.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/reorder.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/views.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/row_layout.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/picker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/images.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/schema.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/takes.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/hook.cc)

# The emoji card under all its wrappers is the heaviest unit of the tests to
# compile (more than a 1 GB box can): built where the host can take it.
option(SKIFF_TESTS_HEAVY "Build the heaviest test units too" ON)
if(SKIFF_TESTS_HEAVY)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/compose_emoji_wrapped.cc)
endif()
# On skiff's stand-in for Skia, nothing is drawn and nothing decoded: the
# tests of pixels and of files of images are Skia's.
if(DEFINED SKIFF_SKIA AND NOT SKIFF_SKIA)
  get_target_property(skiff_test_sources ${PROJECT_NAME} SOURCES)
  list(REMOVE_ITEM skiff_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/images.cc)
  set_target_properties(${PROJECT_NAME} PROPERTIES SOURCES "${skiff_test_sources}")
endif()

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

# A virtual Sections wheeled down, and mux's emoji card -- in a menu, popped
# at the press, laid out provisionally -- wheeled and dragged: each on its
# own, quick to build and run.
add_executable(skiffSectionsWheel ${CMAKE_CURRENT_SOURCE_DIR}/src/sections_wheel.cc)
target_link_libraries(skiffSectionsWheel PRIVATE gtest gtest_main skiff::skiff)
set_target_properties(skiffSectionsWheel PROPERTIES CXX_STANDARD 23 CXX_SCAN_FOR_MODULES ON)
add_test(NAME skiffSectionsWheel COMMAND skiffSectionsWheel)
add_executable(skiffEmojiCard ${CMAKE_CURRENT_SOURCE_DIR}/src/emoji_card.cc)
target_link_libraries(skiffEmojiCard PRIVATE gtest gtest_main skiff::skiff)
set_target_properties(skiffEmojiCard PROPERTIES CXX_STANDARD 23 CXX_SCAN_FOR_MODULES ON)
add_test(NAME skiffEmojiCard COMMAND skiffEmojiCard)
endif()

# The page's tests: skiff.web with its in-memory document, nothing of Skia.
if(TARGET skiff::web)
  add_executable(skiffWebTests ${CMAKE_CURRENT_SOURCE_DIR}/src/web.cc)
  target_link_libraries(skiffWebTests PRIVATE gtest gtest_main skiff::web)
  set_target_properties(skiffWebTests PROPERTIES CXX_STANDARD 23 CXX_SCAN_FOR_MODULES ON)
  add_test(NAME skiffWebTests COMMAND skiffWebTests)
endif()


if(ENABLE_TEST_COVERAGE)
  target_compile_options(skiff PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(skiff PRIVATE -fprofile-arcs -ftest-coverage)
endif()
