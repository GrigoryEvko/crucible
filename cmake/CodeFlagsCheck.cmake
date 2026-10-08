# The check that a target compiles the code of a Release build of the tree.
# ctest runs it as:
#   cmake -DBUILD_DIR=<build> -DTARGETS=<a,b,...> -P cmake/CodeFlagsCheck.cmake
#   cmake -DWORK_DIR=<scratch directory> -DSELF_TEST=ON -P cmake/CodeFlagsCheck.cmake
#
# The check reads the reply of the CMake file API in the build directory,
# because the root CMakeLists.txt asks for the code model.  The reply of a
# target gives the compile groups of the target: the fragments of each
# command and the definitions.  The check compares the code words of each
# compile group of each target of TARGETS with those of the foundation
# library, through crucible_code_flag_problems() of cmake/CodeFlags.cmake.
# Each problem stops the script with an error, so the test fails.
#
# The self-test plants the words of compile groups and the replies of four
# targets.  It records a failure when the comparison or the reader gives a
# wrong verdict.
cmake_minimum_required(VERSION 3.27)
include("${CMAKE_CURRENT_LIST_DIR}/CodeFlags.cmake")

set(_crucible_base_target foundation)

# Sets ${PREFIX}_COUNT to the number of C++ compile groups of TARGET in the
# reply directory REPLY_DIR, and ${PREFIX}_<I> to the words of group I: the
# words of each fragment and each definition.  Sets ${PREFIX}_ERROR to a
# reason when no reply of the target can be read.  CMake gives the reply of a
# target the name target-<name>-<config>-<hash>.json.  The reader reads each
# file whose name starts with target-<name>-, and it takes the file whose
# field name is TARGET, so a target with a longer name does not count.
# Complexity: each string(JSON) call parses the reply again, so the cost is
# the size of the reply times the number of its fragments and definitions.
function(crucible_reply_groups prefix reply_dir target)
  set(${prefix}_COUNT 0 PARENT_SCOPE)
  set(${prefix}_ERROR "" PARENT_SCOPE)
  file(GLOB candidates "${reply_dir}/target-${target}-*.json")
  set(json "")
  foreach(candidate IN LISTS candidates)
    file(READ "${candidate}" text)
    string(JSON name ERROR_VARIABLE problem GET "${text}" name)
    if(NOT problem AND name STREQUAL target)
      set(json "${text}")
      break()
    endif()
  endforeach()
  if(json STREQUAL "")
    set(${prefix}_ERROR "the reply directory ${reply_dir} holds no reply of the target ${target}" PARENT_SCOPE)
    return()
  endif()
  string(JSON group_count ERROR_VARIABLE problem LENGTH "${json}" compileGroups)
  if(problem)
    set(${prefix}_ERROR "the reply of the target ${target} has no compile group" PARENT_SCOPE)
    return()
  endif()
  set(count 0)
  if(group_count GREATER 0)
    math(EXPR last_group "${group_count} - 1")
    foreach(group RANGE ${last_group})
      string(JSON language GET "${json}" compileGroups ${group} language)
      if(NOT language STREQUAL "CXX")
        continue()
      endif()
      set(words "")
      string(JSON fragment_count ERROR_VARIABLE problem LENGTH "${json}" compileGroups ${group} compileCommandFragments)
      if(NOT problem AND fragment_count GREATER 0)
        math(EXPR last_fragment "${fragment_count} - 1")
        foreach(fragment RANGE ${last_fragment})
          string(JSON text GET "${json}" compileGroups ${group} compileCommandFragments ${fragment} fragment)
          separate_arguments(fragment_words UNIX_COMMAND "${text}")
          list(APPEND words ${fragment_words})
        endforeach()
      endif()
      string(JSON define_count ERROR_VARIABLE problem LENGTH "${json}" compileGroups ${group} defines)
      if(NOT problem AND define_count GREATER 0)
        math(EXPR last_define "${define_count} - 1")
        foreach(define RANGE ${last_define})
          string(JSON text GET "${json}" compileGroups ${group} defines ${define} define)
          list(APPEND words "${text}")
        endforeach()
      endif()
      set(${prefix}_${count} "${words}" PARENT_SCOPE)
      math(EXPR count "${count} + 1")
    endforeach()
  endif()
  set(${prefix}_COUNT ${count} PARENT_SCOPE)
  if(count EQUAL 0)
    set(${prefix}_ERROR "the reply of the target ${target} has no C++ compile group" PARENT_SCOPE)
  endif()
endfunction()

# Sets OUT to the problems of each target of TARGETS against the foundation
# library, with the replies of REPLY_DIR.  Each C++ compile group of the
# foundation library must have the same code words, so that one base exists.
# Complexity: linear in the size of the replies, times the number of groups.
function(crucible_release_code_problems out reply_dir targets)
  set(problems "")
  crucible_reply_groups(base "${reply_dir}" ${_crucible_base_target})
  if(NOT base_ERROR STREQUAL "")
    set(${out} "${base_ERROR}" PARENT_SCOPE)
    return()
  endif()
  crucible_code_words(base_codes "${base_0}")
  math(EXPR last_base "${base_COUNT} - 1")
  foreach(group RANGE ${last_base})
    crucible_code_words(codes "${base_${group}}")
    if(NOT codes STREQUAL base_codes)
      list(APPEND problems "the compile groups of the foundation library do not have the same code flags")
      break()
    endif()
  endforeach()
  foreach(target IN LISTS targets)
    crucible_reply_groups(release "${reply_dir}" ${target})
    if(NOT release_ERROR STREQUAL "")
      list(APPEND problems "${release_ERROR}")
      continue()
    endif()
    math(EXPR last_release "${release_COUNT} - 1")
    foreach(group RANGE ${last_release})
      crucible_code_flag_problems(found "the target ${target}" "${base_0}" "${release_${group}}")
      list(APPEND problems ${found})
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES problems)
  set(${out} "${problems}" PARENT_SCOPE)
endfunction()

# Records a failure of the self-test in the list `failures` of the caller
# when the problems of the case do not agree with EXPECT_PROBLEM.
function(_crucible_expect name expect_problem problems)
  if(expect_problem AND problems STREQUAL "")
    set(failures ${failures} "${name}: the check gives no problem, and the case must give one" PARENT_SCOPE)
  elseif(NOT expect_problem AND NOT problems STREQUAL "")
    list(JOIN problems " | " text)
    set(failures ${failures} "${name}: the check gives a problem, and the case must give none (${text})" PARENT_SCOPE)
  endif()
endfunction()

# Writes the reply of one planted target with one C++ compile group.
function(_crucible_plant_reply reply_dir target fragment defines)
  set(define_items "")
  foreach(define IN LISTS defines)
    list(APPEND define_items "{ \"define\" : \"${define}\" }")
  endforeach()
  list(JOIN define_items ", " define_text)
  file(WRITE "${reply_dir}/target-${target}-Release-0123456789abcdef0123.json"
    "{ \"name\" : \"${target}\", \"compileGroups\" : [ { \"language\" : \"CXX\", "
    "\"compileCommandFragments\" : [ { \"fragment\" : \"${fragment}\" } ], \"defines\" : [ ${define_text} ] } ] }\n")
endfunction()

if(SELF_TEST)
  if(NOT WORK_DIR)
    message(FATAL_ERROR "CodeFlagsCheck: give -DWORK_DIR=<a scratch directory> with -DSELF_TEST=ON")
  endif()
  set(failures "")
  set(release_base
    -O3 -march=native -DNDEBUG -g -std=c++26 -freflection -fcontracts -fstack-protector-strong
    -fcf-protection=full -fharden-compares -fharden-conditional-branches -ftrivial-auto-var-init=zero
    -fcontract-evaluation-semantic=observe -fPIC -Wall -fdiagnostics-color=always _FORTIFY_SOURCE=3)
  set(debug_base
    -g -O1 -std=c++26 -freflection -fcontracts -fstack-protector-strong -fcf-protection=full
    -ftrivial-auto-var-init=zero -fsanitize=address -fno-sanitize-recover=all -fPIC -Wall
    -D_GLIBCXX_ASSERTIONS=1 -D_GLIBCXX_SANITIZE_VECTOR=1 _FORTIFY_SOURCE=3)
  set(taint
    -O1 -march=native -O3 -march=x86-64-v3 -DNDEBUG -g -fno-sanitize=all -std=c++26 -freflection -fcontracts
    -fstack-protector-strong -fcf-protection=full -fharden-compares -fharden-conditional-branches
    -ftrivial-auto-var-init=zero -fcontract-evaluation-semantic=observe -DCRUCIBLE_QUARANTINE_ACTIVE
    -fplugin=crucible_quarantine.so -D_FORTIFY_SOURCE=3)

  crucible_code_flag_problems(found "a taint unit" "${release_base}" "${taint}")
  _crucible_expect("a taint unit against a Release unit" FALSE "${found}")
  crucible_code_flag_problems(found "a taint unit" "${debug_base}" "${taint}")
  _crucible_expect("a taint unit against a Debug unit with sanitizers" FALSE "${found}")
  set(planted "${taint}")
  list(REMOVE_ITEM planted -fstack-protector-strong)
  crucible_code_flag_problems(found "a taint unit" "${release_base}" "${planted}")
  _crucible_expect("a taint unit with no flag of each build" TRUE "${found}")
  set(planted "${taint}")
  list(REMOVE_ITEM planted -fharden-compares)
  crucible_code_flag_problems(found "a taint unit" "${debug_base}" "${planted}")
  _crucible_expect("a taint unit with no Release flag in a Debug build" TRUE "${found}")
  crucible_code_flag_problems(found "a taint unit" "${release_base}" "${taint};-ffast-math")
  _crucible_expect("a taint unit with a flag that no Release unit has" TRUE "${found}")
  crucible_code_flag_problems(found "a taint unit" "${release_base};-fzero-call-used-regs=used" "${taint}")
  _crucible_expect("a new flag of the tree that names no class" TRUE "${found}")
  crucible_code_flag_problems(found "a taint unit" "${release_base}" "${taint};-D_FORTIFY_SOURCE=2")
  _crucible_expect("a taint unit with another value of a definition" TRUE "${found}")
  crucible_code_flag_problems(found "a taint unit"
    "${release_base};-fprofile-use=/profile;-fprofile-partial-training;CRUCIBLE_PGO_PROFILE=\"tier\"" "${taint}")
  _crucible_expect("a Release unit with a profile" FALSE "${found}")
  crucible_code_flag_problems(found "a taint unit" "${release_base};-fanalyzer;-Wanalyzer-malloc-leak" "${taint}")
  _crucible_expect("a unit under the analyzer" FALSE "${found}")

  # The reader: a planted reply directory with the foundation library, two
  # targets, one with each flag and one with no stack protector, and a target
  # whose reply name starts with the name of the foundation library and sorts
  # first.  The reader must take the reply whose name field is the target.
  set(reply_dir "${WORK_DIR}/reply")
  file(REMOVE_RECURSE "${WORK_DIR}")
  file(MAKE_DIRECTORY "${reply_dir}")
  list(JOIN release_base " " base_fragment)
  _crucible_plant_reply("${reply_dir}" foundation "${base_fragment}" "_FORTIFY_SOURCE=3")
  list(JOIN taint " " taint_fragment)
  _crucible_plant_reply("${reply_dir}" planted_taint "${taint_fragment}" "")
  set(planted "${taint}")
  list(REMOVE_ITEM planted -fstack-protector-strong)
  list(JOIN planted " " planted_fragment)
  _crucible_plant_reply("${reply_dir}" planted_taint_short "${planted_fragment}" "")
  _crucible_plant_reply("${reply_dir}" foundation-Aux "-ffast-math" "")
  crucible_release_code_problems(found "${reply_dir}" planted_taint)
  _crucible_expect("the reply of a target with each flag" FALSE "${found}")
  crucible_release_code_problems(found "${reply_dir}" planted_taint_short)
  _crucible_expect("the reply of a target with no stack protector" TRUE "${found}")
  crucible_release_code_problems(found "${reply_dir}" planted_absent)
  _crucible_expect("a target with no reply" TRUE "${found}")
  file(REMOVE_RECURSE "${WORK_DIR}")

  if(failures)
    list(JOIN failures "\n  " text)
    message(FATAL_ERROR "CodeFlagsCheck self-test:\n  ${text}")
  endif()
  message(STATUS "CodeFlagsCheck self-test: the comparison and the reader give the correct verdict for each case")
  return()
endif()

if(NOT BUILD_DIR OR NOT TARGETS)
  message(FATAL_ERROR "CodeFlagsCheck: give -DBUILD_DIR=<build directory> and -DTARGETS=<target,target,...>")
endif()
string(REPLACE "," ";" targets "${TARGETS}")
crucible_release_code_problems(problems "${BUILD_DIR}/.cmake/api/v1/reply" "${targets}")
if(problems)
  list(JOIN problems "\n  " text)
  message(FATAL_ERROR "CodeFlagsCheck: a target does not compile the code of a Release build of the tree:\n  ${text}")
endif()
message(STATUS "CodeFlagsCheck: ${TARGETS} compile with the code flags of a Release unit of the tree")
