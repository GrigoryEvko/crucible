# Toolchain-gcc16-aarch64.cmake: the cross compiler of the patched GCC 16.2.1 for aarch64-linux-gnu.
#
# `python3 utils/bin/setup gcc PREFIX --target aarch64-linux-gnu` builds the
# cross compiler PREFIX/usr/bin/aarch64-linux-gnu-g++-16p, with the binutils
# and the glibc sysroot of aarch64 below PREFIX (THE CROSS COMPILER of
# utils/assay/setup/gcc.py).  The preset syntax-aarch64 of CMakePresets.json
# configures a build directory with this file, and `loop syntax` parses each
# unit of that build with the quarantine plugin (CLAUDE.md section XXII, The
# ratchet).
#
# The order of the prefix:
#   1. CRUCIBLE_GCC16_AARCH64_PREFIX, the install prefix of the cross compiler
#   2. $HOME/.local/gcc16-patched-aarch64.
#
# A plugin of the cross compiler is code of the host, so the patched g++ of the
# host compiles the quarantine plugin (THE COMPILER OF THE PLUGIN of
# utils/tools/quarantine/Quarantine.cmake).  CRUCIBLE_PLUGIN_BUILD_CXX names
# that g++, with the order of cmake/Toolchain-gcc16.cmake: CRUCIBLE_CXX, then
# CRUCIBLE_GCC16_PREFIX, then $HOME/.local/gcc16-patched.
#
# CMake runs a program of a configure check through
# CMAKE_CROSSCOMPILING_EMULATOR: qemu-aarch64-static with the sysroot, when the
# host has it.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{CRUCIBLE_GCC16_AARCH64_PREFIX} AND NOT "$ENV{CRUCIBLE_GCC16_AARCH64_PREFIX}" STREQUAL "")
  set(_crucible_cross_prefix "$ENV{CRUCIBLE_GCC16_AARCH64_PREFIX}")
else()
  set(_crucible_cross_prefix "$ENV{HOME}/.local/gcc16-patched-aarch64")
endif()
set(_crucible_cross_bindir "${_crucible_cross_prefix}/usr/bin")
set(_crucible_cross_triplet "aarch64-linux-gnu")
set(_crucible_cross_cxx "${_crucible_cross_bindir}/${_crucible_cross_triplet}-g++-16p")
set(_crucible_cross_sysroot "${_crucible_cross_prefix}/usr/aarch64-redhat-linux/sys-root/fc44")
if(NOT EXISTS "${_crucible_cross_cxx}")
  message(FATAL_ERROR
    "Crucible cross toolchain: the patched g++ for aarch64 is not at '${_crucible_cross_cxx}'.  Build it with "
    "`python3 utils/bin/setup gcc PREFIX --target aarch64-linux-gnu`, and set CRUCIBLE_GCC16_AARCH64_PREFIX to PREFIX.")
endif()

# The patched g++ of the host, which compiles the quarantine plugin.
if(DEFINED ENV{CRUCIBLE_CXX} AND NOT "$ENV{CRUCIBLE_CXX}" STREQUAL "")
  set(_crucible_host_cxx "$ENV{CRUCIBLE_CXX}")
elseif(DEFINED ENV{CRUCIBLE_GCC16_PREFIX} AND NOT "$ENV{CRUCIBLE_GCC16_PREFIX}" STREQUAL "")
  set(_crucible_host_cxx "$ENV{CRUCIBLE_GCC16_PREFIX}/usr/bin/g++-16p")
else()
  set(_crucible_host_cxx "$ENV{HOME}/.local/gcc16-patched/usr/bin/g++-16p")
endif()
if(NOT EXISTS "${_crucible_host_cxx}")
  message(FATAL_ERROR
    "Crucible cross toolchain: the patched g++ of the host is not at '${_crucible_host_cxx}'.  It compiles the "
    "quarantine plugin of the cross compiler.  Build it with `python3 utils/bin/setup gcc PREFIX`.")
endif()

# Refuse a cross compiler that does not have each fix (cmake/PatchedGccProbe.cmake).
if(NOT CMAKE_IN_TRY_COMPILE)
  include("${CMAKE_CURRENT_LIST_DIR}/PatchedGccProbe.cmake")
  crucible_patched_gcc_problem("${_crucible_cross_cxx}" _crucible_gcc_problem)
  if(NOT _crucible_gcc_problem STREQUAL "")
    message(FATAL_ERROR "${_crucible_gcc_problem}")
  endif()
endif()

set(CMAKE_C_COMPILER   "${_crucible_cross_bindir}/${_crucible_cross_triplet}-gcc-16p" CACHE FILEPATH "Crucible cross gcc-16p")
set(CMAKE_CXX_COMPILER "${_crucible_cross_cxx}" CACHE FILEPATH "Crucible cross g++-16p")
set(CMAKE_AR           "${_crucible_cross_bindir}/${_crucible_cross_triplet}-gcc-ar-16p" CACHE FILEPATH "Crucible cross gcc-ar-16p")
set(CMAKE_NM           "${_crucible_cross_bindir}/${_crucible_cross_triplet}-gcc-nm-16p" CACHE FILEPATH "Crucible cross gcc-nm-16p")
set(CMAKE_RANLIB       "${_crucible_cross_bindir}/${_crucible_cross_triplet}-gcc-ranlib-16p" CACHE FILEPATH "Crucible cross gcc-ranlib-16p")
set(CRUCIBLE_TOOLCHAIN_CXX "${_crucible_cross_cxx}")
set(CRUCIBLE_PLUGIN_BUILD_CXX "${_crucible_host_cxx}")

set(CMAKE_FIND_ROOT_PATH "${_crucible_cross_sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(_crucible_cross_qemu NAMES qemu-aarch64-static qemu-aarch64)
if(_crucible_cross_qemu)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${_crucible_cross_qemu};-L;${_crucible_cross_sysroot}")
endif()

# The libstdc++ of the cross compiler, for a program that the emulator runs.
# The root CMakeLists.txt gives each link this rpath (cmake/ToolchainRunPath.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/ToolchainRunPath.cmake")
crucible_toolchain_run_path("${_crucible_cross_prefix}"
                            "${_crucible_cross_prefix}/usr/${_crucible_cross_triplet}/lib64")
