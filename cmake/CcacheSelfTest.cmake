# The test of cmake/Ccache.cmake.  ctest runs it as:
#   cmake -DCXX=<compiler> -DWORK=<directory> -P cmake/CcacheSelfTest.cmake
#
# Each case configures a small project in WORK that calls the two macros of
# cmake/Ccache.cmake around project(), as the root CMakeLists.txt does, and it
# reads the compiler launcher that the project keeps.  project() reads the
# environment variable CMAKE_CXX_COMPILER_LAUNCHER, so each case configures a
# project, and no case calls the macros in script mode.
#   - With CRUCIBLE_USE_CCACHE=OFF, a ccache launcher from the environment or
#     from -D goes, and the project keeps no launcher.
#   - A launcher whose program is not named ccache, such as sccache, stays as
#     it is, with no ccache option.
#   - With CRUCIBLE_USE_CCACHE=ON and no launcher, the project finds ccache on
#     PATH, when PATH holds it, and gives it the options of the tree.
cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED CXX OR NOT DEFINED WORK)
  message(FATAL_ERROR "CcacheSelfTest.cmake: give -DCXX=<compiler> and -DWORK=<directory>")
endif()
set(module "${CMAKE_CURRENT_LIST_DIR}/Ccache.cmake")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/project" "${WORK}/bin")
file(WRITE "${WORK}/project/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.27)\n"
  "include(\"${module}\")\n"
  "crucible_ccache_before_project()\n"
  "project(ccache_self_test LANGUAGES CXX)\n"
  "set(CRUCIBLE_REAL_CXX \"\${CMAKE_CXX_COMPILER}\")\n"
  "crucible_ccache_after_project()\n"
  "file(WRITE \"\${CMAKE_BINARY_DIR}/launcher.txt\" \"\${CMAKE_CXX_COMPILER_LAUNCHER}\")\n")
# A launcher that is not ccache.  It runs the compile that it gets.
file(WRITE "${WORK}/bin/sccache" "#!/bin/sh\nexec \"$@\"\n")
file(CHMOD "${WORK}/bin/sccache" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

# Configure the project in WORK/LABEL with the options in ARGN and the launcher
# ENVIRONMENT in the environment (empty for none), and set OUT_VAR to the
# launcher that the project keeps.
function(configured_launcher label environment out_var)
  if(environment STREQUAL "")
    unset(ENV{CMAKE_CXX_COMPILER_LAUNCHER})
  else()
    set(ENV{CMAKE_CXX_COMPILER_LAUNCHER} "${environment}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${WORK}/project" -B "${WORK}/${label}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${CXX}" ${ARGN}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  unset(ENV{CMAKE_CXX_COMPILER_LAUNCHER})
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "CcacheSelfTest: the configure of the case ${label} failed:\n${output}")
  endif()
  file(READ "${WORK}/${label}/launcher.txt" launcher)
  set(${out_var} "${launcher}" PARENT_SCOPE)
endfunction()

set(failures "")
configured_launcher(off_environment ccache launcher -DCRUCIBLE_USE_CCACHE=OFF)
if(NOT launcher STREQUAL "")
  string(APPEND failures "\n  With CRUCIBLE_USE_CCACHE=OFF and ccache in the environment, the launcher is '${launcher}'.")
endif()
configured_launcher(off_option "" launcher -DCRUCIBLE_USE_CCACHE=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
if(NOT launcher STREQUAL "")
  string(APPEND failures "\n  With CRUCIBLE_USE_CCACHE=OFF and -DCMAKE_CXX_COMPILER_LAUNCHER=ccache, the launcher is "
                         "'${launcher}'.")
endif()
configured_launcher(other_launcher "${WORK}/bin/sccache" launcher -DCRUCIBLE_USE_CCACHE=ON)
if(NOT launcher STREQUAL "${WORK}/bin/sccache")
  string(APPEND failures "\n  A launcher named sccache becomes '${launcher}', and it must stay '${WORK}/bin/sccache'.")
endif()
find_program(path_ccache ccache)
if(path_ccache)
  configured_launcher(found "" launcher -DCRUCIBLE_USE_CCACHE=ON)
  list(GET launcher 0 program)
  if(NOT program STREQUAL path_ccache OR NOT launcher MATCHES "base_dir=")
    string(APPEND failures "\n  With CRUCIBLE_USE_CCACHE=ON and no launcher, the launcher is '${launcher}', and it must "
                           "be ${path_ccache} with the options of the tree.")
  endif()
endif()
file(REMOVE_RECURSE "${WORK}")
if(NOT failures STREQUAL "")
  message(FATAL_ERROR "CcacheSelfTest:${failures}")
endif()
message(STATUS "CcacheSelfTest: each case keeps the launcher that CRUCIBLE_USE_CCACHE and the launcher name call for")
