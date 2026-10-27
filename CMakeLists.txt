cmake_minimum_required(VERSION 3.15)

project(Pumpkin
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal build by default so the plugin loads in both native Apple Silicon
# and Intel builds of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64
# for a faster single-arch dev build.
#
# This must sit immediately after project() and before any target is defined.
# CMake latches the architecture list when the first target is created, and
# setting it later is silently ignored -- you get an arm64-only binary that the
# build log calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and Resolume's plugin interface
# is still OpenGL, so every SDK call warns. Silencing it is not ignoring a
# problem -- there is no alternative API to move to here.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
endif()

option(PUMPKIN_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule under external/.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything the plugin is, as an OBJECT library so the bundle and the
# offline harness are built from exactly the same code.
#
# OBJECT and not STATIC on purpose. CFFGLPluginInfo registers itself from a
# file-scope constructor in PluginEntry.cpp and nothing ever references that
# object by name, so in an archive the linker is entitled to drop the whole
# translation unit -- and the result is a bundle that loads, exports plugMain,
# and reports that it contains no plugins.
# ---------------------------------------------------------------------------
add_library(pumpkin_core OBJECT
    source/Diag.h
    source/Diag.cpp
    source/Random.h
    source/Lantern.h
    source/Lantern.cpp
    source/Geometry.h
    source/Geometry.cpp
    source/Controls.h
    source/Controls.cpp
    source/GLState.h
    source/Shaders.h
    source/Shaders.cpp
    source/Pumpkin.h
    source/Pumpkin.cpp
)

target_include_directories(pumpkin_core PUBLIC source)
target_link_libraries(pumpkin_core PUBLIC ffgl::sdk)
target_compile_definitions(pumpkin_core PUBLIC PUMPKIN_VERSION="${PROJECT_VERSION}")
set_target_properties(pumpkin_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The port is the test card's JavaScript, which rounds every multiply and
# every add. Clang on arm64 fuses a * b + c into one rounding by default, and
# one ulp in a candle's frequency is 1e-7 of brightness after ten years of
# Resolume's clock (pktest --port found it at t = 3.15e8 s). MSVC does not
# contract unless asked.
if(NOT MSVC)
    target_compile_options(pumpkin_core PUBLIC -ffp-contract=off)
endif()

# On Windows/Linux the SDK headers we include pull in <GL/glew.h>. The SDK links
# GLEW privately, so consumers need it too for the include path (and for the
# GLEW_STATIC define the static vcpkg triplet propagates). macOS uses the system
# OpenGL framework and needs none of this.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(pumpkin_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The plugin. SourcePlugin.cpp carries the CFFGLPluginInfo and is listed here,
# not in the core: the harness links the core and must not register a plugin.
# ---------------------------------------------------------------------------
add_library(Pumpkin MODULE source/SourcePlugin.cpp)
target_link_libraries(Pumpkin PRIVATE pumpkin_core)

if(APPLE)
    set_target_properties(Pumpkin PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        OUTPUT_NAME "Pumpkin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Pumpkin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.stoatworks.ffgl.pumpkin"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
    target_link_libraries(Pumpkin PRIVATE
        "-framework OpenGL"
        "-framework Carbon"
        "-framework AppKit"
    )
else()
    set_target_properties(Pumpkin PROPERTIES OUTPUT_NAME "Pumpkin")
endif()

# ---------------------------------------------------------------------------
# Offline harness. Whether the patch is the test card's -- the same face on
# the same tile, the same flicker, the same drawing -- is a measurement, not
# a matter of taste, so this is where the plugin is verified rather than
# merely previewed.
# ---------------------------------------------------------------------------
if(PUMPKIN_BUILD_TOOLS AND APPLE)
    add_executable(pktest tools/pktest/main.cpp)
    # zlib ships with the OS, which is why the PNG reader and writer are a
    # hundred lines here rather than a vendored dependency.
    target_link_libraries(pktest PRIVATE pumpkin_core z
        "-framework OpenGL"
        "-framework AppKit"
    )
    # The test card's own numbers and pictures, read where they are checked in.
    target_compile_definitions(pktest PRIVATE
        PKTEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures")
endif()

# ---------------------------------------------------------------------------
# `cmake --install` drops the bundle straight into the user's Resolume plugin
# folder. Override with -DCMAKE_INSTALL_PREFIX=...
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # honour an explicit prefix
elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS Pumpkin
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)
