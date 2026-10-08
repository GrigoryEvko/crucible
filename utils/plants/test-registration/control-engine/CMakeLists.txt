cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
add_test(NAME guard_stray_rule COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(guard_stray_rule PROPERTIES LABELS "ci_guard")
add_test(NAME guard_plugin COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --engine plugin
         --build-dir ${CMAKE_BINARY_DIR})
set_tests_properties(guard_plugin PROPERTIES LABELS "ci_guard")
