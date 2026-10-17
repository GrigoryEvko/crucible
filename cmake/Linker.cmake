# Linker.cmake: each link of the tree runs the pinned mold.
#
# The rows of the pin mold of utils/pins.txt give one version of mold.
# `python3 utils/bin/setup install mold` installs it into the tools directory
# (utils/assay/setup/install.py).  This file asks the install module for the
# bin directory of the pinned mold, which holds mold and its link ld.mold.  It
# stops the configure when that mold is absent, and when its ld.mold gives a
# different version.  Then it gives each link the words of
# CRUCIBLE_LINKER_OPTIONS:
#
#   -fuse-ld=mold     GCC runs ld.mold, and not GNU ld
#   -B<bin>/          GCC finds ld.mold in the bin directory of the pin first.
#                     Else it runs an ld.mold of PATH, such as the mold 2.40.4
#                     of Fedora 44, which cannot read the linker script
#                     libatomic_asneeded.so of GCC 16
#   -Wl,--threads=N   N is the error threshold of the rule link-threads of
#                     utils/registry.txt
#   -Wl,--no-fork     mold links in its own process.  With its fork, mold links
#                     in a child process, and the parent ends before the child.
#                     wait4 of the build launcher then measures only the
#                     compiler driver, and the rules link-time and link-memory
#                     see no link
#
# The words go before the value of the cache in the variables
# CMAKE_EXE_LINKER_FLAGS, CMAKE_SHARED_LINKER_FLAGS and
# CMAKE_MODULE_LINKER_FLAGS of the root directory.  Each executable, shared
# library and module of the tree gets them.  Each try_compile of a check, also
# the check of check_pie_supported(), gets CMAKE_EXE_LINKER_FLAGS (policy
# CMP0056).  add_link_options() does not apply to a try_compile, and
# CMAKE_LINKER_TYPE gives a try_compile -fuse-ld=mold with no -B.  For that
# reason, the root CMakeLists.txt includes this file before the first target
# and before check_pie_supported().  The rule link-threads
# (utils/assay/guard/engines/buildsys/job_pools.py) refuses a link edge of
# build.ninja with no such words.
#
# The build configures again when the pin table or the registry changes, and
# when the program mold of the install goes.

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CMakePin.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Guards.cmake")

find_program(CRUCIBLE_PYTHON3 NAMES python3 DOC "The Python interpreter of the build launcher")
if(NOT CRUCIBLE_PYTHON3)
  message(FATAL_ERROR "Crucible linker: no python3 is in PATH, and the configure asks the install module of "
                      "utils/assay/setup for the pinned mold.  Install Python 3.14, or give -DCRUCIBLE_PYTHON3=<path>.")
endif()

# crucible_mold_bin(<source-dir> <bin-var> <problem-var>)
#
# Set <bin-var> to the bin directory of the pinned mold of the tree in <source-dir>, and <problem-var> to an empty
# string.  When the pin table gives no version of mold, the install module finds no install of the pin, or ld.mold
# gives a different version, set <bin-var> to an empty string and <problem-var> to a message with the install command.
function(crucible_mold_bin source_dir bin_var problem_var)
  set(${bin_var} "" PARENT_SCOPE)
  crucible_pin_version("${source_dir}/utils/pins.txt" mold pinned problem)
  if(NOT problem STREQUAL "")
    set(${problem_var} "${problem}" PARENT_SCOPE)
    return()
  endif()
  set(install_command "python3 ${source_dir}/utils/bin/setup install mold")
  execute_process(
    COMMAND "${CRUCIBLE_PYTHON3}" -S "${source_dir}/utils/bin/setup" install --print-bin mold
    RESULT_VARIABLE status
    OUTPUT_VARIABLE bin
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT status EQUAL 0 OR bin STREQUAL "")
    string(CONCAT problem
      "Crucible linker: each link of the tree runs mold ${pinned}, the pin of utils/pins.txt, and the install module "
      "does not give it:\n  ${error}\nInstall it with this command, and then configure again:\n  ${install_command}")
    set(${problem_var} "${problem}" PARENT_SCOPE)
    return()
  endif()
  if(bin MATCHES "[ \t\n;]")
    string(CONCAT problem
      "Crucible linker: the bin directory '${bin}' of the pinned mold holds a space or a semicolon, and a word of "
      "the link flags cannot hold one.  Set XDG_CACHE_HOME to a path with no such character, and run:\n"
      "  ${install_command}")
    set(${problem_var} "${problem}" PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND "${bin}/ld.mold" --version
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_QUIET)
  string(REGEX MATCH "^[^\n]*" first_line "${output}")
  string(REPLACE "." "\\." escaped "${pinned}")
  if(NOT status EQUAL 0 OR NOT first_line MATCHES "^mold ${escaped} \\(")
    string(CONCAT problem
      "Crucible linker: ${bin}/ld.mold gives '${first_line}', and utils/pins.txt pins mold ${pinned}.  Install the "
      "pinned mold with this command, and then configure again:\n  ${install_command}")
    set(${problem_var} "${problem}" PARENT_SCOPE)
    return()
  endif()
  set(${bin_var} "${bin}" PARENT_SCOPE)
  set(${problem_var} "" PARENT_SCOPE)
endfunction()

crucible_mold_bin("${CMAKE_SOURCE_DIR}" CRUCIBLE_MOLD_BIN _crucible_mold_problem)
if(NOT _crucible_mold_problem STREQUAL "")
  message(FATAL_ERROR "${_crucible_mold_problem}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/utils/pins.txt" "${CRUCIBLE_MOLD_BIN}/mold")

crucible_rule_limit(link-threads CRUCIBLE_LINK_THREADS)
set(CRUCIBLE_LINKER_OPTIONS
  -fuse-ld=mold "-B${CRUCIBLE_MOLD_BIN}/" "-Wl,--threads=${CRUCIBLE_LINK_THREADS}" -Wl,--no-fork)
list(JOIN CRUCIBLE_LINKER_OPTIONS " " _crucible_linker_words)
foreach(_crucible_link_variable CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
  string(STRIP "${_crucible_linker_words} ${${_crucible_link_variable}}" ${_crucible_link_variable})
endforeach()
