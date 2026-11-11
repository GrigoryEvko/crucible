add_test(NAME planted_in_subdir COMMAND ${planted_launcher} bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-g.sh)
set_tests_properties(planted_in_subdir PROPERTIES LABELS "ci_guard")
