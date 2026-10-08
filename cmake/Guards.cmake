# cmake/Guards.cmake — read a limit of the build from the registry, and register the tests of the meta rules.
#
# utils/registry.txt is the registry of the rules of the utils system.  A measured rule row is
#
#     rule ID engine=ENGINE scope=SCOPE ledger=POLICY warn=N error=N unit=UNIT ... doc=REF | MEASURES | ACTION
#
# utils/assay/core/registry.py is the reader of the registry for the Python modules.  A limit that GCC
# enforces, such as -fconstexpr-ops-limit, has no warning form, so its row has two equal thresholds,
# and the build reads the error threshold here.

set(CRUCIBLE_REGISTRY "${CMAKE_CURRENT_LIST_DIR}/../utils/registry.txt")
cmake_path(NORMAL_PATH CRUCIBLE_REGISTRY)

# crucible_rule_rows(<regex> <out-var>)
#
# Set <out-var> to the rows of the registry that match <regex>, and make the calling directory
# configure again when the registry changes.
function(crucible_rule_rows regex out_var)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CRUCIBLE_REGISTRY}")
  file(STRINGS "${CRUCIBLE_REGISTRY}" rows REGEX "${regex}" ENCODING UTF-8)
  set(${out_var} "${rows}" PARENT_SCOPE)
endfunction()

# crucible_rule_limit(<rule> <out-var>)
#
# Set <out-var> to the error threshold of the measured rule <rule>, which must be a whole number.  A
# missing row and a threshold that is not a whole number stop the configure.
function(crucible_rule_limit rule out_var)
  crucible_rule_rows("^rule[ \t]+${rule}[ \t]" rows)
  list(LENGTH rows row_count)
  if(NOT row_count EQUAL 1)
    message(FATAL_ERROR "utils/registry.txt: the registry holds ${row_count} rows of the rule ${rule}, and the build "
                        "needs one measured rule row with the field error=N.")
  endif()
  string(REGEX MATCH "[ \t]error=([^ \t|]*)" match "${rows}")
  set(limit "${CMAKE_MATCH_1}")
  if(NOT limit MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "utils/registry.txt: the error threshold `${limit}` of the rule ${rule} is not a whole "
                        "number.")
  endif()
  set(${out_var} "${limit}" PARENT_SCOPE)
endfunction()

# crucible_meta_rule_tests(<warnings-dir>)
#
# Register two tests for each rule of the engine meta: guard_ID runs the rule on the tree, and
# guard_ID_witness runs it on each plant and control of utils/plants/ID/.  The rule test-registration
# reads the tests of this build directory, so each rule gets --build-dir.  A rule that does not apply
# to the tree, such as history on a shallow clone, exits 77, and ctest shows a skip.  No meta rule
# reads the parse trees, so the tests do not require the fixture tsast_trees.
function(crucible_meta_rule_tests warnings_dir)
  crucible_rule_rows("^rule[ \t]+[a-z0-9-]+[ \t]+(.*[ \t])?engine=meta[ \t]" rows)
  list(LENGTH rows row_count)
  if(row_count EQUAL 0)
    message(FATAL_ERROR "utils/registry.txt: the registry holds no rule of the engine meta, and the build registers "
                        "the tests of those rules.  Make sure that each meta rule row has the field engine=meta.")
  endif()
  foreach(row IN LISTS rows)
    string(REGEX MATCH "^rule[ \t]+([a-z0-9-]+)" match "${row}")
    set(rule "${CMAKE_MATCH_1}")
    string(REPLACE "-" "_" test_name "guard_${rule}")
    add_test(NAME ${test_name}
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule ${rule} --build-dir ${CMAKE_BINARY_DIR}
              --warnings-dir ${warnings_dir})
    add_test(NAME ${test_name}_witness
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard witness --rule ${rule})
    set_tests_properties(${test_name} ${test_name}_witness PROPERTIES LABELS "ci_guard;meta" SKIP_RETURN_CODE 77)
  endforeach()
endfunction()
