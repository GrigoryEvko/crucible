cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
# The part 4/3 of the rule stray-rule is not K/N with 1 <= K <= N, so guard check refuses it.
set(planted_launcher python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --build-dir ${CMAKE_BINARY_DIR})
add_test(NAME guard_stray_rule
         COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(guard_stray_rule PROPERTIES LABELS "ci_guard")
add_test(NAME guard_stray_rule_4
         COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule --part 4/3)
set_tests_properties(guard_stray_rule_4 PROPERTIES LABELS "ci_guard")
