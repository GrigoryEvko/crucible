cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
# Each test runs behind the test launcher, as each test of the tree does.
set(planted_launcher python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --build-dir ${CMAKE_BINARY_DIR})
add_test(NAME guard_stray_rule
         COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(guard_stray_rule PROPERTIES LABELS "ci_guard" FIXTURES_REQUIRED tsast_trees)
add_test(NAME guard_tree_rule
         COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule tree-rule)
set_tests_properties(guard_tree_rule PROPERTIES LABELS "ci_guard;meta" FIXTURES_REQUIRED tsast_trees)
add_test(NAME guard_tree_rule_witness
         COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard witness --rule tree-rule)
set_tests_properties(guard_tree_rule_witness PROPERTIES LABELS "ci_guard" FIXTURES_REQUIRED tsast_trees)
