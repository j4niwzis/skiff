cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_EXTENSIONS OFF)

# skiff -- a small retained-mode UI framework on Skia, in the shape
# osu!framework gives its drawables, sized for software rasterisation.
#
#   skia         a module wrapper over the Skia headers, so nothing else in
#                the tree needs a global module fragment
#   skiff.paint  the font stack with fallbacks, the canvas painter, easings
#   skiff.scene  the scene graph: anchors, origins, relative and automatic
#                sizing, transforms, hit testing, damage tracking
#   skiff.nodes  the drawables built on it: boxes, text, sprites, flows,
#                scroll and cached containers, clickable areas
#
# Nothing in here knows what a beatmap is, or what osu! is. This is meant to
# be lifted into a repository of its own; keeping it as a project rather than
# a directory is what makes that a move rather than a rewrite.
project(
  skiff
  VERSION 0.1
  LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(SKIFF_EMSCRIPTEN TRUE)
else()
  set(SKIFF_EMSCRIPTEN FALSE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SKIA REQUIRED skia)

# GL, where the Skia this links has a Ganesh in it. Nothing here calls a GL
# function: the header is included for the constant that names a texture
# format, and the libraries are on the interface because a program drawing
# through Ganesh needs them. A Skia built for Graphite on Vulkan and nothing
# else needs none of it, and a machine with such a Skia need not have GL
# development files at all -- so this is looked for rather than demanded, and
# what is not there is not linked.
if(ANDROID)
  find_library(SKIFF_EGL_LIBRARY NAMES EGL)
  find_library(SKIFF_GLES_LIBRARY NAMES GLESv3 GLESv2)
elseif(NOT SKIFF_EMSCRIPTEN)
  find_package(OpenGL)
endif()

# The primary interface unit first, then its partitions. Globbed per
# directory rather than recursively, so that adding a drawable is adding a
# file and nothing else.
file(GLOB skiff_node_partitions CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/nodes/*.cc")

set(skiff_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/skia.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/paint.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scene.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nodes.cc
  ${skiff_node_partitions}
)

if(SKIFF_EMSCRIPTEN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()

add_library(${PROJECT_NAME} STATIC ${skiff_sources})
add_library(skiff::skiff ALIAS ${PROJECT_NAME})

# PUBLIC: anything linking this gets `import skia`, `import skiff.paint`,
# `import skiff.scene` and `import skiff.nodes`.
target_sources(${PROJECT_NAME} PUBLIC
  FILE_SET CXX_MODULES FILES ${skiff_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

# skia.cc is the only thing here that reaches for headers, and it wants both
# Skia's and the GL one it declares SK_GL against.
target_include_directories(${PROJECT_NAME} PUBLIC ${SKIA_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PUBLIC
  ${SKIA_LDFLAGS} ${SKIA_LIBRARIES}
)
if(ANDROID AND SKIFF_EGL_LIBRARY AND SKIFF_GLES_LIBRARY)
  target_link_libraries(${PROJECT_NAME} PUBLIC
    ${SKIFF_EGL_LIBRARY} ${SKIFF_GLES_LIBRARY})
elseif(NOT SKIFF_EMSCRIPTEN AND TARGET OpenGL::GL)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenGL::GL)
endif()

if(SKIFF_EMSCRIPTEN)
  target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions -pthread -msimd128)
endif()

# ---- What this library is, said by the library ----
#
# A consumer writes find_package(skiff) and this says the rest: where it
# comes from, what it needs, and what it produces. The declaration is
# installed beside the library, under share/cmake-everywhere/ports, so a
# prefix that has skiff has the description of skiff -- which is what lets a
# machine answer for it instead of everyone fetching it again.
if(COMMAND cme_declare_port)
  cme_declare_port(
    NAME skiff
    PROVIDES skiff
    VERSION ${PROJECT_VERSION}
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG main
    LICENSE AGPL-3.0-or-later
    # Asked for by pkg-config above, and named here so that whoever answers
    # for skiff answers for Skia the same way.
    #
    # No backend named: this library draws through the canvas and makes no
    # context of its own, so which backend there is -- Ganesh on GL, Graphite
    # on Vulkan, or none at all -- is decided by the program that draws.
    DEPENDS "skia"
    TARGETS skiff::skiff
    CHECK_HEADER skiff/skiff.modules
  )
endif()

# ---- Installing it ----
#
# The module interface units go with it: a consumer of a C++20 module library
# compiles its own binary module interface from the source, so an installed
# library that keeps its sources to itself cannot be consumed at all.
# Installing is what a build of this library does; a build that embeds it
# does not.
#
# Inside another build its dependencies are that build's targets -- Skia
# comes from a port, as skia_skia -- and exporting a target that links one of
# those is an error: "requires target skia_skia that is not in any export
# set". Which is right. What would be installed there is a library whose
# dependencies exist only in the build directory it came from.
option(SKIFF_INSTALL "Install skiff and its CMake package"
       ${PROJECT_IS_TOP_LEVEL})
if(SKIFF_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS ${PROJECT_NAME}
    EXPORT skiffTargets
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/skiff/modules
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(EXPORT skiffTargets
    NAMESPACE skiff::
    CXX_MODULES_DIRECTORY modules
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/skiff)
  # Everything the exported targets name has to be found again by whoever
  # includes this, in the same words. Skia arrives as flags and paths from
  # pkg-config, so nothing has to be found for it beyond pkg-config itself;
  # OpenGL arrives as a target, and a target is a name that means nothing in
  # another build until something makes it -- "links to OpenGL::GL but the
  # target was not found", which is where a consumer of this stopped.
  set(skiff_config "include(CMakeFindDependencyMacro)\n")
  string(APPEND skiff_config
    "find_dependency(PkgConfig)\n"
    "pkg_check_modules(SKIA REQUIRED skia)\n")
  if(NOT ANDROID AND NOT SKIFF_EMSCRIPTEN)
    string(APPEND skiff_config "find_dependency(OpenGL)\n")
  endif()
  string(APPEND skiff_config
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/skiffTargets.cmake\")\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/skiffConfig.cmake" "${skiff_config}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/skiffConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/skiff)
endif()
