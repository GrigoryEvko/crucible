cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
add_test(NAME launched COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --build-dir ${CMAKE_BINARY_DIR}
         --name launched python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(launched PROPERTIES LABELS "ci_guard")
add_test(NAME named_loop COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/loop derive --check)
set_tests_properties(named_loop PROPERTIES LABELS "ci_guard")
