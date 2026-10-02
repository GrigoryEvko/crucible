# The GCC plugin of the tree: CRUCIBLE_QUARANTINE_MODE = REPORT | ERROR
#
# quarantine.cpp and plugin_core.h build the quarantine plugin,
# crucible_quarantine.so.  It reads the rule table
# utils/scripts/layer-rules.txt, which gives the base layers, the quarantined
# directories and the enforce mode of each path, and the head of
# quarantine.cpp says what the plugin reports.  The plugin also applies the
# contract rule of the tree: a P2900 contract specifier is a compile error in
# each file under the source root, also in a generated file of the build
# directory.  CMake builds the plugin at configure time with the compiler of
# the build, and loads it into every C++ compile of the tree, in each preset:
#
#   REPORT  The default.  Each object holds the findings of its unit in the
#           section .crucible.quarantine (the head comment of quarantine.cpp,
#           THE SECTION), and utils/scripts/quarantine_sections.py reads the
#           section.  A finding in a file whose enforce mode is error is a
#           compile error, and each other finding stops nothing.
#   ERROR   Each finding that no opt-out region covers is a compile error.
#
# The location rule decides what the plugin checks, and the target does not.
# So the flags go on the directory before the first target, and every target
# of the tree gets them.  execute_process builds the plugin, so the plugin is
# not a target and it is not built with itself loaded.
#
# THE INPUTS OF A COMPILE
#   utils/scripts/quarantine_stamps.py runs at each configure.  It writes the
#   facts of the rule table (facts.txt), its enforce rows (enforce.txt) and a
#   mode stamp for each source file of a row into ${CMAKE_BINARY_DIR}/quarantine.
#   It writes a file only when its content changes.
#   - The stamp argument holds the key of the plugin, the mode and the hash of
#     facts.txt.  A change of the plugin, of its flags, of the compiler or of a
#     fact changes each compile line, and each object compiles again.  A
#     change of a reason or of a comment changes no compile line.
#   - The plugin names the mode stamp of each file with a finding in the
#     dependency file of the unit.  A change of the enforce mode of a file
#     compiles again only the units with a finding in that file.
#   - ccache hashes enforce.txt (extra_files_to_hash), so a unit that compiles
#     again after a change of a mode never gets the object of the old mode.
#   ccache ignores the paths that the plugin arguments name, so the build
#   directories of two work trees share the entries of the cache.  The stamp
#   stays in the hash, and the section of each object holds it.
#
# The root CMakeLists.txt includes this file after the ccache block and the
# PGO block, which can also clear the compiler launcher, and before the first
# target.  A project that is not the tree, such as the project of
# cmake/CcacheSelfTest.cmake, sets CRUCIBLE_RULE_TABLE to a table of its own
# before the include.

set(CRUCIBLE_QUARANTINE_MODE "REPORT" CACHE STRING "The mode of the quarantine plugin of GCC: REPORT or ERROR")
set_property(CACHE CRUCIBLE_QUARANTINE_MODE PROPERTY STRINGS REPORT ERROR)
if(NOT CRUCIBLE_QUARANTINE_MODE MATCHES "^(REPORT|ERROR)$")
  message(FATAL_ERROR "CRUCIBLE_QUARANTINE_MODE is '${CRUCIBLE_QUARANTINE_MODE}'. The values are REPORT and ERROR. "
    "Each build loads the quarantine plugin, and REPORT is the default.")
endif()

set(CRUCIBLE_QUARANTINE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/quarantine.cpp")
set(CRUCIBLE_PLUGIN_CORE "${CMAKE_CURRENT_LIST_DIR}/plugin_core.h")
cmake_path(SET _crucible_quarantine_scripts NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../../scripts")
set(CRUCIBLE_QUARANTINE_STAMPS_SCRIPT "${_crucible_quarantine_scripts}/quarantine_stamps.py")
if(NOT DEFINED CRUCIBLE_RULE_TABLE)
  set(CRUCIBLE_RULE_TABLE "${_crucible_quarantine_scripts}/layer-rules.txt")
endif()
find_program(CRUCIBLE_PYTHON3 NAMES python3 REQUIRED)

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

# The test of the plugin.  It builds its own copy in a temporary directory,
# and it takes the compile command of one object of this build.
add_test(NAME quarantine_plugin
  COMMAND "${CRUCIBLE_PYTHON3}" "${CMAKE_CURRENT_LIST_DIR}/test/check_plugin.py"
          --cxx "${CRUCIBLE_REAL_CXX}"
          --source "${CRUCIBLE_QUARANTINE_SOURCE}"
          --rules "${CRUCIBLE_RULE_TABLE}"
          --build-dir "${CMAKE_BINARY_DIR}"
          -- ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS})
set_tests_properties(quarantine_plugin PROPERTIES LABELS "ci_guard")
add_test(NAME quarantine_stamps_self_test
  COMMAND "${CRUCIBLE_PYTHON3}" "${CRUCIBLE_QUARANTINE_STAMPS_SCRIPT}" --self-test)
set_tests_properties(quarantine_stamps_self_test PROPERTIES LABELS "ci_guard")

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "The quarantine rule and the contract rule of the tree load a GCC plugin, and the compiler is "
    "${CMAKE_CXX_COMPILER_ID}. Configure with GCC 16 (cmake/Toolchain-gcc16.cmake).")
endif()
if(NOT EXISTS "${_crucible_quarantine_gcc_plugin_dir}/include/gcc-plugin.h")
  message(FATAL_ERROR "The quarantine rule and the contract rule of the tree load a GCC plugin, and "
    "'${CRUCIBLE_REAL_CXX} -print-file-name=plugin' gave '${_crucible_quarantine_gcc_plugin_dir}', which holds no "
    "include/gcc-plugin.h. Use a compiler with plugin support: utils/toolchain/gcc/build.sh builds one.")
endif()

# A plugin must match the cc1plus that loads it, so the compiler is part of
# the key that decides whether a plugin is current.  When ccache is the
# launcher, cmake/Ccache.cmake hashed the same driver and the same cc1plus in
# this configure run, and a hash of the 400 MB cc1plus costs about one second.
execute_process(
  COMMAND "${CRUCIBLE_REAL_CXX}" -print-prog-name=cc1plus
  OUTPUT_VARIABLE _crucible_quarantine_cc1plus
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(_crucible_quarantine_out "${CMAKE_BINARY_DIR}/quarantine")
if(DEFINED CRUCIBLE_DRIVER_SHA256 AND NOT DEFINED CACHE{CRUCIBLE_DRIVER_SHA256})
  set(_crucible_quarantine_driver_hash "${CRUCIBLE_DRIVER_SHA256}")
else()
  file(SHA256 "${CRUCIBLE_REAL_CXX}" _crucible_quarantine_driver_hash)
endif()
set(_crucible_quarantine_compiler_key "${_crucible_quarantine_driver_hash} ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS}")
if(EXISTS "${_crucible_quarantine_cc1plus}")
  if(DEFINED CRUCIBLE_CC1PLUS_SHA256 AND NOT DEFINED CACHE{CRUCIBLE_CC1PLUS_SHA256}
     AND CRUCIBLE_CC1PLUS STREQUAL _crucible_quarantine_cc1plus)
    set(_crucible_quarantine_cc1plus_hash "${CRUCIBLE_CC1PLUS_SHA256}")
  else()
    file(SHA256 "${_crucible_quarantine_cc1plus}" _crucible_quarantine_cc1plus_hash)
  endif()
  string(APPEND _crucible_quarantine_compiler_key " ${_crucible_quarantine_cc1plus_hash}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_crucible_quarantine_cc1plus}")
endif()

# The plugin builds when its key changed.  The compile runs in the directory
# of the sources and maps that directory to `.`, so the plugin bytes hold no
# path of the work tree, and two work trees build the same bytes.  The result
# store of the negative fixtures keys these bytes (test/neg_compile_store.py).
# The key names the map with no path, so the key and the stamp stay the same
# in each work tree.
file(SHA256 "${CRUCIBLE_QUARANTINE_SOURCE}" _crucible_quarantine_source_hash)
file(SHA256 "${CRUCIBLE_PLUGIN_CORE}" _crucible_quarantine_core_hash)
set(_crucible_quarantine_key
  "${_crucible_quarantine_source_hash} ${_crucible_quarantine_core_hash} ${_crucible_quarantine_compiler_key} -ffile-prefix-map=SOURCE_DIRECTORY=.")
set(CRUCIBLE_QUARANTINE_PLUGIN "${_crucible_quarantine_out}/crucible_quarantine.so")
set(_crucible_quarantine_recorded_key "")
if(EXISTS "${CRUCIBLE_QUARANTINE_PLUGIN}.key")
  file(READ "${CRUCIBLE_QUARANTINE_PLUGIN}.key" _crucible_quarantine_recorded_key)
endif()
if(NOT EXISTS "${CRUCIBLE_QUARANTINE_PLUGIN}" OR NOT _crucible_quarantine_recorded_key STREQUAL _crucible_quarantine_key)
  file(MAKE_DIRECTORY "${_crucible_quarantine_out}")
  execute_process(
    COMMAND "${CRUCIBLE_REAL_CXX}" ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS}
            "-ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=." -o "${CRUCIBLE_QUARANTINE_PLUGIN}"
            "${CRUCIBLE_QUARANTINE_SOURCE}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE _crucible_quarantine_result
    ERROR_VARIABLE _crucible_quarantine_error)
  if(NOT _crucible_quarantine_result EQUAL 0)
    message(FATAL_ERROR "The quarantine plugin did not build:\n${_crucible_quarantine_error}")
  endif()
  file(WRITE "${CRUCIBLE_QUARANTINE_PLUGIN}.key" "${_crucible_quarantine_key}")
endif()

# The facts, the enforce rows and the mode stamps of the rule table.
execute_process(
  COMMAND "${CRUCIBLE_PYTHON3}" "${CRUCIBLE_QUARANTINE_STAMPS_SCRIPT}"
          --root "${CMAKE_SOURCE_DIR}" --rules "${CRUCIBLE_RULE_TABLE}" --out "${_crucible_quarantine_out}"
          --build "${CMAKE_BINARY_DIR}"
  RESULT_VARIABLE _crucible_quarantine_result
  ERROR_VARIABLE _crucible_quarantine_error)
if(NOT _crucible_quarantine_result EQUAL 0)
  message(FATAL_ERROR "The quarantine stamps were not written:\n${_crucible_quarantine_error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CRUCIBLE_QUARANTINE_SOURCE}" "${CRUCIBLE_PLUGIN_CORE}" "${CRUCIBLE_RULE_TABLE}"
  "${CRUCIBLE_QUARANTINE_STAMPS_SCRIPT}" "${_crucible_quarantine_scripts}/layer_rules.py")

string(TOLOWER "${CRUCIBLE_QUARANTINE_MODE}" _crucible_quarantine_mode)
file(SHA256 "${_crucible_quarantine_out}/facts.txt" _crucible_quarantine_facts_hash)
string(SHA256 _crucible_quarantine_stamp
  "${_crucible_quarantine_key} ${_crucible_quarantine_mode} ${_crucible_quarantine_facts_hash}")
string(SUBSTRING "${_crucible_quarantine_stamp}" 0 16 _crucible_quarantine_stamp)
set(_crucible_quarantine_argument "-fplugin-arg-crucible_quarantine")
set(_crucible_quarantine_flags
  "-fplugin=${CRUCIBLE_QUARANTINE_PLUGIN}"
  "${_crucible_quarantine_argument}-root=${CMAKE_SOURCE_DIR}"
  "${_crucible_quarantine_argument}-build=${CMAKE_BINARY_DIR}"
  "${_crucible_quarantine_argument}-rules=${CRUCIBLE_RULE_TABLE}"
  "${_crucible_quarantine_argument}-stamps=${_crucible_quarantine_out}"
  "${_crucible_quarantine_argument}-mode=${_crucible_quarantine_mode}"
  "${_crucible_quarantine_argument}-stamp=${_crucible_quarantine_stamp}")

crucible_launcher_is_ccache("${CMAKE_CXX_COMPILER_LAUNCHER}" _crucible_quarantine_launcher_is_ccache)
if(_crucible_quarantine_launcher_is_ccache)
  # ccache hashes the plugin path and the paths of the plugin arguments as
  # text, and each work tree has its own.  The stamp stays in the hash, and
  # ccache hashes the content of enforce.txt and not its path.
  list(APPEND CMAKE_CXX_COMPILER_LAUNCHER
    "ignore_options=-fplugin=* ${_crucible_quarantine_argument}-root=* ${_crucible_quarantine_argument}-build=* ${_crucible_quarantine_argument}-rules=* ${_crucible_quarantine_argument}-stamps=*"
    "extra_files_to_hash=${_crucible_quarantine_out}/enforce.txt")
endif()
foreach(_crucible_quarantine_flag IN LISTS _crucible_quarantine_flags)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${_crucible_quarantine_flag}>")
endforeach()
message(STATUS "CRUCIBLE_QUARANTINE_MODE=${CRUCIBLE_QUARANTINE_MODE}: plugin ${CRUCIBLE_QUARANTINE_PLUGIN} (stamp "
  "${_crucible_quarantine_stamp})")
