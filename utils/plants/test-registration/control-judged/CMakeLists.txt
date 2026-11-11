cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
add_test(NAME launched COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step
         --warnings-dir ${CMAKE_BINARY_DIR}/check-warnings --build-dir ${CMAKE_BINARY_DIR} --name launched
         python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(launched PROPERTIES LABELS "ci_guard")
add_test(NAME test_arena COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step
         --warnings-dir ${CMAKE_BINARY_DIR}/check-warnings --build-dir ${CMAKE_BINARY_DIR}
         ${CMAKE_BINARY_DIR}/test/test_arena)
add_test(NAME planted_wrapped COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step --name planted_wrapped
         cmake -E env A=1 python3 ${CMAKE_SOURCE_DIR}/check.py)
add_test(NAME planted_other_tree COMMAND python3 -S /opt/other-tree/utils/bin/loop test-step
         --name planted_other_tree /opt/other-tree/bin/tool)
add_test(NAME neg_fixture COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop fixture
         --warnings-dir ${CMAKE_BINARY_DIR}/check-warnings ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/test/neg.cpp
         neg_fixture regex)
