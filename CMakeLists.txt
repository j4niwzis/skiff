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

# Where Skia comes from, and everything else this needs.
#
# cmake-everywhere answers find_package: from the machine when the machine
# has the library, and from a pinned source when it does not. Named before
# project(), because that is the only place a dependency provider may be
# installed from -- and only when nothing is answering already, which is the
# case where this library is being built inside somebody else's build and
# that build brought its own.
if(NOT COMMAND cme_declare_port AND NOT CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)
endif()

project(
  skiff
  VERSION 0.1
  LANGUAGES CXX
)

# CMAKE_PROJECT_TOP_LEVEL_INCLUDES is read by the top-level project() call
# and by no other, so a build configured from a subdirectory of this has no
# provider and no sign of it: find_package(Skia) then falls through to
# whatever the machine happens to have, in whatever shape, and what finally
# says something is a compiler error about Skia rather than about this.
if(NOT COMMAND cme_provider)
  message(FATAL_ERROR
    "cmake-everywhere is not installed in this build, so nothing answers "
    "find_package(Skia). CMAKE_PROJECT_TOP_LEVEL_INCLUDES is only read by "
    "the top-level project() call: configure this directory, and not a "
    "subdirectory of it.")
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(SKIFF_EMSCRIPTEN TRUE)
else()
  set(SKIFF_EMSCRIPTEN FALSE)
endif()

# Skia, by the components this library actually reads: it decodes and encodes
# both of those formats, and its font stack is FreeType's. Which GPU backend
# there is -- Ganesh on GL, Graphite on Vulkan, or none -- is not asked for
# here, because this library draws through the canvas and makes no context of
# its own. The program that draws asks for the backend it wants, and a
# program built together with this one is what decides.
#
# A copy of this built on its own has no such program to ask, and what it is
# compiled against is what its consumers get: the module that wraps Skia's
# headers names Ganesh only where the Skia it links has one, so a build that
# is going to be installed and drawn through says which backend that Skia is
# expected to have. Whoever packages this knows -- it is the Skia they are
# packaging beside it.
set(SKIFF_SKIA_COMPONENTS "" CACHE STRING
    "Backend components to ask Skia for, for a copy built on its own")
# What of skiff is built, by component, the way a consumer asks for it:
#
#   cme_find_package(skiff COMPONENTS gl fontmgr-directory png jpeg text-shaping)
#
# Each but text-shaping is one of Skia's features that skiff draws or decodes
# with, asked of Skia only where it is asked of skiff -- so that a Skia
# without it, a system's, is taken as it is, its code left out by Skia's own
# defines. The components are declared below as cme features, so that a
# consumer's COMPONENTS reach this file as CME_FEATURES_skiff; SKIFF_COMPONENTS
# says the same by hand. A build that asks for nothing decodes PNG and JPEG.
set(skiff_components png jpeg gif webp gl fontmgr-directory text-shaping)
if(COMMAND cme_port_feature)
  # Each says which of Skia's features it needs, where a description of
  # skiff is read before skiff is: a build resolves what a port depends on
  # before it reads the port's tree, and a Skia taken for less than was asked
  # of skiff is taken for good -- skiff's own ask for gif, a moment later, is
  # answered by the copy already in the build.
  cme_port_feature(skiff png SUMMARY "PNG, decoded and encoded through Skia"
    DEPENDS "skia[png]")
  cme_port_feature(skiff jpeg SUMMARY "JPEG, decoded and encoded through Skia"
    DEPENDS "skia[jpeg]")
  cme_port_feature(skiff gif SUMMARY "GIF, animated too, decoded through Skia's Wuffs"
    DEPENDS "skia[gif]")
  cme_port_feature(skiff webp SUMMARY "WebP, animated too, decoded through Skia's libwebp"
    DEPENDS "skia[webp]")
  cme_port_feature(skiff gl SUMMARY "Drawing on the GPU, through Skia's Ganesh on GL"
    DEPENDS "skia[gl]")
  cme_port_feature(skiff fontmgr-directory SUMMARY "The fonts read from a directory of them"
    DEPENDS "skia[fontmgr-directory]")
  cme_port_feature(skiff text-shaping
    SUMMARY "Text shaped with HarfBuzz and laid out by Unicode's rules with alef (C++26)")
endif()
set(SKIFF_COMPONENTS "" CACHE STRING
    "The components of skiff to build, as a list; empty for png and jpeg")
set(skiff_asked ${SKIFF_COMPONENTS})
if(DEFINED CME_FEATURES_skiff)
  list(APPEND skiff_asked ${CME_FEATURES_skiff})
endif()
list(REMOVE_DUPLICATES skiff_asked)
foreach(component IN LISTS skiff_asked)
  if(NOT component IN_LIST skiff_components)
    message(FATAL_ERROR "skiff has no component ${component}; it has ${skiff_components}")
  endif()
endforeach()
if(NOT skiff_asked)
  set(skiff_asked png jpeg)
endif()
# Skia's components: freetype always, for text, and those asked for.
set(skiff_skia_components freetype ${skiff_asked})
list(REMOVE_ITEM skiff_skia_components text-shaping)
set(SKIFF_TEXT_SHAPING OFF)
if("text-shaping" IN_LIST skiff_asked)
  set(SKIFF_TEXT_SHAPING ON)
endif()
find_package(Skia REQUIRED
             COMPONENTS ${skiff_skia_components} ${SKIFF_SKIA_COMPONENTS})

# Shaped text, where text-shaping is asked for: HarfBuzz for glyphs, alef
# for Unicode's rules -- bidirectional runs, line breaks, characters. Not by
# default: alef is C++26, so a build with this is too, and a program on C++23
# keeps drawing text as before, one face's run at a time.
if(SKIFF_TEXT_SHAPING)
  cme_declare_port(NAME alef PROVIDES alef
    GITHUB_REPOSITORY j4niwzis/alef
    GIT_TAG ca0dacaab3c6dd5a9dfb3fc4c2322b756f36f744
    LICENSE AGPL-3.0-only
    TARGETS alef)
  cme_options(alef "ALEF_TESTS OFF")
  cme_find_package(alef COMPONENTS grapheme line bidi REQUIRED)
  # A machine's HarfBuzz through pkg-config, read once. The config file it
  # installs makes its targets again each time it is read, and a provider
  # that looks at a system package before answering with it reads it twice
  # -- an error about targets that exist. Without one on the machine, the
  # port builds it from its source, which reads no such file.
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(skiff_harfbuzz QUIET IMPORTED_TARGET GLOBAL harfbuzz)
  endif()
  if(TARGET PkgConfig::skiff_harfbuzz)
    set(skiff_harfbuzz_target PkgConfig::skiff_harfbuzz)
  else()
    if(NOT TARGET harfbuzz::harfbuzz)
      cme_find_package(harfbuzz REQUIRED)
    endif()
    set(skiff_harfbuzz_target harfbuzz::harfbuzz)
  endif()
endif()

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

# The standard skiff is built as: C++26 where text is shaped (alef is
# C++26) or the build asks for it, C++23 otherwise. An aggregate's members
# -- a node's parts, a style's declarations -- are walked by the language's
# binding packs on C++26, by Boost.PFR (as a module) on C++23: the one
# module, skiff.aggregate, from one file or the other.
if(SKIFF_TEXT_SHAPING OR (DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD GREATER_EQUAL 26))
  set(skiff_cxx_standard 26)
  set(skiff_aggregate ${CMAKE_CURRENT_SOURCE_DIR}/src/aggregate/packs.cc)
else()
  set(skiff_cxx_standard 23)
  set(skiff_aggregate ${CMAKE_CURRENT_SOURCE_DIR}/src/aggregate/pfr.cc)
  # The module of Boost.PFR imports std, and CMake gives a target the std
  # module only at C++23 or later: its own target asks for C++20, so the
  # standard it is built at is said here, before it is made.
  if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
  endif()
  cme_find_package(Boost COMPONENTS pfr[modules] REQUIRED)
endif()

# skiff.model: what a program shows, in one place -- the parts of its state
# found by their types, the edits that change them, the events taken by the
# scopes they are sent in. A library of its own, with nothing under it but
# the standard library and skiff.aggregate (and, on C++23, Boost.PFR, which
# that walks members with): a program's state and its rules are tested
# without a window, a Skia, or a scene. skiff.aggregate is built here, and
# skiff has it from here. skiff.bind, in skiff, binds a scene's nodes to it.
add_library(skiff_model STATIC)
add_library(skiff::model ALIAS skiff_model)
set_target_properties(skiff_model PROPERTIES
  EXPORT_NAME model CXX_STANDARD ${skiff_cxx_standard})
target_compile_features(skiff_model PUBLIC cxx_std_${skiff_cxx_standard})
# Its partitions, one directory each, as the nodes' are.
file(GLOB skiff_model_partitions CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.cc")
file(GLOB skiff_bind_partitions CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/bind/*.cc")
file(GLOB skiff_compose_partitions CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/compose/*.cc")
target_sources(skiff_model PUBLIC
  FILE_SET CXX_MODULES FILES ${skiff_aggregate}
                             ${CMAKE_CURRENT_SOURCE_DIR}/src/model.cc
                             ${skiff_model_partitions})
if(skiff_cxx_standard LESS 26)
  # In this build only, as for skiff below: an installed copy finds Boost's
  # pfr again, in its config.
  target_link_libraries(skiff_model PUBLIC $<BUILD_INTERFACE:Boost::pfr>)
endif()

set(skiff_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/skia.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/paint.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scene.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nodes.cc
  ${skiff_node_partitions}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bind.cc
  ${skiff_bind_partitions}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/compose.cc
  ${skiff_compose_partitions}
)

if(SKIFF_EMSCRIPTEN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()

# splice: the variant and the visiting skiff's nodes, events and styles are
# made of. C++23 is all it needs; it takes this build's standard. Its step
# count is alef's where alef is built beside it, so that they share one std.
if(DEFINED ALEF_CONSTEXPR_STEPS)
  set(SPLICE_CONSTEXPR_STEPS ${ALEF_CONSTEXPR_STEPS} CACHE STRING
    "clang's -fconstexpr-steps for splice, as alef's")
endif()
# The one splice, here and in the installed copy's config.
set(skiff_splice_tag 8a7755a335917daf3bbfc54ef8a3a160f8445d15)
cme_declare_port(NAME splice PROVIDES splice
  GITHUB_REPOSITORY j4niwzis/splice
  GIT_TAG ${skiff_splice_tag}
  VERSION 0.4
  LICENSE GPL-3.0-only
  TARGETS splice)
cme_options(splice "SPLICE_TESTS OFF")
cme_find_package(splice 0.4 REQUIRED)

add_library(${PROJECT_NAME} STATIC ${skiff_sources})
add_library(skiff::skiff ALIAS ${PROJECT_NAME})
# In this build only, as Skia and Boost.PFR: an installed copy finds splice
# again, in its config.
target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:splice>)
target_link_libraries(${PROJECT_NAME} PUBLIC skiff_model)
# Outside a release build, the walks over a tree see each child through an
# AnyNode (scene.cc, kErasedWalks): made once, not per type of node. Off,
# every build walks its tree statically, as a release build does.
option(SKIFF_ERASE_WALKS "Outside a release build, walk every child through an AnyNode" ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC
  $<$<AND:$<BOOL:${SKIFF_ERASE_WALKS}>,$<NOT:$<CONFIG:Release>>>:SKIFF_ERASED_WALKS>)

# PUBLIC: anything linking this gets `import skia`, `import skiff.paint`,
# `import skiff.scene` and `import skiff.nodes`.
target_sources(${PROJECT_NAME} PUBLIC
  FILE_SET CXX_MODULES FILES ${skiff_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD ${skiff_cxx_standard})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_${skiff_cxx_standard})
if(skiff_cxx_standard LESS 26)
  # In this build only, as Skia below: Boost.PFR built here as a module is a
  # target of this build directory, and install(EXPORT) refuses one that
  # needs it. An installed copy finds Boost's pfr again, in its config.
  target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:Boost::pfr>)
endif()
if(SKIFF_TEXT_SHAPING)
  target_compile_definitions(${PROJECT_NAME} PUBLIC SKIFF_TEXT_SHAPING)
  target_link_libraries(${PROJECT_NAME} PUBLIC alef::alef ${skiff_harfbuzz_target})
endif()

# Skia as a target rather than as flags and paths. What its headers are cut
# by -- SK_GANESH, SK_GRAPHITE, SK_VULKAN, which codecs are compiled in --
# comes with it, and skia.cc is written against exactly those.
#
# In this build only. A Skia the provider built is a target of this build
# directory, and install(EXPORT) refuses to export something that links it
# -- rightly, since what would be installed is a library whose dependency
# exists nowhere else. So an installed copy names Skia the way a machine can
# answer for it, which is what the config file below writes.
target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:Skia::skia>)
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
    LICENSE AGPL-3.0-only
    # Skia with its font stack, said again where a consumer of skiff will
    # read it. The codecs and the backend are the features' own, above: a
    # description saying png and jpeg here had Skia taken for those alone,
    # before skiff was asked for gif.
    #
    # No backend named: this library draws through the canvas and makes no
    # context of its own, so which backend there is -- Ganesh on GL, Graphite
    # on Vulkan, or none at all -- is decided by the program that draws.
    DEPENDS "skia[freetype]"
    TARGETS skiff::skiff skiff::model
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
  install(TARGETS ${PROJECT_NAME} skiff_model
    EXPORT skiffTargets
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/skiff/modules
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(EXPORT skiffTargets
    NAMESPACE skiff::
    CXX_MODULES_DIRECTORY modules
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/skiff)
  # Everything the exported targets name has to be found again by whoever
  # includes this, in the same words -- and whoever includes this may have no
  # provider at all, which is the case this file is written for: a consumer
  # with nothing but a prefix. So Skia is asked for the way a machine can
  # answer without one, through pkg-config, and a consumer that does have a
  # provider has that call answered by it instead. OpenGL arrives as a
  # target, and a target is a name that means nothing in another build until
  # something makes it -- "links to OpenGL::GL but the target was not
  # found", which is where a consumer of this stopped.
  set(skiff_config "include(CMakeFindDependencyMacro)\n")
  string(APPEND skiff_config
    "find_dependency(PkgConfig)\n"
    "pkg_check_modules(SKIA REQUIRED skia)\n")
  if(NOT ANDROID AND NOT SKIFF_EMSCRIPTEN)
    string(APPEND skiff_config "find_dependency(OpenGL)\n")
  endif()
  # What its module interfaces import and this build made in-tree, found
  # again: splice, and -- built on C++23, its aggregates walked by
  # Boost.PFR -- Boost.PFR as a module. Where cmake-everywhere answers, as
  # this build was answered: splice's port declared as here, and Boost's
  # pfr[modules], which a machine's Boost does not ship, built from its
  # sources, downloaded. Else what the machine has. Never found, a consumer
  # compiling the installed interfaces stopped at "module 'splice' not
  # found" (and, on C++23, at find_dependency(Boost COMPONENTS pfr)).
  string(APPEND skiff_config
    "if(COMMAND cme_find_package)\n"
    "  cme_declare_port(NAME splice PROVIDES splice\n"
    "    GITHUB_REPOSITORY j4niwzis/splice\n"
    "    GIT_TAG ${skiff_splice_tag}\n"
    "    LICENSE GPL-3.0-only\n"
    "    TARGETS splice)\n"
    "  cme_options(splice \"SPLICE_TESTS OFF\")\n"
    "  cme_find_package(splice REQUIRED)\n")
  if(skiff_cxx_standard LESS 26)
    string(APPEND skiff_config "  cme_find_package(Boost COMPONENTS pfr[modules] REQUIRED)\n")
  endif()
  string(APPEND skiff_config "else()\n  find_dependency(splice)\n")
  if(skiff_cxx_standard LESS 26)
    string(APPEND skiff_config "  find_dependency(Boost COMPONENTS pfr)\n")
  endif()
  string(APPEND skiff_config "endif()\n")
  string(APPEND skiff_config
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/skiffTargets.cmake\")\n")
  # And Skia put back on the target, since the exported one carries it only
  # in the build interface. A consumer of this compiles the module interface
  # units that are installed beside it, so it needs Skia's headers, the
  # defines they are cut by, and the library itself -- the first two say
  # which declarations in those headers are real, and getting them wrong is
  # a module that compiles against a different Skia than this was built for.
  get_target_property(skiff_skia_defines Skia::skia INTERFACE_COMPILE_DEFINITIONS)
  if(skiff_skia_defines)
    list(JOIN skiff_skia_defines " " skiff_skia_defines)
    string(APPEND skiff_config
      "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
      "             INTERFACE_COMPILE_DEFINITIONS ${skiff_skia_defines})\n")
  endif()
  string(APPEND skiff_config
    "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
    "             INTERFACE_INCLUDE_DIRECTORIES \${SKIA_INCLUDE_DIRS})\n"
    "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
    "             INTERFACE_LINK_LIBRARIES \${SKIA_LDFLAGS} \${SKIA_LIBRARIES})\n")
  # Twice each: the installed interfaces are compiled by the consumer as a
  # target of their own, and that target takes what it imports from
  # IMPORTED_CXX_MODULES_LINK_LIBRARIES, not from the link interface. Named
  # only in the second, splice was built and linked and its module was still
  # missing from the module map the interfaces were compiled with.
  string(APPEND skiff_config
    "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
    "             INTERFACE_LINK_LIBRARIES splice)\n"
    "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
    "             IMPORTED_CXX_MODULES_LINK_LIBRARIES splice)\n")
  if(skiff_cxx_standard LESS 26)
    string(APPEND skiff_config
      "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
      "             INTERFACE_LINK_LIBRARIES Boost::pfr)\n"
      "set_property(TARGET skiff::skiff APPEND PROPERTY\n"
      "             IMPORTED_CXX_MODULES_LINK_LIBRARIES Boost::pfr)\n")
  endif()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/skiffConfig.cmake" "${skiff_config}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/skiffConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/skiff)
endif()
