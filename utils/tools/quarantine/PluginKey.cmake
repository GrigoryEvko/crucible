# The key file of the quarantine plugin (the key of the plugin of
# utils/tools/quarantine/Quarantine.cmake).
#
# The key file PLUGIN.key holds two lines: the key of the plugin, and the
# SHA-256 of the bytes of the plugin that this key built.  The key names the
# hash of each source of the plugin, the compiler and the flags.  A plugin is
# current only when the plugin and its key file are there, the key file holds
# the key of this configure, and the bytes of the plugin give the hash of the
# key file.  A key file of an earlier format, with no hash, is not current.
#
# A stop of a configure can end it during the link of a new plugin, or after
# the link and before the write of the key file.  The stop of a run of the
# landing gate is such a stop.  The plugin then holds the bytes of a different
# build, or no complete plugin, under the key file of an earlier build.  The
# hash of the bytes finds such a plugin, and the next configure builds it
# again.  The hash does not go into the stamp of the compiles, so a rebuild for
# the same key changes no compile line.
#
# utils/tools/quarantine/PluginKeySelfTest.cmake is the test of this file.

# Sets OUT to TRUE when the file PLUGIN is the plugin of KEY, as its key file
# records it, else to FALSE.  Complexity: one read of the key file, and one
# hash of the plugin when the key agrees.
function(crucible_quarantine_plugin_is_current out plugin key)
  set(${out} FALSE PARENT_SCOPE)
  if(NOT EXISTS "${plugin}" OR NOT EXISTS "${plugin}.key")
    return()
  endif()
  file(READ "${plugin}.key" recorded)
  string(FIND "${recorded}" "\n" newline)
  if(newline EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${recorded}" 0 ${newline} recorded_key)
  math(EXPR hash_start "${newline} + 1")
  string(SUBSTRING "${recorded}" ${hash_start} -1 recorded_hash)
  string(STRIP "${recorded_hash}" recorded_hash)
  if(NOT "${recorded_key}" STREQUAL "${key}")
    return()
  endif()
  file(SHA256 "${plugin}" bytes_hash)
  if("${bytes_hash}" STREQUAL "${recorded_hash}")
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

# Writes the key file of the plugin PLUGIN: KEY on the first line, and the
# SHA-256 of the bytes of the plugin on the second line.  The caller calls it
# after a complete build of the plugin, as the last step of that build.
function(crucible_quarantine_write_plugin_key plugin key)
  file(SHA256 "${plugin}" bytes_hash)
  file(WRITE "${plugin}.key" "${key}\n${bytes_hash}\n")
endfunction()
