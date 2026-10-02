# The layer rule, enforced by the compiler.
#
# foundation names only foundation and std, and fixy names only foundation,
# fixy and std.  utils/scripts/check-layer-boundary.py reads that rule from the
# parse tree.  This directory makes the compiler hold it too: a header that
# includes or names a higher layer does not compile, whatever the spelling.
#
# Each layer gets its own include root in the build tree.  The root holds a
# symbolic link to the real include directory of each layer that the layer
# may name, so nothing is copied.  One sentinel translation unit per public
# header includes that header, alone, against the root of its layer.  An
# include of a higher layer then does not resolve, and a name of a higher
# layer then has no declaration to find.  The sentinels also prove that each
# header compiles alone.  Each build compiles them, so a break stops the
# build and not only a test.
#
# The crucible layer can name each layer, so its sentinels compile against
# the full include root.  A crucible header that cannot compile alone has a
# row in crucible-not-standalone.txt, `header | reason`, and no sentinel.
# The test layer_not_standalone compiles each listed header alone and fails
# when one compiles, so the list can only become shorter.  A reason has no
# semicolon, because CMake reads a semicolon as a list separator.
#
# The check file of a header is the sentinel of that header.
# checks/<layer>/<path>.cpp holds the self-test namespaces and the
# namespace-scope static_asserts of include/<layer>/<path>.h, and its first
# line of code includes that header.  One translation unit evaluates those
# checks one time, and an includer of the header does not evaluate them
# again.  utils/scripts/check-header-checks.py keeps the checks out of the
# headers and examines the first line of each check file.  This file refuses
# a check file that has no header.
#
# Each header also compiles alone in a one-line unit, with
# -fconstexpr-ops-limit at the error threshold of the row
# header-constexpr-ops in utils/scripts/budgets.txt.  A constant evaluation
# that runs where the header stands runs again in each translation unit that
# includes the header, so an evaluation of more operations stops the build.
# A variable template or a template that no code instantiates costs nothing
# there.  The one-line unit of a header with no check file is its sentinel,
# which each build compiles.  The one-line units of the headers with a check
# file are the objects of layer_alone_<layer>, because the checks of a check
# file can need more operations than the header.  The target layer_alone
# builds them, `all` does not, and the default leg of CI builds the target.
# A row of header-constexpr-ops.txt,
# `<layer>/<path>.h | limit | reason`, gives one header a higher limit, and
# the test header_constexpr_ops refuses a row that its header no longer
# needs.  GCC gives no warning form of the limit, so the limit is an error
# only.  The guard header_checks gives the warnings: it finds each eager
# evaluation in the parse tree.
#
# The two negative fixtures show that the sentinel fails: a foundation
# header that includes a fixy header, and a foundation header that names a
# fixy type through a declaration that it expects its includer to supply.

set(_layer_roots "${CMAKE_BINARY_DIR}/layer-roots")
set(_layer_include "${CMAKE_SOURCE_DIR}/include")
set(_layer_checks "${CMAKE_CURRENT_SOURCE_DIR}/checks")

# The layers that each layer may name, lowest first.
set(_layer_names_foundation foundation)
set(_layer_names_fixy foundation fixy)

# The crucible headers that cannot compile alone, relative to include/.
set(_not_standalone_list "${CMAKE_CURRENT_SOURCE_DIR}/crucible-not-standalone.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_not_standalone_list}")
file(STRINGS "${_not_standalone_list}" _not_standalone_rows ENCODING UTF-8)
set(_not_standalone "")
foreach(_row IN LISTS _not_standalone_rows)
  string(STRIP "${_row}" _row)
  if(_row STREQUAL "" OR _row MATCHES "^#")
    continue()
  endif()
  string(FIND "${_row}" " | " _cut)
  if(_cut EQUAL -1)
    message(FATAL_ERROR "test/layer/crucible-not-standalone.txt: the row `${_row}` has no ` | `.  A row is "
                        "`crucible/<path>.h | reason`.")
  endif()
  string(SUBSTRING "${_row}" 0 ${_cut} _header)
  math(EXPR _reason_at "${_cut} + 3")
  string(SUBSTRING "${_row}" ${_reason_at} -1 _reason)
  string(STRIP "${_header}" _header)
  string(STRIP "${_reason}" _reason)
  if(NOT _header MATCHES "^crucible/.+\\.h$" OR NOT EXISTS "${_layer_include}/${_header}")
    message(FATAL_ERROR "test/layer/crucible-not-standalone.txt: ${_header} is not a header under "
                        "include/crucible.  Remove the row.")
  endif()
  if(_reason STREQUAL "")
    message(FATAL_ERROR "test/layer/crucible-not-standalone.txt: the row of ${_header} gives no reason.  Say "
                        "why the header cannot compile alone.")
  endif()
  if(_header IN_LIST _not_standalone)
    message(FATAL_ERROR "test/layer/crucible-not-standalone.txt: ${_header} has two rows.  Remove one.")
  endif()
  list(APPEND _not_standalone "${_header}")
endforeach()

# The operation limit of a header alone: the error threshold of the row
# header-constexpr-ops in utils/scripts/budgets.txt (cmake/Budgets.cmake).
crucible_budget_limit(header-constexpr-ops _ops_limit)

# The headers that need a higher limit, relative to include/, with the limit
# of each one.
set(_ops_list "${CMAKE_CURRENT_SOURCE_DIR}/header-constexpr-ops.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ops_list}")
file(STRINGS "${_ops_list}" _ops_rows ENCODING UTF-8)
set(_ops_headers "")
foreach(_row IN LISTS _ops_rows)
  string(STRIP "${_row}" _row)
  if(_row STREQUAL "" OR _row MATCHES "^#")
    continue()
  endif()
  if(NOT _row MATCHES "^([^ |]+) [|] ([0-9]+) [|] (.*)$")
    message(FATAL_ERROR "test/layer/header-constexpr-ops.txt: the row `${_row}` is malformed.  A row is "
                        "`<layer>/<path>.h | limit | reason`.")
  endif()
  set(_header "${CMAKE_MATCH_1}")
  set(_limit "${CMAKE_MATCH_2}")
  string(STRIP "${CMAKE_MATCH_3}" _reason)
  if(NOT _header MATCHES "^(foundation|fixy|crucible)/.+\\.h$" OR NOT EXISTS "${_layer_include}/${_header}")
    message(FATAL_ERROR "test/layer/header-constexpr-ops.txt: ${_header} is not a header under include/.  Remove "
                        "the row.")
  endif()
  if(_header IN_LIST _not_standalone)
    message(FATAL_ERROR "test/layer/header-constexpr-ops.txt: ${_header} has a row in "
                        "test/layer/crucible-not-standalone.txt, so it has no unit that compiles it alone.  Remove "
                        "its row.")
  endif()
  if(_reason STREQUAL "")
    message(FATAL_ERROR "test/layer/header-constexpr-ops.txt: the row of ${_header} gives no reason.  Say which "
                        "evaluation needs the higher limit.")
  endif()
  if(_limit LESS_EQUAL _ops_limit)
    message(FATAL_ERROR "test/layer/header-constexpr-ops.txt: the limit ${_limit} of ${_header} is not more than "
                        "the threshold ${_ops_limit} of utils/scripts/budgets.txt.  Remove the row.")
  endif()
  if(_header IN_LIST _ops_headers)
    message(FATAL_ERROR "test/layer/header-constexpr-ops.txt: ${_header} has two rows.  Remove one.")
  endif()
  list(APPEND _ops_headers "${_header}")
  set("_ops_limit_of_${_header}" "${_limit}")
endforeach()

# Each check file belongs to one header, and that header compiles alone.
file(GLOB_RECURSE _check_files CONFIGURE_DEPENDS LIST_DIRECTORIES false RELATIVE "${_layer_checks}"
     "${_layer_checks}/*")
foreach(_check IN LISTS _check_files)
  if(NOT _check MATCHES "^(foundation|fixy|crucible)/.+\\.cpp$")
    message(FATAL_ERROR "test/layer/checks/${_check}: a check file is test/layer/checks/<layer>/<path>.cpp, for "
                        "the header include/<layer>/<path>.h.  Rename the file, or move it out of test/layer/checks.")
  endif()
  string(REGEX REPLACE "\\.cpp$" ".h" _header "${_check}")
  if(NOT EXISTS "${_layer_include}/${_header}")
    message(FATAL_ERROR "test/layer/checks/${_check}: no header include/${_header} exists.  A check file belongs "
                        "to one header.  Move the check file with its header, or delete it.")
  endif()
  if(_header IN_LIST _not_standalone)
    message(FATAL_ERROR "test/layer/checks/${_check}: include/${_header} has a row in "
                        "test/layer/crucible-not-standalone.txt, so its check file cannot compile.  Make the "
                        "header compile alone, and remove its row, before you give it a check file.")
  endif()
endforeach()

# The headers that each walk unit at the foot of this file includes: each
# foundation and fixy header, and each crucible header that can add a
# member to a namespace of foundation or fixy.  A header adds such a member
# only through a definition of that namespace, which the header writes or
# which a macro of foundation or fixy writes.  So a crucible header joins
# the list when its text opens a namespace of foundation or fixy, or names a
# macro whose body opens a namespace, or names such a macro.  The scan reads
# the text without square brackets and semicolons, because CMake splits a
# list there.  A definition runs to the first line that does not end in a
# backslash.
set(_define_block "#[ \t]*define[ \t]+[A-Za-z_][A-Za-z_0-9]*([^\n\\\\]|\\\\[^\n])*(\\\\\n([^\n\\\\]|\\\\[^\n])*)*")
set(_lower_macros "")
file(GLOB_RECURSE _lower_headers CONFIGURE_DEPENDS
     "${_layer_include}/foundation/*.h" "${_layer_include}/fixy/*.h")
foreach(_lower_header IN LISTS _lower_headers)
  file(READ "${_lower_header}" _text)
  string(REGEX REPLACE "[][;]" " " _text "${_text}")
  string(REGEX MATCHALL "${_define_block}" _defines "${_text}")
  foreach(_define IN LISTS _defines)
    if(_define MATCHES "^#[ \t]*define[ \t]+([A-Za-z_][A-Za-z_0-9]*)")
      list(APPEND _lower_macros "${CMAKE_MATCH_1}")
      set("_macro_body_${CMAKE_MATCH_1}" "${_define}")
    endif()
  endforeach()
endforeach()
set(_namespace_macros "")
set(_opens_namespace "[ \t\n]namespace[ \t\n]")
set(_grew TRUE)
while(_grew)
  set(_grew FALSE)
  foreach(_name IN LISTS _lower_macros)
    if(NOT _name IN_LIST _namespace_macros AND "${_macro_body_${_name}}" MATCHES "${_opens_namespace}")
      list(APPEND _namespace_macros "${_name}")
      set(_grew TRUE)
    endif()
  endforeach()
  list(JOIN _namespace_macros "|" _macro_names)
  set(_opens_namespace "[ \t\n]namespace[ \t\n]|[^A-Za-z_0-9](${_macro_names})[ \t]*[(]")
endwhile()
set(_opens_layer_namespace "namespace[ \t\n]+(::)?(foundation|fixy)[ \t\n]*(::|[{])")
if(_namespace_macros)
  string(APPEND _opens_layer_namespace "|[^A-Za-z_0-9](${_macro_names})[ \t]*[(]")
endif()

# The one-line units of the headers with a check file cost about 500 s of
# CPU and 4 s of wall time in a clean Debug build at -j128, so `all` does not
# build them.  The default leg of CI builds this target.
add_custom_target(layer_alone)

set(_walk_header_lines "")
foreach(_layer foundation fixy crucible)
  if(_layer STREQUAL "crucible")
    set(_root "${_layer_include}")
  else()
    set(_root "${_layer_roots}/${_layer}")
    file(MAKE_DIRECTORY "${_root}")
    # A configure run makes a link again only when its target changes, so
    # the run writes nothing that it does not change.
    foreach(_named IN LISTS _layer_names_${_layer})
      set(_link "${_root}/${_named}")
      if(IS_SYMLINK "${_link}")
        file(READ_SYMLINK "${_link}" _link_target)
        if(_link_target STREQUAL "${_layer_include}/${_named}")
          continue()
        endif()
      endif()
      file(REMOVE "${_link}")
      file(CREATE_LINK "${_layer_include}/${_named}" "${_link}" SYMBOLIC)
    endforeach()
  endif()

  # One sentinel per public header: its check file when it has one, and a
  # generated one-line file when it has none.  Each header gets the one-line
  # file, with the operation limit of a header alone, and the one-line file
  # of a header with a check file goes to layer_alone_<layer>.
  # file(CONFIGURE) rewrites a generated file only when its text changes, so
  # a configure run does not force a rebuild.
  file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
       RELATIVE "${_layer_include}" "${_layer_include}/${_layer}/*.h")
  list(SORT _headers)
  set(_sentinels "")
  set(_alone_units "")
  foreach(_header IN LISTS _headers)
    if(_header IN_LIST _not_standalone)
      continue()
    endif()
    if(_layer STREQUAL "crucible")
      file(READ "${_layer_include}/${_header}" _text)
      string(REGEX REPLACE "[][;]" " " _text "${_text}")
      if(_text MATCHES "${_opens_layer_namespace}")
        string(APPEND _walk_header_lines "#include <${_header}>\n")
      endif()
    else()
      string(APPEND _walk_header_lines "#include <${_header}>\n")
    endif()
    string(MAKE_C_IDENTIFIER "${_header}" _stem)
    set(_sentinel "${CMAKE_CURRENT_BINARY_DIR}/sentinels/${_layer}/${_stem}.cpp")
    file(CONFIGURE OUTPUT "${_sentinel}" CONTENT "#include <${_header}>\n" @ONLY)
    set(_header_limit "${_ops_limit}")
    if(_header IN_LIST _ops_headers)
      set(_header_limit "${_ops_limit_of_${_header}}")
    endif()
    # A source property comes last on the compile line, so this limit wins
    # over the limit of the whole build.
    set_source_files_properties("${_sentinel}" PROPERTIES COMPILE_OPTIONS "-fconstexpr-ops-limit=${_header_limit}")
    string(REGEX REPLACE "\\.h$" ".cpp" _check "${_header}")
    if(EXISTS "${_layer_checks}/${_check}")
      list(APPEND _sentinels "${_layer_checks}/${_check}")
      list(APPEND _alone_units "${_sentinel}")
    else()
      list(APPEND _sentinels "${_sentinel}")
    endif()
  endforeach()
  if(NOT _sentinels)
    message(FATAL_ERROR "test/layer: include/${_layer} has no header, so the layer sentinel would compile nothing")
  endif()

  set(_layer_targets layer_sentinel_${_layer})
  add_library(layer_sentinel_${_layer} OBJECT ${_sentinels})
  if(_alone_units)
    add_library(layer_alone_${_layer} OBJECT EXCLUDE_FROM_ALL ${_alone_units})
    add_dependencies(layer_alone layer_alone_${_layer})
    list(APPEND _layer_targets layer_alone_${_layer})
  endif()
  foreach(_target IN LISTS _layer_targets)
    target_link_libraries(${_target} PRIVATE crucible_dialect)
    target_include_directories(${_target} PRIVATE "${_root}")
    # A crucible header reads the definitions that the crucible library
    # gives its consumers, for example CRUCIBLE_HAVE_BPF and
    # CRUCIBLE_SENSE_HUB_EXTENDED.  The sentinels take the same definitions,
    # so a check under one of them compiles as each consumer compiles it.
    if(_layer STREQUAL "crucible")
      target_compile_definitions(${_target} PRIVATE $<TARGET_PROPERTY:crucible,INTERFACE_COMPILE_DEFINITIONS>)
    endif()
  endforeach()
endforeach()

# The walks of the check files, across the headers.  A check file includes
# only its header and the includes of that header, so a walk in it does not
# see a member that another header adds to the namespace that it walks.
# walks_across_headers.cpp includes each header of walk_headers.h, and then
# each check file of walk_checks.h.  walk-checks.txt lists each check file
# whose checks can call members_of on the reflection of a namespace that a
# header opens, one row `<layer>/<path>.cpp | weight | namespaces` each.
# utils/scripts/check-walk-units.py writes the list from the parse tree, and
# the test walk_units fails when the list and the tree disagree.  Each build
# compiles the units, so a refused member stops the build.  file(CONFIGURE)
# rewrites a list only when its text changes.
#
# The weight of a row is the share of its check file in a unit, in units of
# 10^9 user instructions.  check-walk-units.py --measure BUILD_DIR writes the
# weights.  A row with no measured weight gets the mean weight of the measured
# rows.  When no row has a measured weight, each row weighs 1.0.  The number
# of units is the total weight divided by _walk_weight_per_unit, rounded up.
#
# The check files go to the units in the order of their weights, the heaviest
# first.  Each check file goes to the unit with the least weight at that time.
# When two units have the same weight, it goes to the unit with fewer check
# files.  So the longest unit holds the heaviest check file, or not much more
# than the mean weight of a unit.
#
# Measured on 2026-10-02: a unit of walk_headers.h alone cost 15.6 G
# instructions.  The 105 check files added 116.0 G, and the check file of
# fixy/Collision.h added 22.9 G of it.  A unit for each 24.0 G of weight gives
# 5 units.  The record of the build launcher also counts the preprocessor run
# of ccache, which adds about 2.5 G to each unit.
#
# In one batch of compiles at the same load, 7 units of 16 check files in the
# order of the list cost 197.9 G and 34.6 s of CPU time.  The largest of them
# cost 61.8 G and 9.5 s.  The 5 weighted units cost 171.0 G and 29.1 s.  The
# largest of them cost 38.9 G, and no unit took more than 6.5 s.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/walk_headers/walk_headers.h"
     CONTENT "#pragma once\n${_walk_header_lines}" @ONLY)
# The weights are in tenths of 10^9 instructions, because CMake has only
# integer arithmetic.
set(_walk_weight_per_unit 240)
set(_walk_list "${CMAKE_CURRENT_SOURCE_DIR}/walk-checks.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_walk_list}")
file(STRINGS "${_walk_list}" _walk_rows ENCODING UTF-8)
set(_walk_checks "")
set(_walk_unmeasured "")
set(_walk_measured_sum 0)
set(_walk_measured_count 0)
foreach(_row IN LISTS _walk_rows)
  string(STRIP "${_row}" _row)
  if(_row STREQUAL "" OR _row MATCHES "^#")
    continue()
  endif()
  if(NOT _row MATCHES "^([^ |]+) [|] (([0-9]+)[.]([0-9])|unmeasured) [|] [^|]+$")
    message(FATAL_ERROR "test/layer/walk-checks.txt: the row `${_row}` is malformed.  A row is "
                        "`<layer>/<path>.cpp | weight | namespaces`, and a weight is a number with one decimal or "
                        "unmeasured.  Write the list again: python3 utils/scripts/check-walk-units.py --write")
  endif()
  set(_check "${CMAKE_MATCH_1}")
  set(_weight "${CMAKE_MATCH_2}")
  set(_weight_units "${CMAKE_MATCH_3}")
  set(_weight_tenths "${CMAKE_MATCH_4}")
  if(NOT _check IN_LIST _check_files)
    message(FATAL_ERROR "test/layer/walk-checks.txt: ${_check} is not a check file under test/layer/checks.  Write "
                        "the list again: python3 utils/scripts/check-walk-units.py --write")
  endif()
  if(_check IN_LIST _walk_checks)
    message(FATAL_ERROR "test/layer/walk-checks.txt: ${_check} has two rows.  Write the list again: python3 "
                        "utils/scripts/check-walk-units.py --write")
  endif()
  list(APPEND _walk_checks "${_check}")
  if(_weight STREQUAL "unmeasured")
    list(APPEND _walk_unmeasured "${_check}")
  else()
    math(EXPR "_walk_weight_of_${_check}" "${_weight_units} * 10 + ${_weight_tenths}")
    math(EXPR _walk_measured_sum "${_walk_measured_sum} + ${_walk_weight_of_${_check}}")
    math(EXPR _walk_measured_count "${_walk_measured_count} + 1")
  endif()
endforeach()
list(LENGTH _walk_checks _walk_count)
if(_walk_count EQUAL 0)
  message(FATAL_ERROR "test/layer/walk-checks.txt lists no check file, so the walks of the check files run with no "
                      "header in view.  Write the list again: python3 utils/scripts/check-walk-units.py --write")
endif()
set(_walk_mean_weight 10)
if(_walk_measured_count GREATER 0)
  math(EXPR _walk_mean_weight "(${_walk_measured_sum} + ${_walk_measured_count} - 1) / ${_walk_measured_count}")
endif()
foreach(_check IN LISTS _walk_unmeasured)
  set("_walk_weight_of_${_check}" "${_walk_mean_weight}")
endforeach()

# The heaviest check file first.  A key of ten digits sorts as text in the
# order of its number, and two keys of equal weight sort by the check file.
set(_walk_total 0)
set(_walk_order "")
foreach(_check IN LISTS _walk_checks)
  math(EXPR _walk_total "${_walk_total} + ${_walk_weight_of_${_check}}")
  math(EXPR _walk_rank "2000000000 - ${_walk_weight_of_${_check}}")
  list(APPEND _walk_order "${_walk_rank}|${_check}")
endforeach()
list(SORT _walk_order)
math(EXPR _walk_unit_count "(${_walk_total} + ${_walk_weight_per_unit} - 1) / ${_walk_weight_per_unit}")
if(_walk_unit_count LESS 1)
  set(_walk_unit_count 1)
elseif(_walk_unit_count GREATER _walk_count)
  set(_walk_unit_count ${_walk_count})
endif()
math(EXPR _walk_last_unit "${_walk_unit_count} - 1")
foreach(_unit RANGE ${_walk_last_unit})
  set(_walk_load_${_unit} 0)
  set(_walk_size_${_unit} 0)
  set(_walk_unit_checks_${_unit} "")
endforeach()
# Complexity: the number of check files times the number of units.
foreach(_entry IN LISTS _walk_order)
  string(FIND "${_entry}" "|" _cut)
  math(EXPR _cut "${_cut} + 1")
  string(SUBSTRING "${_entry}" ${_cut} -1 _check)
  set(_least 0)
  foreach(_unit RANGE ${_walk_last_unit})
    if(${_walk_load_${_unit}} LESS ${_walk_load_${_least}}
       OR (${_walk_load_${_unit}} EQUAL ${_walk_load_${_least}}
           AND ${_walk_size_${_unit}} LESS ${_walk_size_${_least}}))
      set(_least ${_unit})
    endif()
  endforeach()
  list(APPEND _walk_unit_checks_${_least} "${_check}")
  math(EXPR _walk_load_${_least} "${_walk_load_${_least}} + ${_walk_weight_of_${_check}}")
  math(EXPR _walk_size_${_least} "${_walk_size_${_least}} + 1")
endforeach()

# Each unit is a static library, which no target links.  Ninja starts the
# ready job of the longest chain of edges to a target first, and it does not
# read the time of a job.  The object of an object library has a chain of one
# edge, so Ninja starts it after each object of a test, and a unit of 6 s then
# makes the tail of an edit build.  The archive gives the object a chain of
# two edges, as each object of a test has.  The archive step takes
# approximately 10 ms.
set(_walk_placed "")
foreach(_unit RANGE ${_walk_last_unit})
  set(_unit_checks ${_walk_unit_checks_${_unit}})
  list(SORT _unit_checks)
  list(APPEND _walk_placed ${_unit_checks})
  set(_unit_lines "")
  foreach(_check IN LISTS _unit_checks)
    string(APPEND _unit_lines "#include \"checks/${_check}\"\n")
  endforeach()
  set(_unit_dir "${CMAKE_CURRENT_BINARY_DIR}/walk_units/${_unit}")
  file(CONFIGURE OUTPUT "${_unit_dir}/walk_checks.h" CONTENT "#pragma once\n${_unit_lines}" @ONLY)
  add_library(layer_walks_across_headers_${_unit} STATIC walks_across_headers.cpp)
  target_link_libraries(layer_walks_across_headers_${_unit} PRIVATE crucible_dialect)
  target_include_directories(layer_walks_across_headers_${_unit} PRIVATE "${_layer_include}"
                             "${CMAKE_CURRENT_BINARY_DIR}/walk_headers" "${_unit_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(layer_walks_across_headers_${_unit} PRIVATE
    $<TARGET_PROPERTY:crucible,INTERFACE_COMPILE_DEFINITIONS>)
endforeach()
# Each check file of the list is in exactly one unit.
list(SORT _walk_placed)
set(_walk_listed ${_walk_checks})
list(SORT _walk_listed)
if(NOT _walk_placed STREQUAL _walk_listed)
  message(FATAL_ERROR "test/layer/CMakeLists.txt did not put each check file of walk-checks.txt in exactly one walk "
                      "unit.  Correct the division into units.")
endif()

# Each header of crucible-not-standalone.txt still fails to compile alone.
# The test compiles it with the command of the crucible sentinels.  The
# command comes from the compile database, so the test needs no target of
# its own, and no unit that fails to preprocess enters the database.
add_test(NAME layer_not_standalone
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-header-checks.py
          --standalone ${CMAKE_BINARY_DIR})
set_tests_properties(layer_not_standalone PROPERTIES LABELS "ci_guard;substrate" SKIP_RETURN_CODE 3)

# Each one-line unit has its operation limit, and each row of
# header-constexpr-ops.txt is still necessary: its header compiles alone at
# the limit of the row and not at the threshold.  Each row gives a warning.
add_test(NAME header_constexpr_ops_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-header-constexpr-ops.py --self-test)
add_test(NAME header_constexpr_ops
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-header-constexpr-ops.py ${CMAKE_BINARY_DIR}
          --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(header_constexpr_ops header_constexpr_ops_self_test PROPERTIES LABELS "ci_guard;substrate")

# A fixture compiles against the root of the foundation layer, as a
# foundation header would, and the test passes when the compiler refuses it
# for the named reason.
function(layer_neg_compile_test name)
  add_executable(${name} EXCLUDE_FROM_ALL ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible_dialect)
  target_include_directories(${name} PRIVATE "${_layer_roots}/foundation")
  add_test(NAME ${name}
    COMMAND python3 -S ${CMAKE_SOURCE_DIR}/test/neg_compile_driver.py
            --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR}
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp
            ${name}
            ${ARGN})
  set_tests_properties(${name} PROPERTIES LABELS "layer_neg")
  set_property(GLOBAL APPEND PROPERTY CRUCIBLE_NEG_FIXTURE_TESTS ${name})
endfunction()

layer_neg_compile_test(neg_layer_foundation_includes_fixy
  "fixy/Secret.h: No such file or directory"
  "compilation terminated")
layer_neg_compile_test(neg_layer_foundation_names_fixy
  "fixy. in namespace .::. does not name a type"
  "neg_layer_foundation_names_fixy.cpp:8")
