# ToolchainRunPath.cmake: the run path of the libraries of a toolchain, set on each configure.
#
# A binary of the tree links the libstdc++ of its toolchain, which is not on
# the path of the system loader.  The root CMakeLists.txt gives each link of the
# tree the link option -rpath of CRUCIBLE_TOOLCHAIN_RUN_PATH, with
# add_link_options().  A toolchain file calls crucible_toolchain_run_path() on
# each configure, so an edit of the run path reaches each build directory with
# a plain configure.
#
# THE CACHE
#     A cache entry keeps its value across configures, so a run path in the
#     cache cannot follow an edit of a toolchain file.  The function removes
#     each run path directory of the prefix from the cache entries
#     CMAKE_EXE_LINKER_FLAGS, CMAKE_SHARED_LINKER_FLAGS and
#     CMAKE_MODULE_LINKER_FLAGS, and it keeps each other flag.  It reads a run
#     path in the forms -Wl,-rpath,DIRECTORIES and -Wl,-rpath=DIRECTORIES, with
#     ":" between two directories.  A directory is of the prefix when it is the
#     prefix or a directory below it, as the prefix is spelled or as its links
#     resolve.  An entry with no such directory does not change.

# crucible_toolchain_run_path(PREFIX DIRECTORY)
#
# Set CRUCIBLE_TOOLCHAIN_RUN_PATH to DIRECTORY in the scope of the caller, and
# remove each run path directory of PREFIX from the three cache entries (THE
# CACHE).  Complexity: linear in the words of the three entries.
function(crucible_toolchain_run_path prefix directory)
  set(CRUCIBLE_TOOLCHAIN_RUN_PATH "${directory}" PARENT_SCOPE)
  file(REAL_PATH "${prefix}" _crucible_real_prefix)
  foreach(_crucible_entry CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
    if(NOT DEFINED CACHE{${_crucible_entry}})
      continue()
    endif()
    separate_arguments(_crucible_words UNIX_COMMAND "$CACHE{${_crucible_entry}}")
    set(_crucible_kept "")
    set(_crucible_is_changed FALSE)
    foreach(_crucible_word IN LISTS _crucible_words)
      if(NOT _crucible_word MATCHES "^(-Wl,-rpath[,=])(.*)$")
        list(APPEND _crucible_kept "${_crucible_word}")
        continue()
      endif()
      set(_crucible_head "${CMAKE_MATCH_1}")
      string(REPLACE ":" ";" _crucible_directories "${CMAKE_MATCH_2}")
      set(_crucible_others "")
      foreach(_crucible_directory IN LISTS _crucible_directories)
        set(_crucible_is_of_prefix FALSE)
        foreach(_crucible_root "${prefix}" "${_crucible_real_prefix}")
          string(FIND "${_crucible_directory}/" "${_crucible_root}/" _crucible_position)
          if(_crucible_position EQUAL 0)
            set(_crucible_is_of_prefix TRUE)
          endif()
        endforeach()
        if(_crucible_is_of_prefix)
          set(_crucible_is_changed TRUE)
        else()
          list(APPEND _crucible_others "${_crucible_directory}")
        endif()
      endforeach()
      list(LENGTH _crucible_others _crucible_other_count)
      if(_crucible_other_count GREATER 0)
        list(JOIN _crucible_others ":" _crucible_rest)
        list(APPEND _crucible_kept "${_crucible_head}${_crucible_rest}")
      endif()
    endforeach()
    if(_crucible_is_changed)
      list(JOIN _crucible_kept " " _crucible_flags)
      get_property(_crucible_help CACHE ${_crucible_entry} PROPERTY HELPSTRING)
      set(${_crucible_entry} "${_crucible_flags}" CACHE STRING "${_crucible_help}" FORCE)
    endif()
  endforeach()
endfunction()
