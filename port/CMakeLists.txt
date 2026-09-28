cmake_minimum_required(VERSION 3.25)
project(smg_port LANGUAGES C CXX OBJC OBJCXX)

# macOS port of Super Mario Galaxy built from the Petari decompilation.
#
#   cmake -S port -B build/port -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build/port
#
# Game data is not included; the game reads it from a disc image (RMGK01) at
# runtime, see port/README.md.

set(PETARI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif ()

# ---------------------------------------------------------------------------
# Aurora (GX/VI/DVD compatibility layer on WebGPU + SDL3)
# ---------------------------------------------------------------------------
set(AURORA_ENABLE_GX ON CACHE BOOL "" FORCE)
set(AURORA_ENABLE_DVD ON CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)

set(AURORA_SOURCE_DIR "" CACHE PATH "Use a local Aurora checkout (with port/patches/aurora applied)")
if (AURORA_SOURCE_DIR)
  add_subdirectory("${AURORA_SOURCE_DIR}" aurora EXCLUDE_FROM_ALL)
else ()
  include(FetchContent)
  file(GLOB AURORA_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/patches/aurora/*.patch")
  FetchContent_Declare(aurora
    GIT_REPOSITORY https://github.com/encounter/aurora.git
    GIT_TAG 0812291
    PATCH_COMMAND git apply ${AURORA_PATCHES}
    UPDATE_DISCONNECTED ON
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(aurora)
endif ()

# ---------------------------------------------------------------------------
# Decompiled game code (Game, JSystem, nw4r, RVLFaceLib)
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake")

set(SMG_GAME_SOURCES
  ${SMG_SOURCES_GAME}
  ${SMG_SOURCES_JSYSTEM}
  ${SMG_SOURCES_NW4R}
  ${SMG_SOURCES_RVLFACELIB}
)
# The game is compiled from a mirror of its source trees in which Japanese
# string literals are Shift-JIS, as sjiswrap does for the Wii build (the game
# data names things in Shift-JIS). See tools/sjis_mirror.py.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SMG_SJIS_DIR "${CMAKE_BINARY_DIR}/sjis")
set(SMG_SJIS_TREES src include libs/JSystem/include libs/RVLFaceLib/include libs/RVL_SDK/include libs/nw4r/include)
set(SMG_SJIS_COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/sjis_mirror.py" "${PETARI_ROOT}"
  "${SMG_SJIS_DIR}" ${SMG_SJIS_TREES})
execute_process(COMMAND ${SMG_SJIS_COMMAND} COMMAND_ERROR_IS_FATAL ANY)
list(TRANSFORM SMG_GAME_SOURCES PREPEND "${SMG_SJIS_DIR}/")
add_custom_target(smg_sjis_mirror
  COMMAND ${SMG_SJIS_COMMAND}
  BYPRODUCTS ${SMG_GAME_SOURCES}
  COMMENT "Mirroring game sources with Shift-JIS literals"
  VERBATIM
)

add_library(smg_game STATIC ${SMG_GAME_SOURCES})
add_dependencies(smg_game smg_sjis_mirror)
target_include_directories(smg_game PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${SMG_SJIS_DIR}/include"
  "${SMG_SJIS_DIR}/libs/JSystem/include"
  "${SMG_SJIS_DIR}/libs/RVLFaceLib/include"
  "${SMG_SJIS_DIR}/libs/RVL_SDK/include"
  "${SMG_SJIS_DIR}/libs/nw4r/include"
)
target_compile_definitions(smg_game PUBLIC TARGET_PC=1 PORT_AURORA=1 VERSION=0)
target_compile_options(smg_game PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/port/prelude.h"
  -fshort-wchar
  # PowerPC's plain char is unsigned (Shift-JIS handling depends on it).
  -funsigned-char
  # The decomp relies on Metrowerks semantics: type punning everywhere and
  # wrapping signed arithmetic.
  -fno-strict-aliasing
  -fwrapv
  # Pointers into block-scoped buffers sometimes outlive the block; Metrowerks
  # never reused that stack space, so don't let clang overlap or kill it.
  "SHELL:-Xclang -disable-lifetime-markers"
  -ffp-contract=on
  -Wno-everything
  -Werror=return-type
  -Wno-c++11-narrowing
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -fno-exceptions -fno-rtti -Wno-register>
  $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
)
# Aurora headers (dolphin/*) are needed by the SDK header routing.
target_link_libraries(smg_game PUBLIC aurora::core aurora::gx aurora::gd aurora::vi aurora::mtx aurora::dvd)

# ---------------------------------------------------------------------------
# Host platform layer + executable
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
)

add_executable(smg MACOSX_BUNDLE ${PORT_SOURCES})
set_target_properties(smg PROPERTIES
  OUTPUT_NAME "Super Mario Galaxy"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.petari.smg"
  MACOSX_BUNDLE_BUNDLE_NAME "Super Mario Galaxy"
)
target_compile_features(smg PRIVATE cxx_std_20)
target_compile_options(smg PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fno-exceptions>)
# The 16-bit wide-string runtime must match the game's -fshort-wchar ABI.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/wchar16.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk_rfl.c"
  PROPERTIES COMPILE_OPTIONS -fshort-wchar)
target_link_libraries(smg PRIVATE smg_game aurora::main fmt::fmt iconv)
target_include_directories(smg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Link every game object, as the original build did (factory tables, static
# registration and the like would otherwise be dropped).
target_link_options(smg PRIVATE "LINKER:-force_load,$<TARGET_FILE:smg_game>" -Wl,-map,${CMAKE_BINARY_DIR}/smg.map)
