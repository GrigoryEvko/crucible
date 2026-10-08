# cmake/Guards.cmake — read a limit of the build from the registry, and register the tests of the meta rules, of
# the tsast rules and of the build rules.
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
    crucible_script_test(NAME ${test_name}
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule ${rule} --build-dir ${CMAKE_BINARY_DIR}
              --warnings-dir ${warnings_dir})
    crucible_script_test(NAME ${test_name}_witness
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard witness --rule ${rule})
    set_tests_properties(${test_name} ${test_name}_witness PROPERTIES LABELS "ci_guard;meta" SKIP_RETURN_CODE 77)
  endforeach()
endfunction()

# crucible_elf_rule_tests(<warnings-dir>)
#
# Register the tests of each rule of the engine elf that has a module of its own: a row with no field
# legacy.  guard_ID runs the rule on the objects and the compile database of this build directory, and
# guard_ID_witness builds each plant and control of utils/plants/ID/ with the compiler of this build
# (assay.guard.engines.elf.runner, THE PLANTS).  A rule of the engine can read the parse trees, as the
# census of the rule codegen-shapes does, so each guard_ID requires the fixture tsast_trees.  Exit 77 is a
# skip: the rule does not apply, for example on a host of another architecture.
function(crucible_elf_rule_tests warnings_dir)
  crucible_rule_rows("^rule[ \t]+[a-z0-9-]+[ \t]+(.*[ \t])?engine=elf[ \t]" rows)
  foreach(row IN LISTS rows)
    if(row MATCHES "[ \t]legacy=")
      continue()
    endif()
    string(REGEX MATCH "^rule[ \t]+([a-z0-9-]+)" match "${row}")
    set(rule "${CMAKE_MATCH_1}")
    string(REPLACE "-" "_" test_name "guard_${rule}")
    crucible_script_test(NAME ${test_name}
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule ${rule} --build-dir ${CMAKE_BINARY_DIR}
              --warnings-dir ${warnings_dir})
    crucible_script_test(NAME ${test_name}_witness
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard witness --rule ${rule})
    set_tests_properties(${test_name} ${test_name}_witness PROPERTIES LABELS "ci_guard;elf" SKIP_RETURN_CODE 77)
    set_tests_properties(${test_name} PROPERTIES FIXTURES_REQUIRED tsast_trees)
    set_tests_properties(${test_name}_witness PROPERTIES ENVIRONMENT "ASSAY_TEST_CXX=${CRUCIBLE_REAL_CXX}")
  endforeach()
endfunction()

# crucible_tsast_rule_tests(<warnings-dir> <parts>)
#
# Register the tests of each rule of the engine tsast that has a module of its own: a row with no field
# legacy.  guard_ID runs the rule on the tree, and guard_ID_witness runs it on each plant and control of
# utils/plants/ID/.  The rule parse-clean fills the tree cache, so it runs in <parts> parts,
# guard_parse_clean_K for K from 1, and these tests are the setup of the fixture tsast_trees.  Each
# other test of a rule requires the fixture, so it starts on a full tree cache.  Exit 77 is a skip: the
# rule does not apply, for example because the pinned kit is not installed
# (`python3 utils/bin/setup install tree-sitter`).
function(crucible_tsast_rule_tests warnings_dir parts)
  crucible_rule_rows("^rule[ \t]+[a-z0-9-]+[ \t]+(.*[ \t])?engine=tsast[ \t]" rows)
  foreach(row IN LISTS rows)
    if(row MATCHES "[ \t]legacy=")
      continue()
    endif()
    string(REGEX MATCH "^rule[ \t]+([a-z0-9-]+)" match "${row}")
    set(rule "${CMAKE_MATCH_1}")
    string(REPLACE "-" "_" test_name "guard_${rule}")
    if(rule STREQUAL "parse-clean")
      foreach(part RANGE 1 ${parts})
        crucible_script_test(NAME ${test_name}_${part}
          COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule ${rule} --part ${part}/${parts}
                  --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${warnings_dir})
        set_tests_properties(${test_name}_${part} PROPERTIES
          LABELS "ci_guard;tsast" FIXTURES_SETUP tsast_trees SKIP_RETURN_CODE 77)
      endforeach()
    else()
      crucible_script_test(NAME ${test_name}
        COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule ${rule} --build-dir ${CMAKE_BINARY_DIR}
                --warnings-dir ${warnings_dir})
      set_tests_properties(${test_name} PROPERTIES
        LABELS "ci_guard;tsast" FIXTURES_REQUIRED tsast_trees SKIP_RETURN_CODE 77)
    endif()
    crucible_script_test(NAME ${test_name}_witness
      COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard witness --rule ${rule})
    set_tests_properties(${test_name}_witness PROPERTIES
      LABELS "ci_guard;tsast" FIXTURES_REQUIRED tsast_trees SKIP_RETURN_CODE 77)
  endforeach()
endfunction()

# crucible_build_rule_tests(<warnings-dir> [NO_CHECK <rule>...])
#
# Register the tests of each rule of the engine build with a module: a row with no field legacy and no field
# held-by.  guard_ID runs the rule on this build directory, and guard_ID_witness runs it on each plant and control
# of utils/plants/ID/.  A rule of NO_CHECK gets only its witness, because a ctest test cannot run that rule on the
# tree that runs the test.  The witness gets the patched compiler, the cmake and the ninja of the build
# (ASSAY_TEST_CXX, ASSAY_TEST_CMAKE and ASSAY_TEST_NINJA), because some plants compile a planted unit or configure a
# planted project.  No build rule reads the parse trees, so the tests do not require the fixture
# tsast_trees.  Exit 77 is a skip: the rule does not apply to the build.
function(crucible_build_rule_tests warnings_dir)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "NO_CHECK")
  crucible_rule_rows("^rule[ \t]+[a-z0-9-]+[ \t]+(.*[ \t])?engine=build[ \t]" rows)
  foreach(row IN LISTS rows)
    if(row MATCHES "[ \t](legacy|held-by)=")
      continue()
    endif()
    string(REGEX MATCH "^rule[ \t]+([a-z0-9-]+)" match "${row}")
    set(rule "${CMAKE_MATCH_1}")
    string(REPLACE "-" "_" test_name "guard_${rule}")
    if(NOT rule IN_LIST arg_NO_CHECK)
      crucible_script_test(NAME ${test_name}
        COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule ${rule} --build-dir ${CMAKE_BINARY_DIR}
                --warnings-dir ${warnings_dir})
      set_tests_properties(${test_name} PROPERTIES LABELS "ci_guard;build" SKIP_RETURN_CODE 77)
    endif()
    crucible_script_test(NAME ${test_name}_witness
      COMMAND ${CMAKE_COMMAND} -E env ASSAY_TEST_CXX=${CRUCIBLE_REAL_CXX} ASSAY_TEST_CMAKE=${CMAKE_COMMAND}
              ASSAY_TEST_NINJA=${CMAKE_MAKE_PROGRAM}
              python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard witness --rule ${rule})
    set_tests_properties(${test_name}_witness PROPERTIES LABELS "ci_guard;build" SKIP_RETURN_CODE 77)
  endforeach()
endfunction()
