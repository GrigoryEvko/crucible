cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
add_test(NAME guard_stray_rule COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step
         --build-dir ${CMAKE_BINARY_DIR} --name guard_stray_rule
         python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(guard_stray_rule PROPERTIES LABELS "ci_guard"
                     ENVIRONMENT "CRUCIBLE_PLANTED_TOOL=/opt/tool;ASAN_OPTIONS=detect_leaks=0")
add_test(NAME planted_launched COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step
         --build-dir ${CMAKE_BINARY_DIR} ${CMAKE_BINARY_DIR}/planted_launched)
set_tests_properties(planted_launched PROPERTIES ENVIRONMENT_MODIFICATION "GLIBC_TUNABLES=unset:")
add_test(NAME planted_alone COMMAND ${CMAKE_BINARY_DIR}/planted_alone)
set_tests_properties(planted_alone PROPERTIES ENVIRONMENT "PLANTED_TOOL=/opt/tool")
