# StaleCompiler.cmake: refuse a build directory whose cache keeps a compiler
# that its toolchain file does not resolve, and record the compilers of each
# configure.
#
# CMake keeps the compiler of the first configure of a build directory.  A
# toolchain file of the tree puts its compiler in the cache with no FORCE, so
# a new default prefix, such as the prefix of a new patch of GCC, does not
# reach a build directory that exists.  The build would compile with the old
# compiler, and the probe of the root CMakeLists.txt would refuse it with no
# word about the cause.  Each toolchain file of the tree calls
# crucible_refuse_stale_compilers for its C++ compiler and its C compiler
# after it resolves them.  The gate heals its own build directories before
# this check (THE COMPILER HEAL of utils/assay/loop/gate.py, and `loop heal`).
#
# The function compares real paths, with each symlink resolved, in two ways:
#   * the compiler of the cache with the resolved compiler.  A cached compiler
#     with a file name other than the name of the resolved compiler is a
#     wrapper, such as the afl-g++-fast of `loop fuzz`, and passes.
#   * the real path that the last configure recorded for the resolved compiler
#     (CRUCIBLE_CONFIGURED_CXX_REAL and CRUCIBLE_CONFIGURED_C_REAL) with the
#     real path of the resolved compiler now.  So a symlink that points to a
#     new prefix stops the configure, also when the cache and the toolchain
#     file name the same symlink.  A cache with no record passes.
# After the two checks pass, the configure records the real paths.  A
# try_compile project gets its compiler from the project that runs it, so the
# functions skip each such project.

# The real path of a compiler, or the path itself when no such file exists.
function(crucible_compiler_real_path compiler out_var)
  set(_crucible_real "${compiler}")
  if(EXISTS "${compiler}")
    file(REAL_PATH "${compiler}" _crucible_real)
  endif()
  set(${out_var} "${_crucible_real}" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to the problem of the cache entry ENTRY and of its record
# RECORD, or to an empty text when the two agree with the compiler RESOLVED.
function(crucible_stale_compiler_problem entry record resolved out_var)
  set(${out_var} "" PARENT_SCOPE)
  if(CMAKE_IN_TRY_COMPILE)
    return()
  endif()
  crucible_compiler_real_path("${resolved}" _crucible_resolved_real)
  if(DEFINED CACHE{${entry}})
    set(_crucible_cached "$CACHE{${entry}}")
    get_filename_component(_crucible_cached_name "${_crucible_cached}" NAME)
    get_filename_component(_crucible_resolved_name "${resolved}" NAME)
    crucible_compiler_real_path("${_crucible_cached}" _crucible_cached_real)
    if(_crucible_cached_name STREQUAL _crucible_resolved_name
       AND NOT _crucible_cached_real STREQUAL _crucible_resolved_real)
      string(CONCAT _crucible_problem
        "Crucible toolchain: the build directory '${CMAKE_BINARY_DIR}' keeps ${entry} "
        "'${_crucible_cached}' (real path '${_crucible_cached_real}') from its first configure, and the "
        "toolchain file resolves '${resolved}' (real path '${_crucible_resolved_real}').  CMake does not change "
        "the compiler of a configured build directory.  Configure the directory again with "
        "`cmake --preset PRESET --fresh`, with the preset whose binaryDir is this directory, such as "
        "`cmake --preset default --fresh` for build.")
      set(${out_var} "${_crucible_problem}" PARENT_SCOPE)
      return()
    endif()
  endif()
  if(DEFINED CACHE{${record}} AND NOT "$CACHE{${record}}" STREQUAL _crucible_resolved_real)
    string(CONCAT _crucible_problem
      "Crucible toolchain: the build directory '${CMAKE_BINARY_DIR}' recorded the real path "
      "'$CACHE{${record}}' of ${entry} at its last configure, and the toolchain file now resolves '${resolved}' "
      "(real path '${_crucible_resolved_real}').  The commands of the build name the same path, so Ninja builds "
      "no object again by itself.  Configure the directory again with `cmake --preset PRESET --fresh`, with the "
      "preset whose binaryDir is this directory, and remove its .ninja_log, so each object builds again.")
    set(${out_var} "${_crucible_problem}" PARENT_SCOPE)
  endif()
endfunction()

# Stop the configure when the cache or its record names a C++ compiler or a C
# compiler other than CXX or CC, the compilers that the toolchain file
# resolves.  Else record the real paths of CXX and CC.
function(crucible_refuse_stale_compilers cxx cc)
  if(CMAKE_IN_TRY_COMPILE)
    return()
  endif()
  crucible_stale_compiler_problem(CMAKE_CXX_COMPILER CRUCIBLE_CONFIGURED_CXX_REAL "${cxx}" _crucible_cxx_problem)
  crucible_stale_compiler_problem(CMAKE_C_COMPILER CRUCIBLE_CONFIGURED_C_REAL "${cc}" _crucible_cc_problem)
  if(NOT _crucible_cxx_problem STREQUAL "")
    message(FATAL_ERROR "${_crucible_cxx_problem}")
  endif()
  if(NOT _crucible_cc_problem STREQUAL "")
    message(FATAL_ERROR "${_crucible_cc_problem}")
  endif()
  crucible_compiler_real_path("${cxx}" _crucible_cxx_real)
  crucible_compiler_real_path("${cc}" _crucible_cc_real)
  set(CRUCIBLE_CONFIGURED_CXX_REAL "${_crucible_cxx_real}" CACHE INTERNAL
    "The real path of the C++ compiler that the last configure resolved (cmake/StaleCompiler.cmake)")
  set(CRUCIBLE_CONFIGURED_C_REAL "${_crucible_cc_real}" CACHE INTERNAL
    "The real path of the C compiler that the last configure resolved (cmake/StaleCompiler.cmake)")
endfunction()
