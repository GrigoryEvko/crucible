# The two GCC plugins of the tree: CRUCIBLE_QUARANTINE = OFF | REPORT | ERROR
#
# contract.cpp builds the contract plugin, crucible_contract.so.  It holds the
# contract rule of the tree: a P2900 contract specifier is a compile error in
# each file under the source root, also in a generated file of the build
# directory.  quarantine.cpp builds the quarantine plugin,
# crucible_quarantine.so.  It reads the rule table
# utils/scripts/layer-rules.txt, which gives the base layers and the
# quarantined directories, and the head of quarantine.cpp says what the plugin
# reports.  The quarantine plugin applies the contract rule too.
# plugin_core.h holds the part that the two share, and its section THE FILES
# gives the class of each file.  Each plugin takes the build directory, so
# the two plugins give each file the same class.
# CMake builds the plugin of the build at configure time with the compiler of
# the build, and loads it into every C++ compile of the tree:
#
#   OFF     The default.  Each compile loads the contract plugin.  The rule
#           writes no file, so the compiler launcher stays: a cache hit gives
#           an object that the same source and the same plugin made, and that
#           compile passed the rule.
#   REPORT  Each compile loads the quarantine plugin, and each object holds
#           the findings of its unit in the section .crucible.quarantine
#           (the head comment of quarantine.cpp, THE SECTION).  A finding in
#           a path with the enforce mode report does not stop the build.  The
#           compiler launcher stays: a cache hit gives an object with the
#           section of the same source, plugin and rule table, because the
#           stamp holds the plugin and the rule table.
#           utils/scripts/quarantine_sections.py reads the section.
#   ERROR   Each compile loads the quarantine plugin.  Each finding that no
#           opt-out region covers is a compile error.
#
# A compile loads one of the two plugins, because each registers the opt-out
# pragmas.  The plugin of an OFF build comes from contract.cpp and
# plugin_core.h only.  So a change of quarantine.cpp configures no OFF build
# again, and it compiles none of its objects again.
#
# The location rule decides what a plugin checks, and the target does not.
# So the flags go on the directory before the first target, and every target
# of the tree gets them.  execute_process builds the plugin, so the plugin is
# not a target and it is not built with itself loaded.  A change of the
# sources of the plugin, of its flags, of the compiler or (for the quarantine
# plugin) of the rule table configures again.  The stamp argument then
# changes each compile line, and each object compiles again.  The stamp comes
# from these inputs and not from the bytes of the plugin.  ccache ignores the
# paths that the plugin arguments name, so the build directories of two work
# trees share the entries of the cache.
#
# The root CMakeLists.txt includes this file after the ccache block and the
# PGO block, which can also clear the compiler launcher, and before the first
# target.

set(CRUCIBLE_QUARANTINE "OFF" CACHE STRING "The quarantine plugin of GCC: OFF, REPORT or ERROR")
set_property(CACHE CRUCIBLE_QUARANTINE PROPERTY STRINGS OFF REPORT ERROR)
if(NOT CRUCIBLE_QUARANTINE MATCHES "^(OFF|REPORT|ERROR)$")
  message(FATAL_ERROR "CRUCIBLE_QUARANTINE is '${CRUCIBLE_QUARANTINE}'. The values are OFF, REPORT and ERROR.")
endif()

set(CRUCIBLE_CONTRACT_PLUGIN_SOURCE "${CMAKE_CURRENT_LIST_DIR}/contract.cpp")
set(CRUCIBLE_QUARANTINE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/quarantine.cpp")
set(CRUCIBLE_PLUGIN_CORE "${CMAKE_CURRENT_LIST_DIR}/plugin_core.h")
set(CRUCIBLE_RULE_TABLE "${CMAKE_SOURCE_DIR}/utils/scripts/layer-rules.txt")

# The flags that build a plugin.  GCC is built without RTTI, and the plugin
# loads into cc1plus, so it finds the libstdc++ of the compiler through an
# rpath.  The test below builds its own copies with the same flags.
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

# The test of the two plugins.  It builds its own copies in a temporary
# directory.
add_test(NAME quarantine_plugin
  COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/test/check_plugin.py"
          --cxx "${CRUCIBLE_REAL_CXX}"
          --contract-source "${CRUCIBLE_CONTRACT_PLUGIN_SOURCE}"
          --source "${CRUCIBLE_QUARANTINE_SOURCE}"
          --rules "${CRUCIBLE_RULE_TABLE}"
          -- ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS})
set_tests_properties(quarantine_plugin PROPERTIES LABELS "ci_guard")

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "The contract rule of the tree loads a GCC plugin, and the compiler is "
    "${CMAKE_CXX_COMPILER_ID}. Configure with GCC 16 (cmake/Toolchain-gcc16.cmake).")
endif()
if(NOT EXISTS "${_crucible_quarantine_gcc_plugin_dir}/include/gcc-plugin.h")
  message(FATAL_ERROR "The contract rule of the tree loads a GCC plugin, and '${CRUCIBLE_REAL_CXX} "
    "-print-file-name=plugin' gave '${_crucible_quarantine_gcc_plugin_dir}', which holds no "
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

# Builds the plugin NAME from SOURCE and plugin_core.h when its key changed,
# and sets the variable named by OUT_KEY to the key.  The compile runs in the
# directory of the sources and maps that directory to `.`, so the plugin bytes
# hold no path of the work tree, and two work trees build the same bytes.  The
# result store of the negative fixtures keys these bytes
# (test/neg_compile_store.py).  The key names the map with no path, so the key
# and the stamp stay the same in each work tree.
function(crucible_build_gcc_plugin name source out_key)
  file(SHA256 "${source}" source_hash)
  file(SHA256 "${CRUCIBLE_PLUGIN_CORE}" core_hash)
  set(key "${source_hash} ${core_hash} ${_crucible_quarantine_compiler_key} -ffile-prefix-map=SOURCE_DIRECTORY=.")
  set(plugin "${_crucible_quarantine_out}/${name}.so")
  get_filename_component(source_dir "${source}" DIRECTORY)
  set(recorded_key "")
  if(EXISTS "${plugin}.key")
    file(READ "${plugin}.key" recorded_key)
  endif()
  if(NOT EXISTS "${plugin}" OR NOT recorded_key STREQUAL key)
    file(MAKE_DIRECTORY "${_crucible_quarantine_out}")
    execute_process(
      COMMAND "${CRUCIBLE_REAL_CXX}" ${CRUCIBLE_QUARANTINE_PLUGIN_FLAGS} "-ffile-prefix-map=${source_dir}=."
              -o "${plugin}" "${source}"
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE result
      ERROR_VARIABLE error_text)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "The plugin ${name} did not build:\n${error_text}")
    endif()
    file(WRITE "${plugin}.key" "${key}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" "${CRUCIBLE_PLUGIN_CORE}")
  set(${out_key} "${key}" PARENT_SCOPE)
endfunction()

if(CRUCIBLE_QUARANTINE STREQUAL "OFF")
  set(_crucible_quarantine_name crucible_contract)
  crucible_build_gcc_plugin(crucible_contract "${CRUCIBLE_CONTRACT_PLUGIN_SOURCE}" _crucible_quarantine_key)
else()
  set(_crucible_quarantine_name crucible_quarantine)
  crucible_build_gcc_plugin(crucible_quarantine "${CRUCIBLE_QUARANTINE_SOURCE}" _crucible_quarantine_key)
endif()
set(CRUCIBLE_QUARANTINE_PLUGIN "${_crucible_quarantine_out}/${_crucible_quarantine_name}.so")
set(_crucible_quarantine_argument "-fplugin-arg-${_crucible_quarantine_name}")

set(_crucible_quarantine_flags
  "-fplugin=${CRUCIBLE_QUARANTINE_PLUGIN}"
  "${_crucible_quarantine_argument}-root=${CMAKE_SOURCE_DIR}"
  "${_crucible_quarantine_argument}-build=${CMAKE_BINARY_DIR}")
set(_crucible_quarantine_stamp_input "${_crucible_quarantine_key}")
if(NOT CRUCIBLE_QUARANTINE STREQUAL "OFF")
  string(TOLOWER "${CRUCIBLE_QUARANTINE}" _crucible_quarantine_mode)
  list(APPEND _crucible_quarantine_flags
    "${_crucible_quarantine_argument}-rules=${CRUCIBLE_RULE_TABLE}"
    "${_crucible_quarantine_argument}-mode=${_crucible_quarantine_mode}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CRUCIBLE_RULE_TABLE}")
  file(SHA256 "${CRUCIBLE_RULE_TABLE}" _crucible_quarantine_rules_hash)
  string(APPEND _crucible_quarantine_stamp_input " ${_crucible_quarantine_rules_hash}")
endif()
string(SHA256 _crucible_quarantine_stamp "${_crucible_quarantine_stamp_input}")
string(SUBSTRING "${_crucible_quarantine_stamp}" 0 16 _crucible_quarantine_stamp)
list(APPEND _crucible_quarantine_flags "${_crucible_quarantine_argument}-stamp=${_crucible_quarantine_stamp}")

crucible_launcher_is_ccache("${CMAKE_CXX_COMPILER_LAUNCHER}" _crucible_quarantine_launcher_is_ccache)
if(_crucible_quarantine_launcher_is_ccache)
  # ccache hashes the plugin path and the paths of the plugin arguments as
  # text, and each work tree has its own.  The stamp stays in the hash.
  list(APPEND CMAKE_CXX_COMPILER_LAUNCHER
    "ignore_options=-fplugin=* ${_crucible_quarantine_argument}-root=* ${_crucible_quarantine_argument}-build=* ${_crucible_quarantine_argument}-rules=*")
endif()
foreach(_crucible_quarantine_flag IN LISTS _crucible_quarantine_flags)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${_crucible_quarantine_flag}>")
endforeach()
message(STATUS "CRUCIBLE_QUARANTINE=${CRUCIBLE_QUARANTINE}: plugin ${CRUCIBLE_QUARANTINE_PLUGIN} (stamp "
  "${_crucible_quarantine_stamp})")
