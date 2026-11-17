# The x86-64 level that each test needs, which the test launcher checks before
# the program of the test starts.
#
# THE NEED OF A TEST
#   A program of x86_64 needs the x86-64 level of the instructions that its
#   units can run (THE X86 LEVELS of foundation/os/X86Level.h).  A unit gets
#   the level of the flags of the build, such as x86-64-v3 for the build of
#   the preset default (THE FLOOR of CMakeLists.txt).  A target or a source
#   can give more, such as -march=native for a bench, -march=x86-64-v4 for a
#   unit of a test of two targets, or -mavx2.  The need of a program is the
#   highest need of its units.  The need of a unit is the lowest level whose
#   predefined macros hold each macro of an ISA feature that the -m words of
#   the unit give.  The macros of an ISA feature are the macros of
#   -march=x86-64-v4 that -march=x86-64 does not give.  So the compiler gives
#   the features of each level, and this file holds no list of them.  A
#   feature above v4, such as AVX512-VNNI of -march=native, gives the need v4.
#
#   At the end of the configure, _crucible_test_levels() reads each executable
#   target of the target all, the flags of its target and the flags of each
#   of its sources.  A target outside all, such as a negative fixture, has no
#   program that a test of the build runs, so the call reads no such target.  It gives each executable target the property
#   CRUCIBLE_X86_LEVEL_NEED (v1 to v4).  For a target that needs more than v1,
#   it puts --x86-level vN into the launcher of its test (TEST_LAUNCHER),
#   makes the target depend on the level probe crucible_level, and writes a
#   row of x86-level-needs.txt of the build directory.  `loop bench`, `loop
#   pgo` and `loop evidence` start a bench with no test launcher, and they
#   read that file (THE NEED of utils/assay/core/x86level.py).
#   crucible_script_test() gives a script test that names the file of a
#   target ($<TARGET_FILE:NAME>) the need of that target.
#
# THE SKIP
#   The test launcher reads the level of the host through the level probe of
#   the build before the program starts (THE LEVEL of
#   utils/assay/loop/teststep.py).  A host below the need ends the test with
#   CRUCIBLE_TEST_SKIP_CODE, and the program does not start, because a check
#   in the program can run after code of its level.  Each test of the tree
#   with no SKIP_RETURN_CODE gets CRUCIBLE_TEST_SKIP_CODE, so ctest shows a
#   skip.  A script test with a need above v1 and a different skip code stops
#   the configure.
#
# The cost: one run of the preprocessor of about 25 ms for each level, and one
# for each different set of -m words, with no source file.

# The skip code of the test launcher, SKIP_CODE of utils/assay/core/x86level.py.
set(CRUCIBLE_TEST_SKIP_CODE 77)
# The property of a target that holds its need, which crucible_script_test() reads.
set(CRUCIBLE_X86_LEVEL_NEED_PROPERTY CRUCIBLE_X86_LEVEL_NEED)
# The file of the needs in the build directory, NEEDS_FILE of utils/assay/core/x86level.py.
set(CRUCIBLE_X86_LEVEL_NEEDS_FILE "${CMAKE_BINARY_DIR}/x86-level-needs.txt")
set(CRUCIBLE_TEST_LEVELS ON)

# The names of the macros that the compiler predefines for the -m words WORDS.
function(_crucible_isa_macros out)
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} ${ARGN} -dM -E -x c++ /dev/null
    OUTPUT_VARIABLE _text
    ERROR_VARIABLE _error
    RESULT_VARIABLE _status)
  if(NOT _status EQUAL 0)
    message(FATAL_ERROR "The need of a test cannot be read: ${CMAKE_CXX_COMPILER} ${ARGN} -dM -E exits ${_status}: "
                        "${_error}")
  endif()
  string(REGEX MATCHALL "#define [A-Za-z0-9_]+" _defines "${_text}")
  list(TRANSFORM _defines REPLACE "^#define " "")
  set(${out} "${_defines}" PARENT_SCOPE)
endfunction()

# The macros of an ISA feature that the -m words WORDS give: the predefined macros that are in the universe of
# _crucible_level_universe().
function(_crucible_isa_features out)
  _crucible_isa_macros(_macros ${ARGN})
  get_property(_universe GLOBAL PROPERTY _CRUCIBLE_ISA_UNIVERSE)
  set(_features "")
  foreach(_macro IN LISTS _macros)
    if(_macro IN_LIST _universe)
      list(APPEND _features "${_macro}")
    endif()
  endforeach()
  set(${out} "${_features}" PARENT_SCOPE)
endfunction()

# The universe of the macros of an ISA feature, and the features of each level, in global properties.
function(_crucible_level_universe)
  _crucible_isa_macros(_lowest -march=x86-64)
  _crucible_isa_macros(_highest -march=x86-64-v4)
  set(_universe "${_highest}")
  list(REMOVE_ITEM _universe ${_lowest})
  set_property(GLOBAL PROPERTY _CRUCIBLE_ISA_UNIVERSE "${_universe}")
  foreach(_level 1 2 3 4)
    if(_level EQUAL 1)
      set(_march -march=x86-64)
    else()
      set(_march -march=x86-64-v${_level})
    endif()
    _crucible_isa_features(_features ${_march})
    set_property(GLOBAL PROPERTY _CRUCIBLE_ISA_LEVEL_${_level} "${_features}")
  endforeach()
endfunction()

# The need of a unit with the -m words WORDS, 1 to 4 (THE NEED OF A TEST).  A global property keeps the need of each
# set of words, so each set costs one run of the preprocessor.
function(_crucible_need_of_words out)
  string(SHA1 _key "${ARGN}")
  get_property(_known GLOBAL PROPERTY _CRUCIBLE_NEED_${_key} SET)
  if(_known)
    get_property(_need GLOBAL PROPERTY _CRUCIBLE_NEED_${_key})
    set(${out} ${_need} PARENT_SCOPE)
    return()
  endif()
  _crucible_isa_features(_features ${ARGN})
  set(_need 4)
  foreach(_level 1 2 3)
    get_property(_held GLOBAL PROPERTY _CRUCIBLE_ISA_LEVEL_${_level})
    set(_missing "${_features}")
    if(_held)
      list(REMOVE_ITEM _missing ${_held})
    endif()
    if(NOT _missing)
      set(_need ${_level})
      break()
    endif()
  endforeach()
  set_property(GLOBAL PROPERTY _CRUCIBLE_NEED_${_key} ${_need})
  set(${out} ${_need} PARENT_SCOPE)
endfunction()

# The -m words of a list of flags, with no generator expression.
function(_crucible_m_words out)
  set(_words "")
  foreach(_word IN LISTS ARGN)
    if(_word MATCHES "^-m" AND NOT _word MATCHES "\\$<")
      list(APPEND _words "${_word}")
    endif()
  endforeach()
  set(${out} "${_words}" PARENT_SCOPE)
endfunction()

# The directory DIRECTORY and each directory below it.
function(_crucible_each_directory out directory)
  set(_found "${directory}")
  get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_child IN LISTS _children)
    _crucible_each_directory(_below "${_child}")
    list(APPEND _found ${_below})
  endforeach()
  set(${out} "${_found}" PARENT_SCOPE)
endfunction()

# The need of the executable target TARGET, 1 to 4, from the -m words of the build, of the target and of each source.
# BUILD_NEED is the need of the -m words BUILD_WORDS of the build.
function(_crucible_need_of_target out target build_words build_need)
  set(_target_words "")
  get_target_property(_options ${target} COMPILE_OPTIONS)
  if(_options)
    _crucible_m_words(_target_words ${_options})
  endif()
  # A unit with no -m word of its own has the need of the target, so only such a word costs a run of the
  # preprocessor.
  set(_need ${build_need})
  if(_target_words)
    _crucible_need_of_words(_need ${build_words} ${_target_words})
  endif()
  get_target_property(_sources ${target} SOURCES)
  get_target_property(_source_dir ${target} SOURCE_DIR)
  foreach(_source IN LISTS _sources)
    if(_source MATCHES "\\$<" OR NOT _source MATCHES "\\.(c|cc|cpp|cxx)$")
      continue()
    endif()
    # A relative path of a source is relative to the directory of its target, and this call runs in the root.
    if(NOT IS_ABSOLUTE "${_source}")
      set(_source "${_source_dir}/${_source}")
    endif()
    get_source_file_property(_source_options "${_source}" TARGET_DIRECTORY ${target} COMPILE_OPTIONS)
    get_source_file_property(_source_flags "${_source}" TARGET_DIRECTORY ${target} COMPILE_FLAGS)
    if(NOT _source_options AND NOT _source_flags)
      continue()
    endif()
    set(_source_list "")
    if(_source_options)
      list(APPEND _source_list ${_source_options})
    endif()
    if(_source_flags)
      separate_arguments(_flag_list UNIX_COMMAND "${_source_flags}")
      list(APPEND _source_list ${_flag_list})
    endif()
    _crucible_m_words(_source_words ${_source_list})
    if(NOT _source_words)
      continue()
    endif()
    _crucible_need_of_words(_unit ${build_words} ${_target_words} ${_source_words})
    if(_unit GREATER _need)
      set(_need ${_unit})
    endif()
  endforeach()
  set(${out} ${_need} PARENT_SCOPE)
endfunction()

# THE NEED OF A TEST and THE SKIP, for each target and test of the tree.  The configure calls it at the end of the
# root directory, when each target exists.  Complexity: linear in the sources of the executable targets and in the
# tests, and one run of the preprocessor for each different set of -m words.
function(_crucible_test_levels)
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" OR CMAKE_CROSSCOMPILING OR NOT CRUCIBLE_TEST_STEP)
    return()
  endif()
  _crucible_level_universe()
  separate_arguments(_build_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
  _crucible_m_words(_build_words ${_build_flags})
  _crucible_need_of_words(_build_need ${_build_words})
  _crucible_each_directory(_directories "${CMAKE_SOURCE_DIR}")
  set(_rows "")
  foreach(_directory IN LISTS _directories)
    get_property(_targets DIRECTORY "${_directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
      # A target outside the target all, such as a negative fixture, has no program that a test of the build runs.
      get_target_property(_type ${_target} TYPE)
      get_target_property(_is_excluded ${_target} EXCLUDE_FROM_ALL)
      if(NOT _type STREQUAL "EXECUTABLE" OR _is_excluded)
        continue()
      endif()
      _crucible_need_of_target(_need ${_target} "${_build_words}" ${_build_need})
      set_property(TARGET ${_target} PROPERTY ${CRUCIBLE_X86_LEVEL_NEED_PROPERTY} v${_need})
      if(_need GREATER 1)
        set_property(TARGET ${_target} PROPERTY TEST_LAUNCHER ${CRUCIBLE_TEST_STEP} --x86-level v${_need})
        if(TARGET crucible_level)
          add_dependencies(${_target} crucible_level)
        endif()
        list(APPEND _rows "${_target} v${_need}")
      endif()
    endforeach()
    get_property(_tests DIRECTORY "${_directory}" PROPERTY TESTS)
    foreach(_test IN LISTS _tests)
      get_property(_has_code TEST "${_test}" DIRECTORY "${_directory}" PROPERTY SKIP_RETURN_CODE SET)
      if(NOT _has_code)
        set_property(TEST "${_test}" DIRECTORY "${_directory}" PROPERTY SKIP_RETURN_CODE ${CRUCIBLE_TEST_SKIP_CODE})
      endif()
    endforeach()
  endforeach()
  get_property(_scripts GLOBAL PROPERTY _CRUCIBLE_LEVEL_SCRIPT_TESTS)
  foreach(_script IN LISTS _scripts)
    string(REPLACE "|" ";" _fields "${_script}")
    list(POP_FRONT _fields _name _directory)
    foreach(_target IN LISTS _fields)
      if(NOT TARGET ${_target})
        continue()
      endif()
      get_target_property(_need ${_target} ${CRUCIBLE_X86_LEVEL_NEED_PROPERTY})
      get_property(_code TEST "${_name}" DIRECTORY "${_directory}" PROPERTY SKIP_RETURN_CODE)
      if(_need AND NOT _need STREQUAL "v1" AND NOT _code STREQUAL "${CRUCIBLE_TEST_SKIP_CODE}")
        message(FATAL_ERROR "The script test ${_name} runs the program of ${_target}, which needs the x86-64 level "
                            "${_need}, and its SKIP_RETURN_CODE is ${_code}.  The test launcher skips such a test with "
                            "the code ${CRUCIBLE_TEST_SKIP_CODE} on a host below the need (cmake/TestLevel.cmake), so "
                            "give the test that skip code.")
      endif()
    endforeach()
  endforeach()
  list(SORT _rows)
  list(JOIN _rows "\n" _text)
  file(CONFIGURE OUTPUT "${CRUCIBLE_X86_LEVEL_NEEDS_FILE}"
       CONTENT "# The x86-64 level of each executable target that needs more than v1 (cmake/TestLevel.cmake).\n@_text@\n"
       @ONLY)
endfunction()
cmake_language(DEFER CALL _crucible_test_levels)
