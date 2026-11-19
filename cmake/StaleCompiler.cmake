# StaleCompiler.cmake: refuse a build directory whose cache keeps a compiler
# that its toolchain file does not resolve.
#
# CMake keeps the compiler of the first configure of a build directory.  A
# toolchain file of the tree puts its compiler in the cache with no FORCE, so
# a new default prefix, such as the prefix of a new patch of GCC, does not
# reach a build directory that exists.  The build would compile with the old
# compiler, and the probe of the root CMakeLists.txt would refuse it with no
# word about the cause.  Each toolchain file of the tree calls this function
# for its C++ compiler and its C compiler after it resolves them.  The gate
# heals its own build directories before this check (THE COMPILER HEAL of
# utils/assay/loop/gate.py).
#
# The function compares the two real paths, with each symlink resolved.  A
# cached compiler with a file name other than the name of the resolved
# compiler is a wrapper, such as the afl-g++-fast of `loop fuzz`, and the
# function accepts it.  A try_compile project gets its compiler from the
# project that runs it, so the function accepts each such project.

# Set OUT_VAR to the problem of the cache entry ENTRY, or to an empty text when
# the entry is not in the cache or names the compiler RESOLVED.
function(crucible_stale_compiler_problem entry resolved out_var)
  set(${out_var} "" PARENT_SCOPE)
  if(CMAKE_IN_TRY_COMPILE OR NOT DEFINED CACHE{${entry}})
    return()
  endif()
  set(_crucible_cached "$CACHE{${entry}}")
  get_filename_component(_crucible_cached_name "${_crucible_cached}" NAME)
  get_filename_component(_crucible_resolved_name "${resolved}" NAME)
  if(NOT _crucible_cached_name STREQUAL _crucible_resolved_name)
    return()
  endif()
  # A cached compiler that no longer exists keeps its path, which differs from
  # the real path of each resolved compiler that exists.
  set(_crucible_cached_real "${_crucible_cached}")
  if(EXISTS "${_crucible_cached}")
    file(REAL_PATH "${_crucible_cached}" _crucible_cached_real)
  endif()
  set(_crucible_resolved_real "${resolved}")
  if(EXISTS "${resolved}")
    file(REAL_PATH "${resolved}" _crucible_resolved_real)
  endif()
  if(_crucible_cached_real STREQUAL _crucible_resolved_real)
    return()
  endif()
  string(CONCAT _crucible_problem
    "Crucible toolchain: the build directory '${CMAKE_BINARY_DIR}' keeps ${entry} "
    "'${_crucible_cached}' (real path '${_crucible_cached_real}') from its first configure, and the toolchain "
    "file resolves '${resolved}' (real path '${_crucible_resolved_real}').  CMake does not change the compiler of "
    "a configured build directory.  Configure the directory again with `cmake --preset PRESET --fresh`, with the "
    "preset whose binaryDir is this directory, such as `cmake --preset default --fresh` for build.")
  set(${out_var} "${_crucible_problem}" PARENT_SCOPE)
endfunction()

# Stop the configure when the cache keeps a C++ compiler or a C compiler other
# than CXX or CC, the compilers that the toolchain file resolves.
function(crucible_refuse_stale_compilers cxx cc)
  crucible_stale_compiler_problem(CMAKE_CXX_COMPILER "${cxx}" _crucible_cxx_problem)
  crucible_stale_compiler_problem(CMAKE_C_COMPILER "${cc}" _crucible_cc_problem)
  if(NOT _crucible_cxx_problem STREQUAL "")
    message(FATAL_ERROR "${_crucible_cxx_problem}")
  endif()
  if(NOT _crucible_cc_problem STREQUAL "")
    message(FATAL_ERROR "${_crucible_cc_problem}")
  endif()
endfunction()
