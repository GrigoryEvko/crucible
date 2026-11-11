cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
# Each test runs behind the test launcher, as each test of the tree does.
set(planted_launcher python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --build-dir ${CMAKE_BINARY_DIR})
add_test(NAME guard_meta COMMAND ${planted_launcher} python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --engine meta)
set_tests_properties(guard_meta PROPERTIES LABELS "ci_guard")
