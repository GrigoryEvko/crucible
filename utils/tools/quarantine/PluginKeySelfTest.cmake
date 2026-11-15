# The test of the key file of the quarantine plugin (utils/tools/quarantine/PluginKey.cmake).  ctest runs it as:
#   cmake -DWORK=<directory> -P utils/tools/quarantine/PluginKeySelfTest.cmake
#
# A plugin is current only with its key file, the key of the configure and the
# hash of its bytes.  A stop of a configure after the link of a new plugin and
# before the write of its key file, and a stop during the link, must each give
# a plugin that is not current.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/PluginKey.cmake")
if(NOT DEFINED WORK)
  message(FATAL_ERROR "PluginKeySelfTest.cmake: give -DWORK=<directory>")
endif()

set(failures "")
set(plugin "${WORK}/plugin-key-self-test/crucible_quarantine.so")
file(REMOVE_RECURSE "${WORK}/plugin-key-self-test")
file(MAKE_DIRECTORY "${WORK}/plugin-key-self-test")

# Records a failure when the verdict for KEY is not EXPECTED.
function(expect name expected key)
  crucible_quarantine_plugin_is_current(is_current "${plugin}" "${key}")
  if(NOT is_current STREQUAL expected)
    set(failures ${failures} "${name}: the plugin is current ${is_current}, and the case expects ${expected}"
      PARENT_SCOPE)
  endif()
endfunction()

expect("no plugin" FALSE "key-of-main")
file(WRITE "${plugin}" "the bytes of the plugin of main")
expect("a plugin with no key file" FALSE "key-of-main")
file(WRITE "${plugin}.key" "key-of-main")
expect("a key file of the earlier format, with no hash of the bytes" FALSE "key-of-main")
crucible_quarantine_write_plugin_key("${plugin}" "key-of-main")
expect("a plugin with its key file" TRUE "key-of-main")
expect("a different key" FALSE "key-of-a-change")
file(WRITE "${plugin}" "the bytes of the plugin of a change")
expect("the bytes of a different build under the key file of main, as a stop after the link leaves them" FALSE
  "key-of-main")
file(WRITE "${plugin}" "")
expect("an empty plugin under the key file of main, as a stop at the start of the link leaves it" FALSE
  "key-of-main")
crucible_quarantine_write_plugin_key("${plugin}" "key-of-a-change")
expect("a new key file after a complete build" TRUE "key-of-a-change")

file(REMOVE_RECURSE "${WORK}/plugin-key-self-test")
if(failures)
  list(JOIN failures "\n  " text)
  message(FATAL_ERROR "PluginKeySelfTest:\n  ${text}")
endif()
message(STATUS "PluginKeySelfTest: the key file gives the correct verdict for each case")
