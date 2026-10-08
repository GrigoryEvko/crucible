# The GCC plugin of the tree: CRUCIBLE_QUARANTINE_MODE = REPORT | ERROR
#
# The sources of utils/tools/quarantine/ build the quarantine plugin,
# crucible_quarantine.so: one source for each rule family, and plugin.h and
# rows.h, which each source includes (the head comment of plugin.h).  The
# plugin reads the rule table utils/rule-table.txt, which gives the base
# layers, the quarantined directories and the enforce mode of each path, and
# the head of plugin.h says what the plugin reports.  The plugin also applies
# the contract rule of the tree: a P2900 contract specifier is a compile error
# in each file under the source root, also in a generated file of the build
# directory.  CMake builds the plugin at configure time and loads it into every
# C++ compile of the tree, in each preset:
#
#   REPORT  The default.  Each object holds the findings of its unit in the
#           section .crucible.quarantine (the head comment of plugin.cpp,
#           THE SECTION), and utils/assay/readers/sections.py reads the
#           section.  A finding in a file whose enforce mode is error is a
#           compile error, and each other finding stops nothing.
#   ERROR   Each finding that no opt-out region covers is a compile error.
#
# The location rule decides what the plugin checks, and the target does not.
# So the flags go on the directory before the first target, and every target
# of the tree gets them.  execute_process builds the plugin, so the plugin is
# not a target and it is not built with itself loaded.
#
# THE COMPILER OF THE PLUGIN
#   The plugin loads into the cc1plus of the patched GCC, so the patched g++
#   builds it.  cmake/Toolchain-gcc16.cmake gives that compiler as
#   CRUCIBLE_TOOLCHAIN_CXX, also when CMAKE_CXX_COMPILER names a wrapper of it,
#   such as the afl-g++-fast of `loop fuzz`.  A wrapper that instruments the
#   code it compiles gives the plugin a reference to a symbol that cc1plus does
#   not define, and cc1plus then cannot load the plugin.  A configure with no
#   toolchain file builds the plugin with the compiler of the build.
#
# THE INPUTS OF A COMPILE
#   `loop stamps` (utils/assay/loop/stamps.py) runs at each configure.  It writes the
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
# THE KINDS
#   After each build of the plugin, the plugin writes the kinds of each rule to
#   ${CMAKE_BINARY_DIR}/quarantine/kinds.txt (the argument kinds of plugin.cpp).
#   This run also makes sure that cc1plus loads the plugin.  The rule
#   plugin-kinds compares kinds.txt with the kinds of the rows of the engine
#   plugin of utils/registry.txt.
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

# The sources of the plugin: one unit for each rule family, and the headers
# that each unit includes.  rows_reader.cpp is the host program of the rule
# rows-readers, and no part of the plugin.
set(_crucible_quarantine_units plugin rule_table regions contract include unit library owner declaration expression)
set(_crucible_quarantine_headers plugin.h rows.h)
set(CRUCIBLE_QUARANTINE_SOURCES "")
foreach(_crucible_quarantine_unit IN LISTS _crucible_quarantine_units)
  list(APPEND CRUCIBLE_QUARANTINE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/${_crucible_quarantine_unit}.cpp")
endforeach()
foreach(_crucible_quarantine_header IN LISTS _crucible_quarantine_headers)
  list(APPEND CRUCIBLE_QUARANTINE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/${_crucible_quarantine_header}")
endforeach()
cmake_path(SET _crucible_quarantine_utils NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CRUCIBLE_GUARD "${_crucible_quarantine_utils}/bin/guard")
set(CRUCIBLE_LOOP "${_crucible_quarantine_utils}/bin/loop")
if(NOT DEFINED CRUCIBLE_RULE_TABLE)
  set(CRUCIBLE_RULE_TABLE "${_crucible_quarantine_utils}/rule-table.txt")
endif()
find_program(CRUCIBLE_PYTHON3 NAMES python3 REQUIRED)

# The compiler of the plugin (THE COMPILER OF THE PLUGIN).
if(DEFINED CRUCIBLE_TOOLCHAIN_CXX)
  set(CRUCIBLE_PLUGIN_CXX "${CRUCIBLE_TOOLCHAIN_CXX}")
else()
  set(CRUCIBLE_PLUGIN_CXX "${CRUCIBLE_REAL_CXX}")
endif()

# The flags that build the plugin.  GCC is built without RTTI, and the plugin
# loads into cc1plus, so it finds the libstdc++ of the compiler through an
# rpath.
execute_process(
  COMMAND "${CRUCIBLE_PLUGIN_CXX}" -print-file-name=plugin
  OUTPUT_VARIABLE _crucible_quarantine_gcc_plugin_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND "${CRUCIBLE_PLUGIN_CXX}" -print-file-name=libstdc++.so
  OUTPUT_VARIABLE _crucible_quarantine_libstdcxx
  OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(_crucible_quarantine_library_dir "${_crucible_quarantine_libstdcxx}" DIRECTORY)
set(CRUCIBLE_QUARANTINE_COMPILE_FLAGS
  -std=gnu++17 -fno-rtti -fPIC -O2 -g -Wall -Wextra -Werror
  -isystem "${_crucible_quarantine_gcc_plugin_dir}/include")
set(CRUCIBLE_QUARANTINE_LINK_FLAGS -fno-rtti -shared "-Wl,-rpath,${_crucible_quarantine_library_dir}")

crucible_script_test(NAME quarantine_stamps_self_test
  COMMAND "${CRUCIBLE_PYTHON3}" "${CRUCIBLE_GUARD}" selftest loop.stamps)
set_tests_properties(quarantine_stamps_self_test PROPERTIES LABELS "ci_guard")

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "The quarantine rule and the contract rule of the tree load a GCC plugin, and the compiler is "
    "${CMAKE_CXX_COMPILER_ID}. Configure with GCC 16 (cmake/Toolchain-gcc16.cmake).")
endif()
if(NOT EXISTS "${_crucible_quarantine_gcc_plugin_dir}/include/gcc-plugin.h")
  message(FATAL_ERROR "The quarantine rule and the contract rule of the tree load a GCC plugin, and "
    "'${CRUCIBLE_PLUGIN_CXX} -print-file-name=plugin' gave '${_crucible_quarantine_gcc_plugin_dir}', which holds no "
    "include/gcc-plugin.h. Use a compiler with plugin support: python3 utils/bin/setup gcc PREFIX builds one.")
endif()

# A plugin must match the cc1plus that loads it, so the compiler is part of
# the key that decides whether a plugin is current.  When ccache is the
# launcher, cmake/Ccache.cmake hashed the same driver and the same cc1plus in
# this configure run, and a hash of the 400 MB cc1plus costs about one second.
execute_process(
  COMMAND "${CRUCIBLE_PLUGIN_CXX}" -print-prog-name=cc1plus
  OUTPUT_VARIABLE _crucible_quarantine_cc1plus
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(_crucible_quarantine_out "${CMAKE_BINARY_DIR}/quarantine")
if(DEFINED CRUCIBLE_DRIVER_SHA256 AND NOT DEFINED CACHE{CRUCIBLE_DRIVER_SHA256}
   AND CRUCIBLE_REAL_CXX STREQUAL CRUCIBLE_PLUGIN_CXX)
  set(_crucible_quarantine_driver_hash "${CRUCIBLE_DRIVER_SHA256}")
else()
  file(SHA256 "${CRUCIBLE_PLUGIN_CXX}" _crucible_quarantine_driver_hash)
endif()
set(_crucible_quarantine_compiler_key
  "${_crucible_quarantine_driver_hash} ${CRUCIBLE_QUARANTINE_COMPILE_FLAGS} ${CRUCIBLE_QUARANTINE_LINK_FLAGS}")
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

# The plugin builds when its key changed.  Each unit compiles in the directory
# of the sources, which the compile maps to `.`, so the plugin bytes hold no
# path of the work tree, and two work trees build the same bytes.  The result
# store of the negative fixtures keys these bytes (utils/assay/loop/fixture/store.py).
# The key names the map with no path, so the key and the stamp stay the same
# in each work tree.  The units compile at the same time: execute_process runs
# its commands as one pipeline, and no compiler reads its input or writes its
# output.
set(_crucible_quarantine_key "")
foreach(_crucible_quarantine_source IN LISTS CRUCIBLE_QUARANTINE_SOURCES)
  file(SHA256 "${_crucible_quarantine_source}" _crucible_quarantine_source_hash)
  get_filename_component(_crucible_quarantine_source_name "${_crucible_quarantine_source}" NAME)
  string(APPEND _crucible_quarantine_key "${_crucible_quarantine_source_name}=${_crucible_quarantine_source_hash} ")
endforeach()
string(APPEND _crucible_quarantine_key "${_crucible_quarantine_compiler_key} -ffile-prefix-map=SOURCE_DIRECTORY=.")
set(CRUCIBLE_QUARANTINE_PLUGIN "${_crucible_quarantine_out}/crucible_quarantine.so")
set(_crucible_quarantine_kinds "${_crucible_quarantine_out}/kinds.txt")
set(_crucible_quarantine_recorded_key "")
if(EXISTS "${CRUCIBLE_QUARANTINE_PLUGIN}.key")
  file(READ "${CRUCIBLE_QUARANTINE_PLUGIN}.key" _crucible_quarantine_recorded_key)
endif()
if(NOT EXISTS "${CRUCIBLE_QUARANTINE_PLUGIN}" OR NOT _crucible_quarantine_recorded_key STREQUAL _crucible_quarantine_key)
  set(_crucible_quarantine_objects_dir "${_crucible_quarantine_out}/plugin-objects")
  file(REMOVE_RECURSE "${_crucible_quarantine_objects_dir}")
  file(MAKE_DIRECTORY "${_crucible_quarantine_objects_dir}")
  set(_crucible_quarantine_compiles "")
  set(_crucible_quarantine_objects "")
  foreach(_crucible_quarantine_unit IN LISTS _crucible_quarantine_units)
    set(_crucible_quarantine_object "${_crucible_quarantine_objects_dir}/${_crucible_quarantine_unit}.o")
    list(APPEND _crucible_quarantine_objects "${_crucible_quarantine_object}")
    list(APPEND _crucible_quarantine_compiles
      COMMAND "${CRUCIBLE_PLUGIN_CXX}" ${CRUCIBLE_QUARANTINE_COMPILE_FLAGS}
              "-ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=." -c -o "${_crucible_quarantine_object}"
              "${CMAKE_CURRENT_LIST_DIR}/${_crucible_quarantine_unit}.cpp")
  endforeach()
  execute_process(${_crucible_quarantine_compiles}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULTS_VARIABLE _crucible_quarantine_results
    ERROR_VARIABLE _crucible_quarantine_error)
  foreach(_crucible_quarantine_result IN LISTS _crucible_quarantine_results)
    if(NOT _crucible_quarantine_result EQUAL 0)
      message(FATAL_ERROR "The quarantine plugin did not build:\n${_crucible_quarantine_error}")
    endif()
  endforeach()
  execute_process(
    COMMAND "${CRUCIBLE_PLUGIN_CXX}" ${CRUCIBLE_QUARANTINE_LINK_FLAGS} -o "${CRUCIBLE_QUARANTINE_PLUGIN}"
            ${_crucible_quarantine_objects}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE _crucible_quarantine_result
    ERROR_VARIABLE _crucible_quarantine_error)
  if(NOT _crucible_quarantine_result EQUAL 0)
    message(FATAL_ERROR "The quarantine plugin did not link:\n${_crucible_quarantine_error}")
  endif()
  file(REMOVE_RECURSE "${_crucible_quarantine_objects_dir}")
  file(REMOVE "${_crucible_quarantine_kinds}")
  file(WRITE "${CRUCIBLE_QUARANTINE_PLUGIN}.key" "${_crucible_quarantine_key}")
endif()

# The kinds of each rule (THE KINDS).
if(NOT EXISTS "${_crucible_quarantine_kinds}")
  execute_process(
    COMMAND "${CRUCIBLE_PLUGIN_CXX}" -fsyntax-only -x c++ /dev/null "-fplugin=${CRUCIBLE_QUARANTINE_PLUGIN}"
            "-fplugin-arg-crucible_quarantine-kinds=${_crucible_quarantine_kinds}"
    RESULT_VARIABLE _crucible_quarantine_result
    ERROR_VARIABLE _crucible_quarantine_error)
  if(NOT _crucible_quarantine_result EQUAL 0 OR NOT EXISTS "${_crucible_quarantine_kinds}")
    message(FATAL_ERROR "cc1plus did not load the quarantine plugin ${CRUCIBLE_QUARANTINE_PLUGIN}, or the plugin did "
      "not write ${_crucible_quarantine_kinds}:\n${_crucible_quarantine_error}")
  endif()
endif()

# The witness of the plugin: it compiles each plant and each control of the
# rules of the engine plugin with the plugin that this configure builds
# (utils/plants/RULE/ of each such rule).  At most
# _crucible_quarantine_test_jobs compilers of the test run at one time, and the
# test asks ctest for the same number of processors.  So ctest -j counts each
# compiler of the test.
set(_crucible_quarantine_test_jobs 16)
crucible_script_test(NAME guard_plugin_witness
  COMMAND "${CRUCIBLE_PYTHON3}" "${CRUCIBLE_GUARD}" witness --engine plugin
          --cxx "${CRUCIBLE_PLUGIN_CXX}" --plugin "${CRUCIBLE_QUARANTINE_PLUGIN}"
          --build-dir "${CMAKE_BINARY_DIR}" --jobs ${_crucible_quarantine_test_jobs})
set_tests_properties(guard_plugin_witness PROPERTIES LABELS "ci_guard" PROCESSORS ${_crucible_quarantine_test_jobs})

# The facts, the enforce rows and the mode stamps of the rule table.
execute_process(
  COMMAND "${CRUCIBLE_PYTHON3}" "${CRUCIBLE_LOOP}" stamps
          --root "${CMAKE_SOURCE_DIR}" --rules "${CRUCIBLE_RULE_TABLE}" --out "${_crucible_quarantine_out}"
          --build "${CMAKE_BINARY_DIR}"
  RESULT_VARIABLE _crucible_quarantine_result
  ERROR_VARIABLE _crucible_quarantine_error)
if(NOT _crucible_quarantine_result EQUAL 0)
  message(FATAL_ERROR "The quarantine stamps were not written:\n${_crucible_quarantine_error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CRUCIBLE_QUARANTINE_SOURCES} "${CRUCIBLE_RULE_TABLE}"
  "${CRUCIBLE_LOOP}" "${_crucible_quarantine_utils}/assay/loop/stamps.py"
  "${_crucible_quarantine_utils}/assay/core/ruletable.py" "${_crucible_quarantine_utils}/assay/core/rows.py"
  "${_crucible_quarantine_utils}/assay/core/paths.py")

string(TOLOWER "${CRUCIBLE_QUARANTINE_MODE}" _crucible_quarantine_mode)
file(SHA256 "${_crucible_quarantine_out}/facts.txt" _crucible_quarantine_facts_hash)
string(SHA256 _crucible_quarantine_stamp
  "${_crucible_quarantine_key} ${_crucible_quarantine_mode} ${_crucible_quarantine_facts_hash}")
string(SUBSTRING "${_crucible_quarantine_stamp}" 0 16 _crucible_quarantine_stamp)
set(_crucible_quarantine_argument "-fplugin-arg-crucible_quarantine")
# The families declaration property and expression shape read a base header
# only in the sentinel unit of that header (THE SENTINEL of plugin.h).
# test/layer/CMakeLists.txt gives each sentinel unit this argument with the
# path of its header under the root.  The path is relative to the source
# root, so each work tree gives ccache one text.
set(CRUCIBLE_QUARANTINE_SENTINEL_ARGUMENT "${_crucible_quarantine_argument}-sentinel")
# CRUCIBLE_QUARANTINE_ACTIVE makes the region macros of
# include/foundation/Quarantine.h expand to the pragmas of the plugin.
set(_crucible_quarantine_flags
  "-DCRUCIBLE_QUARANTINE_ACTIVE"
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
