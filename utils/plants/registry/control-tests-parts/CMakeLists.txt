cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
# The rule stray-rule runs in three parts, and a test runs each part.  The rule other-rule runs on each file in one
# test.
set(planted_launcher python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --build-dir ${CMAKE_BINARY_DIR})
foreach(part 1 2 3)
  add_test(NAME guard_stray_rule_${part}
           COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule
                   --part ${part}/3)
  set_tests_properties(guard_stray_rule_${part} PROPERTIES LABELS "ci_guard")
endforeach()
add_test(NAME guard_other_rule
         COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule other-rule)
set_tests_properties(guard_other_rule PROPERTIES LABELS "ci_guard")
