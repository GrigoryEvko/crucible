# The build launcher.
#
# utils/scripts/build-launcher.py runs in front of each C and C++ compile of
# each build, in front of ccache when ccache is the compiler launcher, and in
# front of each link of an executable or a shared library.  It runs the step,
# writes its CPU time, wall time and peak memory to <output>.cost, and exits
# with the status of the step.  A ccache hit gives a record that says "hit"
# and holds no time.  The launcher also holds the budget of the step: a step
# over the memory error threshold fails, and RLIMIT_CPU stops a step that
# runs away (the docstring of the launcher gives the rules).
#
# This file comes after each block that sets or clears the compiler
# launcher: the ccache block, the PGO block and the quarantine plugin.  It
# puts the launcher at the front of the list that those blocks leave, which
# can be empty.  A configure with -DCMAKE_CXX_COMPILER_LAUNCHER=ccache, as CI
# gives, gets the launcher and then ccache.  It comes before the first
# target, because a target reads CMAKE_<LANG>_COMPILER_LAUNCHER and
# CMAKE_<LANG>_LINKER_LAUNCHER when it is created.  CMake writes no launcher
# into compile_commands.json, so the tools that read the compile database see
# the compiler first.  The try_compile projects of the configure step do not
# get the launcher, because a variable of this directory does not go into
# them.
#
# python3 -S skips the site module, which saves about 5 ms of each step.  A
# host with no python3 builds with no launcher, because a measurement never
# stops a build.  The build then writes no record.
#
# The build kind names the preset of the build for the ledger rows, because
# the sizes and the times depend on it.  The launcher reads it from
# build-kind.txt in the build directory.  The options
# that make the kind come after this file, so a call at the end of the root
# directory writes the file.

find_program(CRUCIBLE_PYTHON3 NAMES python3 DOC "The Python interpreter of the build launcher")
if(NOT CRUCIBLE_PYTHON3)
  message(WARNING "The build launcher utils/scripts/build-launcher.py needs python3, and no python3 is on PATH, so "
                  "no compile and no link writes a cost record.  Install Python 3, or give its path with "
                  "-DCRUCIBLE_PYTHON3=<path>.")
  return()
endif()
set(CRUCIBLE_BUILD_LAUNCHER "${CRUCIBLE_PYTHON3}" -S "${CMAKE_SOURCE_DIR}/utils/scripts/build-launcher.py")
foreach(_crucible_launcher_language C CXX)
  set(CMAKE_${_crucible_launcher_language}_COMPILER_LAUNCHER
      ${CRUCIBLE_BUILD_LAUNCHER} ${CMAKE_${_crucible_launcher_language}_COMPILER_LAUNCHER})
  set(CMAKE_${_crucible_launcher_language}_LINKER_LAUNCHER
      ${CRUCIBLE_BUILD_LAUNCHER} --link ${CMAKE_${_crucible_launcher_language}_LINKER_LAUNCHER})
endforeach()
list(JOIN CMAKE_CXX_COMPILER_LAUNCHER " " _crucible_launcher_text)
message(STATUS "build launcher: ${_crucible_launcher_text}")

function(_crucible_write_build_kind)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _kind)
  if(CMAKE_BUILD_TYPE)
    string(TOLOWER "-${CMAKE_BUILD_TYPE}" _build_type)
    string(APPEND _kind "${_build_type}")
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CRUCIBLE_SANITIZE)
    string(APPEND _kind "-asan")
  endif()
  foreach(_option TSAN UBSAN_STRICT VERIFY ANALYZER)
    if(CRUCIBLE_${_option})
      string(TOLOWER "${_option}" _word)
      string(REPLACE "_" "-" _word "${_word}")
      string(APPEND _kind "-${_word}")
    endif()
  endforeach()
  if(CRUCIBLE_PGO AND NOT CRUCIBLE_PGO STREQUAL "off")
    string(APPEND _kind "-pgo-${CRUCIBLE_PGO}")
  endif()
  file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/build-kind.txt" CONTENT "@_kind@\n" @ONLY)
endfunction()
cmake_language(DEFER CALL _crucible_write_build_kind)
