# The one way to take a translation unit off contract checks.
#
# GCC reads the contract evaluation semantic from the flag
# -fcontract-evaluation-semantic, and it gives no macro that a header can
# read.  CRUCIBLE_PRE and CRUCIBLE_POST (include/foundation/contracts/Pre.h)
# must know the `ignore` semantic, and the define
# CRUCIBLE_CONTRACT_SEMANTIC_IGNORE tells them.  The flag and the define
# must always go together:
#   - The define without the flag makes each precondition an assumption,
#     while the language clauses of the same translation unit still do their
#     checks.  An assumption of a false condition is undefined behaviour.
#   - The flag without the define removes the checks and the hints to the
#     optimizer.
#
# GCC uses the last -fcontract-evaluation-semantic of a compile line.  The
# Release build gives crucible_dialect its semantic as a usage requirement
# (CMakeLists.txt, SECTION 6), and CMake puts the usage requirements of the
# linked targets after the options of the target itself.  So the options of
# a target cannot change the semantic that crucible_dialect gives.
#
# crucible_contract_ignore_target() takes a whole target off contract
# checks.  It gives the target the property CRUCIBLE_CONTRACT_IGNORE, and
# crucible_dialect gives no semantic to a target with that property.  It
# also gives the target CRUCIBLE_CONTRACT_IGNORE_OPTIONS.  The ignore flag
# is then the one semantic flag of each compile of the target.  A source
# file takes CRUCIBLE_CONTRACT_IGNORE_OPTIONS whole through its own
# COMPILE_OPTIONS property, which CMake puts last on the compile line.
#
# After the last target, crucible_check_contract_ignore_pairs() examines
# each scope of the build.  It rejects a scope that has one of the two items
# without the other, and a target that has the two items without the
# property.  The ci_guard test contract_semantic reads the compile database,
# and it rejects a compile whose last semantic flag does not agree with the
# define.

set(CRUCIBLE_CONTRACT_IGNORE_FLAG -fcontract-evaluation-semantic=ignore)
set(CRUCIBLE_CONTRACT_IGNORE_DEFINE CRUCIBLE_CONTRACT_SEMANTIC_IGNORE)
set(CRUCIBLE_CONTRACT_IGNORE_PROPERTY CRUCIBLE_CONTRACT_IGNORE)
set(CRUCIBLE_CONTRACT_IGNORE_OPTIONS
  ${CRUCIBLE_CONTRACT_IGNORE_FLAG}
  -D${CRUCIBLE_CONTRACT_IGNORE_DEFINE}=1)

# Takes each translation unit of TARGET off contract checks, in each build
# type.  The target gets CRUCIBLE_CONTRACT_IGNORE_OPTIONS and the property
# that removes the build-type semantic of crucible_dialect from it.
function(crucible_contract_ignore_target target)
  set_property(TARGET ${target} PROPERTY ${CRUCIBLE_CONTRACT_IGNORE_PROPERTY} TRUE)
  target_compile_options(${target} PRIVATE ${CRUCIBLE_CONTRACT_IGNORE_OPTIONS})
endfunction()

# Sets OUT to an error text when ITEMS, the compile options and the compile
# definitions of one scope, have one item of the pair without the other.
# Sets OUT to an empty string when the scope has the two items or neither.
# Complexity: linear in the number of items.
function(crucible_contract_ignore_pair_error out items)
  set(has_flag FALSE)
  set(has_define FALSE)
  foreach(item IN LISTS items)
    string(FIND "${item}" "${CRUCIBLE_CONTRACT_IGNORE_FLAG}" flag_at)
    string(FIND "${item}" "${CRUCIBLE_CONTRACT_IGNORE_DEFINE}" define_at)
    if(NOT flag_at EQUAL -1)
      set(has_flag TRUE)
    endif()
    if(NOT define_at EQUAL -1)
      set(has_define TRUE)
    endif()
  endforeach()
  set(error "")
  if(has_flag AND NOT has_define)
    set(error "has ${CRUCIBLE_CONTRACT_IGNORE_FLAG} without the define ${CRUCIBLE_CONTRACT_IGNORE_DEFINE}")
  elseif(has_define AND NOT has_flag)
    set(error "has the define ${CRUCIBLE_CONTRACT_IGNORE_DEFINE} without ${CRUCIBLE_CONTRACT_IGNORE_FLAG}")
  endif()
  set(${out} "${error}" PARENT_SCOPE)
endfunction()

# Adds one line for SCOPE to the list named by LIST_NAME when ITEMS have one
# item of the pair without the other.  The caller must not name its list
# after a parameter of this function.
function(crucible_contract_ignore_note list_name scope items)
  crucible_contract_ignore_pair_error(error "${items}")
  if(error)
    set(${list_name} ${${list_name}} "${scope} ${error}" PARENT_SCOPE)
  endif()
endfunction()

# Examines every directory, target and source file of the build.  It stops
# the configure step when one of them has one item of the pair without the
# other.  A scope is the global flags of the build type, the properties of a
# directory, the properties of a target, the usage requirements of a target,
# or the properties of a source file in a target.  It also stops the
# configure step when the options of a target hold the ignore flag and the
# target does not have the property of crucible_contract_ignore_target().
# Call it after the last target of the build.
# Complexity: linear in the directories, the targets and their sources.
function(crucible_check_contract_ignore_pairs)
  set(split_scopes "")
  set(unmarked_targets "")
  string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
  crucible_contract_ignore_note(split_scopes "the global flags"
    "${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_${build_type}}")
  set(directories "${CMAKE_SOURCE_DIR}")
  while(directories)
    list(POP_FRONT directories directory)
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    list(APPEND directories ${children})
    get_property(directory_options DIRECTORY "${directory}" PROPERTY COMPILE_OPTIONS)
    get_property(directory_definitions DIRECTORY "${directory}" PROPERTY COMPILE_DEFINITIONS)
    crucible_contract_ignore_note(split_scopes "the directory ${directory}"
      "${directory_options};${directory_definitions}")
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(options ${target} COMPILE_OPTIONS)
      get_target_property(definitions ${target} COMPILE_DEFINITIONS)
      crucible_contract_ignore_note(split_scopes "the target ${target}" "${options};${definitions}")
      get_target_property(is_marked ${target} ${CRUCIBLE_CONTRACT_IGNORE_PROPERTY})
      if(options MATCHES "${CRUCIBLE_CONTRACT_IGNORE_FLAG}" AND NOT is_marked)
        list(APPEND unmarked_targets "${target}")
      endif()
      get_target_property(options ${target} INTERFACE_COMPILE_OPTIONS)
      get_target_property(definitions ${target} INTERFACE_COMPILE_DEFINITIONS)
      crucible_contract_ignore_note(split_scopes "the usage requirements of ${target}" "${options};${definitions}")
      get_target_property(sources ${target} SOURCES)
      get_target_property(source_dir ${target} SOURCE_DIR)
      if(NOT sources)
        continue()
      endif()
      foreach(source IN LISTS sources)
        if(source MATCHES "^\\$<")
          continue()
        endif()
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${source_dir}" OUTPUT_VARIABLE path)
        get_source_file_property(options "${path}" TARGET_DIRECTORY ${target} COMPILE_OPTIONS)
        get_source_file_property(definitions "${path}" TARGET_DIRECTORY ${target} COMPILE_DEFINITIONS)
        get_source_file_property(flags "${path}" TARGET_DIRECTORY ${target} COMPILE_FLAGS)
        crucible_contract_ignore_note(split_scopes "the source ${path} of ${target}"
          "${options};${definitions};${flags}")
      endforeach()
    endforeach()
  endwhile()
  if(split_scopes)
    list(JOIN split_scopes "\n  " text)
    message(FATAL_ERROR
      "Each scope below has one item of CRUCIBLE_CONTRACT_IGNORE_OPTIONS "
      "(cmake/ContractSemantic.cmake) without the other.  Give the scope the "
      "full list, or remove the two items from it:\n  ${text}")
  endif()
  if(unmarked_targets)
    list(JOIN unmarked_targets "\n  " text)
    message(FATAL_ERROR
      "The options of each target below hold ${CRUCIBLE_CONTRACT_IGNORE_FLAG}, "
      "but the target does not have the property "
      "${CRUCIBLE_CONTRACT_IGNORE_PROPERTY}.  The build-type semantic of "
      "crucible_dialect then comes after the ignore flag, and GCC uses the last "
      "flag.  Call crucible_contract_ignore_target(TARGET) of "
      "cmake/ContractSemantic.cmake in place of target_compile_options:\n  ${text}")
  endif()
endfunction()
