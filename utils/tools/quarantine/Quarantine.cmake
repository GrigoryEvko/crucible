# The quarantine plugin of GCC: CRUCIBLE_QUARANTINE = OFF | REPORT | ERROR
#
# Every source file outside include/foundation/ and include/fixy/ is
# quarantined.  The head of quarantine.cpp says what the plugin reports.  OFF,
# the default, adds nothing to a compile line.  REPORT and ERROR build the
# plugin at configure time with the compiler of the build, and load it into
# every C++ compile of the tree:
#
#   REPORT  Each translation unit writes its findings to one file in
#           ${CMAKE_BINARY_DIR}/quarantine/report/.  A finding never stops
#           the build.  The compiler launcher is not used, because a cache
#           hit skips the compile and its report.
#   ERROR   Each finding that no opt-out region covers is a compile error.
#
# The location rule decides what the plugin checks, and the target does not.
# So the flags go on the directory before the first target, and every target
# of the tree gets them.  execute_process builds the plugin, so the plugin is
# not a target and it is not built with itself loaded.  A change of the plugin
# source or of the admitted list configures again.  The stamp argument then
# changes each compile line, and each object compiles again.
#
# The root CMakeLists.txt includes this file after the PGO block, which can
# also clear the compiler launcher, and before the first target.

set(CRUCIBLE_QUARANTINE "OFF" CACHE STRING "The quarantine plugin of GCC: OFF, REPORT or ERROR")
set_property(CACHE CRUCIBLE_QUARANTINE PROPERTY STRINGS OFF REPORT ERROR)
if(NOT CRUCIBLE_QUARANTINE MATCHES "^(OFF|REPORT|ERROR)$")
  message(FATAL_ERROR "CRUCIBLE_QUARANTINE is '${CRUCIBLE_QUARANTINE}'. The values are OFF, REPORT and ERROR.")
endif()

set(CRUCIBLE_QUARANTINE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/quarantine.cpp")
set(CRUCIBLE_QUARANTINE_ADMITTED "${CMAKE_SOURCE_DIR}/utils/scripts/quarantine-admitted-std.txt")

# The flags that build the plugin.  GCC is built without RTTI, and the plugin
# loads into cc1plus, so it finds the libstdc++ of the compiler through an
# rpath.  The test below builds its own copy with the same flags.
execute_process(
  COMMAND "${CRUCIBLE_REAL_CXX}" -print-file-name=plugin
  OUTPUT_VARIABLE _crucible_quarantine_gcc_plugin_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND "${CRUCIBLE_REAL_CXX}" -print-file-name=libstdc++.so
  OUTPUT_VARIABLE _crucible_quarantine_libstdcxx
  OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(_crucible_quarantine_library_dir "${_crucible_quarantine_libstdcxx}" DIRECTORY)
set(CRUCIBLE_QUARANTINE_PLUGIN_FLAGS
  -std=gnu++17 -fno-rtti -fPIC -shared -O2 -g -Wall -Wextra -Werror
  -isystem "${_crucible_quarantine_gcc_plugin_dir}/include"
  "-Wl,-rpath,${_crucible_quarantine_library_dir}")

# The test of the plugin.  It runs in every configuration, because it builds
# its own copy of the plugin in a temporary directory.
add_test(NAME quarantine_plugin
  COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/test/check_plugin.py"
          --cxx "${CRUCIBLE_REAL_CXX}"
          --source "${CRUCIBLE_QUARANTINE_SOURCE}"
          --admitted "${CRUCIBLE_QUARANTINE_ADMITTED}"
          -- ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS})
set_tests_properties(quarantine_plugin PROPERTIES LABELS "ci_guard")

if(CRUCIBLE_QUARANTINE STREQUAL "OFF")
  return()
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "CRUCIBLE_QUARANTINE loads a GCC plugin. The compiler is ${CMAKE_CXX_COMPILER_ID}.")
endif()
if(NOT EXISTS "${_crucible_quarantine_gcc_plugin_dir}/include/gcc-plugin.h")
  message(FATAL_ERROR "CRUCIBLE_QUARANTINE: '${CRUCIBLE_REAL_CXX} -print-file-name=plugin' gave "
    "'${_crucible_quarantine_gcc_plugin_dir}', which holds no include/gcc-plugin.h. Build the compiler "
    "with plugin support, or set CRUCIBLE_QUARANTINE=OFF.")
endif()

# The plugin must match the cc1plus that loads it, so the compiler is part of
# the key that decides whether the plugin is current.
execute_process(
  COMMAND "${CRUCIBLE_REAL_CXX}" -print-prog-name=cc1plus
  OUTPUT_VARIABLE _crucible_quarantine_cc1plus
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(_crucible_quarantine_out "${CMAKE_BINARY_DIR}/quarantine")
set(CRUCIBLE_QUARANTINE_PLUGIN "${_crucible_quarantine_out}/crucible_quarantine.so")
file(SHA256 "${CRUCIBLE_QUARANTINE_SOURCE}" _crucible_quarantine_source_hash)
file(SHA256 "${CRUCIBLE_REAL_CXX}" _crucible_quarantine_driver_hash)
set(_crucible_quarantine_key "${_crucible_quarantine_source_hash} ${_crucible_quarantine_driver_hash}")
if(EXISTS "${_crucible_quarantine_cc1plus}")
  file(SHA256 "${_crucible_quarantine_cc1plus}" _crucible_quarantine_cc1plus_hash)
  string(APPEND _crucible_quarantine_key " ${_crucible_quarantine_cc1plus_hash}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_crucible_quarantine_cc1plus}")
endif()
set(_crucible_quarantine_recorded_key "")
if(EXISTS "${CRUCIBLE_QUARANTINE_PLUGIN}.key")
  file(READ "${CRUCIBLE_QUARANTINE_PLUGIN}.key" _crucible_quarantine_recorded_key)
endif()
if(NOT EXISTS "${CRUCIBLE_QUARANTINE_PLUGIN}" OR
   NOT _crucible_quarantine_recorded_key STREQUAL _crucible_quarantine_key)
  file(MAKE_DIRECTORY "${_crucible_quarantine_out}")
  execute_process(
    COMMAND "${CRUCIBLE_REAL_CXX}" ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS}
            -o "${CRUCIBLE_QUARANTINE_PLUGIN}" "${CRUCIBLE_QUARANTINE_SOURCE}"
    RESULT_VARIABLE _crucible_quarantine_result
    ERROR_VARIABLE _crucible_quarantine_error)
  if(NOT _crucible_quarantine_result EQUAL 0)
    message(FATAL_ERROR "CRUCIBLE_QUARANTINE: the plugin did not build:\n${_crucible_quarantine_error}")
  endif()
  file(WRITE "${CRUCIBLE_QUARANTINE_PLUGIN}.key" "${_crucible_quarantine_key}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CRUCIBLE_QUARANTINE_SOURCE}" "${CRUCIBLE_QUARANTINE_ADMITTED}")

file(SHA256 "${CRUCIBLE_QUARANTINE_PLUGIN}" _crucible_quarantine_plugin_hash)
file(SHA256 "${CRUCIBLE_QUARANTINE_ADMITTED}" _crucible_quarantine_admitted_hash)
string(SHA256 _crucible_quarantine_stamp "${_crucible_quarantine_plugin_hash} ${_crucible_quarantine_admitted_hash}")
string(SUBSTRING "${_crucible_quarantine_stamp}" 0 16 _crucible_quarantine_stamp)
string(TOLOWER "${CRUCIBLE_QUARANTINE}" _crucible_quarantine_mode)

set(_crucible_quarantine_flags
  "-fplugin=${CRUCIBLE_QUARANTINE_PLUGIN}"
  "-fplugin-arg-crucible_quarantine-root=${CMAKE_SOURCE_DIR}"
  "-fplugin-arg-crucible_quarantine-build=${CMAKE_BINARY_DIR}"
  "-fplugin-arg-crucible_quarantine-admitted=${CRUCIBLE_QUARANTINE_ADMITTED}"
  "-fplugin-arg-crucible_quarantine-mode=${_crucible_quarantine_mode}"
  "-fplugin-arg-crucible_quarantine-stamp=${_crucible_quarantine_stamp}")
if(CRUCIBLE_QUARANTINE STREQUAL "REPORT")
  set(CRUCIBLE_QUARANTINE_REPORT_DIR "${_crucible_quarantine_out}/report")
  file(MAKE_DIRECTORY "${CRUCIBLE_QUARANTINE_REPORT_DIR}")
  list(APPEND _crucible_quarantine_flags "-fplugin-arg-crucible_quarantine-out=${CRUCIBLE_QUARANTINE_REPORT_DIR}")
  if(CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "CRUCIBLE_QUARANTINE=REPORT: the compiler launcher (${CMAKE_CXX_COMPILER_LAUNCHER}) is not used")
  endif()
  set(CMAKE_CXX_COMPILER_LAUNCHER "")
endif()
foreach(_crucible_quarantine_flag IN LISTS _crucible_quarantine_flags)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${_crucible_quarantine_flag}>")
endforeach()
message(STATUS "CRUCIBLE_QUARANTINE=${CRUCIBLE_QUARANTINE}: plugin ${CRUCIBLE_QUARANTINE_PLUGIN} (stamp "
  "${_crucible_quarantine_stamp})")
