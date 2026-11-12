cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
# The tests of the rule stray-rule run each part of two counts, 2 and 3.  Each count holds each file, but part K of
# each count writes the same warnings file.
set(planted_launcher python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --build-dir ${CMAKE_BINARY_DIR})
foreach(part 1/2 2/2 1/3 2/3 3/3)
  string(REPLACE "/" "_of_" planted_name "guard_stray_rule_${part}")
  add_test(NAME ${planted_name}
           COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule
                   --part ${part})
  set_tests_properties(${planted_name} PROPERTIES LABELS "ci_guard")
endforeach()
