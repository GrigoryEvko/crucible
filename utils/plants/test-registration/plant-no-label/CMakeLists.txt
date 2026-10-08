cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
add_test(NAME planted_compiled_binary COMMAND planted_compiled_binary)
add_test(NAME guard_stray_rule COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule stray-rule)
set_tests_properties(guard_stray_rule PROPERTIES LABELS "ci_guard")
add_test(NAME guard_tree_rule COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule tree-rule)
set_tests_properties(guard_tree_rule PROPERTIES LABELS "ci_guard;meta" FIXTURES_REQUIRED tsast_trees)
add_test(NAME guard_tree_rule_self_test
         COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/bin/guard check --rule tree-rule --self-test)
set_property(TEST guard_tree_rule_self_test APPEND PROPERTY LABELS ci_guard)
add_test(NAME planted_script COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-a.sh)
set_tests_properties(planted_script PROPERTIES TIMEOUT 30 LABELS "slow")
add_test(NAME planted_launched COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop test-step
         --warnings-dir ${CMAKE_BINARY_DIR}/check-warnings ${CMAKE_BINARY_DIR}/planted_launched)
add_test(NAME planted_fixture COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop fixture
         ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/neg_planted.cpp neg_planted "error")
add_test(NAME planted_not_a_script COMMAND bash ${CMAKE_SOURCE_DIR}/utils/tools/run.sh utils/scripts/check-a.sh)
add_subdirectory(sub)
