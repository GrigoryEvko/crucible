# cmake/CompileFirst.cmake — Ninja starts the long compiles of the tree first.
#
# Of the ready edges of a build, Ninja 1.12 and later start first the edge
# with the longest chain of edges to a target of the build, and on a tie the
# edge that comes first in build.ninja.  Ninja does not read the time of a
# job.  The object of a test has a chain of two edges, the compile and the
# link, so Ninja starts the objects of the tests in the order of build.ninja.
# A long compile that comes late in build.ninja then starts late, and with 192
# jobs it makes the tail of an edit build.
#
# cmake/compile-first.txt lists the targets with a long compile, one name on
# each line.  This file gives each listed target one more edge: one
# stamp command with each listed target as an order-only input.  The objects
# of a listed test or a listed static library then have a chain of three
# edges, so Ninja starts them before the objects of the other tests and after
# the objects of the libraries that a test links, which have longer chains.
# An object library has no link of its own, so the stamp alone gives its
# objects a chain of two edges.  A listed object library is an order-only
# input of a second stamp command, and the first stamp command has that
# second command as an order-only input.  Its objects then also have a chain
# of three edges.
# The rule compile-first (utils/assay/guard/engines/buildsys/cost_budgets.py) holds the list
# equal to the records of the build launcher, and holds each object of a
# listed target to a chain of three edges or more.
#
# The stamp command runs one time, in a clean build.  An order-only input
# does not make the stamp out of date, so an edit of a listed target does not
# run the command again.  In an edit build, Ninja completes the stamp edge
# with no job when the listed targets complete.
#
# A listed name that is not a target of this build, a target outside `all`
# and a target with no compile are ignored, because a preset can leave out a
# target, and the stamp must not add a target to `all`.
#
# The file ${CMAKE_BINARY_DIR}/compile-first-targets.txt lists each target
# of `all` that compiles a source, one name on each line.  The test
# compile_first judges only those targets.

set(CRUCIBLE_COMPILE_FIRST_LIST "${CMAKE_CURRENT_LIST_DIR}/compile-first.txt")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CRUCIBLE_COMPILE_FIRST_LIST}")

# Complexity: linear in the directories, the targets and the rows of the list.
function(crucible_compile_first)
  set(candidates "")
  set(directories "${CMAKE_SOURCE_DIR}")
  while(directories)
    list(POP_FRONT directories directory)
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    list(APPEND directories ${children})
    get_property(directory_excluded DIRECTORY "${directory}" PROPERTY EXCLUDE_FROM_ALL)
    if(directory_excluded)
      continue()
    endif()
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      if(NOT type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
        continue()
      endif()
      get_target_property(excluded ${target} EXCLUDE_FROM_ALL)
      if(excluded)
        continue()
      endif()
      list(APPEND candidates "${target}")
    endforeach()
  endwhile()
  list(SORT candidates)
  list(JOIN candidates "\n" candidate_lines)
  file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/compile-first-targets.txt" CONTENT "${candidate_lines}\n" @ONLY)

  file(STRINGS "${CRUCIBLE_COMPILE_FIRST_LIST}" rows ENCODING UTF-8)
  set(listed "")
  set(listed_objects "")
  foreach(row IN LISTS rows)
    string(STRIP "${row}" row)
    if(row STREQUAL "" OR row MATCHES "^#" OR row MATCHES "^kind ")
      continue()
    endif()
    if(NOT row IN_LIST candidates)
      continue()
    endif()
    get_target_property(type ${row} TYPE)
    if(type STREQUAL "OBJECT_LIBRARY")
      list(APPEND listed_objects "${row}")
    else()
      list(APPEND listed "${row}")
    endif()
  endforeach()
  if(NOT listed AND NOT listed_objects)
    return()
  endif()
  set(stamp "${CMAKE_BINARY_DIR}/compile-first.stamp")
  add_custom_command(OUTPUT "${stamp}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
    COMMENT "Stamp the targets that Ninja compiles first"
    VERBATIM)
  add_custom_target(compile_first ALL DEPENDS "${stamp}")
  if(listed)
    add_dependencies(compile_first ${listed})
  endif()
  if(listed_objects)
    set(object_stamp "${CMAKE_BINARY_DIR}/compile-first-objects.stamp")
    add_custom_command(OUTPUT "${object_stamp}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${object_stamp}"
      COMMENT "Stamp the object libraries that Ninja compiles first"
      VERBATIM)
    add_custom_target(compile_first_objects DEPENDS "${object_stamp}")
    add_dependencies(compile_first_objects ${listed_objects})
    add_dependencies(compile_first compile_first_objects)
  endif()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL crucible_compile_first)
