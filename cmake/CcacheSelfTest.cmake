# The test of cmake/Ccache.cmake.  ctest runs it as:
#   cmake -DCXX=<compiler> -DWORK=<directory> -DCASE=<launcher|relocation> -P cmake/CcacheSelfTest.cmake
#
# Each case configures a small project in WORK that calls the macros of
# cmake/Ccache.cmake around project(), as the root CMakeLists.txt does.
# project() reads the environment variable CMAKE_CXX_COMPILER_LAUNCHER, so
# each case configures a project, and no case calls the macros in script mode.
#
# CASE=launcher reads the compiler launcher that the project keeps.
#   - With CRUCIBLE_USE_CCACHE=OFF, a ccache launcher from the environment or
#     from -D goes, and the project keeps no launcher.
#   - A launcher whose program is not named ccache, such as sccache, stays as
#     it is, with no ccache option.
#   - With CRUCIBLE_USE_CCACHE=ON and no launcher, the project finds ccache on
#     PATH, when PATH holds it, and gives it the options of the tree.
#
# CASE=relocation builds the object of one source with debug information in
# three build roots inside the source root.  The first two use the ccache that
# PATH holds, with a cache of their own in WORK, so the second gets a hit from
# the first.  These two objects must be the same, and neither can name WORK.
# The third uses no ccache, and its object cannot name its build root.  The
# compile without ccache gets the absolute path of the source, so its __FILE__
# names WORK.  Without ccache on PATH, the case builds only the third.
cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED CXX OR NOT DEFINED WORK)
  message(FATAL_ERROR "CcacheSelfTest.cmake: give -DCXX=<compiler> and -DWORK=<directory>")
endif()
if(NOT DEFINED CASE)
  set(CASE launcher)
endif()
set(module "${CMAKE_CURRENT_LIST_DIR}/Ccache.cmake")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/project/include" "${WORK}/bin")
file(WRITE "${WORK}/project/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.27)\n"
  "include(\"${module}\")\n"
  "crucible_ccache_before_project()\n"
  "project(ccache_self_test LANGUAGES CXX)\n"
  "set(CRUCIBLE_REAL_CXX \"\${CMAKE_CXX_COMPILER}\")\n"
  "crucible_ccache_after_project()\n"
  "crucible_relocatable_objects()\n"
  "file(WRITE \"\${CMAKE_BINARY_DIR}/launcher.txt\" \"\${CMAKE_CXX_COMPILER_LAUNCHER}\")\n"
  "add_library(probe OBJECT probe.cpp)\n"
  "target_include_directories(probe PRIVATE include)\n")
file(WRITE "${WORK}/project/include/probe.h" "#pragma once\ninline int twice(int value) { return 2 * value; }\n")
file(WRITE "${WORK}/project/probe.cpp"
  "#include <probe.h>\n"
  "int probe_twice(int value) { return twice(value); }\n"
  "const char* probe_file() { return __FILE__; }\n")
# A launcher that is not ccache.  It runs the compile that it gets.
file(WRITE "${WORK}/bin/sccache" "#!/bin/sh\nexec \"$@\"\n")
file(CHMOD "${WORK}/bin/sccache" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

# Configure the project in DIRECTORY with the options in ARGN and the launcher
# ENVIRONMENT in the environment (empty for none), and set OUT_VAR to the
# launcher that the project keeps.
function(configured_launcher directory environment out_var)
  if(environment STREQUAL "")
    unset(ENV{CMAKE_CXX_COMPILER_LAUNCHER})
  else()
    set(ENV{CMAKE_CXX_COMPILER_LAUNCHER} "${environment}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${WORK}/project" -B "${directory}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${CXX}" ${ARGN}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  unset(ENV{CMAKE_CXX_COMPILER_LAUNCHER})
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "CcacheSelfTest: the configure of ${directory} failed:\n${output}")
  endif()
  file(READ "${directory}/launcher.txt" launcher)
  set(${out_var} "${launcher}" PARENT_SCOPE)
endfunction()

set(failures "")
find_program(path_ccache ccache)
if(CASE STREQUAL "launcher")
  configured_launcher("${WORK}/off_environment" ccache launcher -DCRUCIBLE_USE_CCACHE=OFF)
  if(NOT launcher STREQUAL "")
    string(APPEND failures "\n  With CRUCIBLE_USE_CCACHE=OFF and ccache in the environment, the launcher is "
                           "'${launcher}'.")
  endif()
  configured_launcher("${WORK}/off_option" "" launcher -DCRUCIBLE_USE_CCACHE=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  if(NOT launcher STREQUAL "")
    string(APPEND failures "\n  With CRUCIBLE_USE_CCACHE=OFF and -DCMAKE_CXX_COMPILER_LAUNCHER=ccache, the launcher "
                           "is '${launcher}'.")
  endif()
  configured_launcher("${WORK}/other_launcher" "${WORK}/bin/sccache" launcher -DCRUCIBLE_USE_CCACHE=ON)
  if(NOT launcher STREQUAL "${WORK}/bin/sccache")
    string(APPEND failures "\n  A launcher named sccache becomes '${launcher}', and it must stay "
                           "'${WORK}/bin/sccache'.")
  endif()
  if(path_ccache)
    configured_launcher("${WORK}/found" "" launcher -DCRUCIBLE_USE_CCACHE=ON)
    list(GET launcher 0 program)
    if(NOT program STREQUAL path_ccache OR NOT launcher MATCHES "base_dir=")
      string(APPEND failures "\n  With CRUCIBLE_USE_CCACHE=ON and no launcher, the launcher is '${launcher}', and it "
                             "must be ${path_ccache} with the options of the tree.")
    endif()
  endif()
elseif(CASE STREQUAL "relocation")
  file(WRITE "${WORK}/ccache.conf" "")
  set(ENV{CCACHE_DIR} "${WORK}/ccache")
  set(ENV{CCACHE_CONFIGPATH} "${WORK}/ccache.conf")
  # Build the probe in WORK/project/LABEL, and set OUT_VAR to its object.
  function(built_object label use_ccache out_var)
    set(directory "${WORK}/project/${label}")
    configured_launcher("${directory}" "" launcher -DCMAKE_BUILD_TYPE=Debug -DCRUCIBLE_USE_CCACHE=${use_ccache})
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --build "${directory}" --target probe
      RESULT_VARIABLE code
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output)
    if(NOT code EQUAL 0)
      message(FATAL_ERROR "CcacheSelfTest: the build of ${directory} failed:\n${output}")
    endif()
    file(GLOB_RECURSE object "${directory}/CMakeFiles/probe.dir/*.o")
    set(${out_var} "${object}" PARENT_SCOPE)
  endfunction()
  # Append a failure when the object OBJECT holds the text of PATH.
  function(expect_unnamed object path)
    file(READ "${object}" object_hex HEX)
    string(HEX "${path}" path_hex)
    string(FIND "${object_hex}" "${path_hex}" position)
    if(NOT position EQUAL -1)
      string(APPEND failures "\n  The object ${object} names ${path}.")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
  endfunction()
  if(path_ccache)
    built_object(build_one ON miss_object)
    built_object(build_two ON hit_object)
    file(SHA256 "${miss_object}" miss_hash)
    file(SHA256 "${hit_object}" hit_hash)
    if(NOT miss_hash STREQUAL hit_hash)
      string(APPEND failures "\n  The object ${hit_object} is not the same as ${miss_object}.")
    endif()
    expect_unnamed("${miss_object}" "${WORK}")
    expect_unnamed("${hit_object}" "${WORK}")
  endif()
  built_object(build_three OFF plain_object)
  expect_unnamed("${plain_object}" "${WORK}/project/build_three")
else()
  message(FATAL_ERROR "CcacheSelfTest.cmake: CASE is '${CASE}', and the cases are launcher and relocation")
endif()
file(REMOVE_RECURSE "${WORK}")
if(NOT failures STREQUAL "")
  message(FATAL_ERROR "CcacheSelfTest:${failures}")
endif()
message(STATUS "CcacheSelfTest: the case ${CASE} gives the expected result")
