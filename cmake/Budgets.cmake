# cmake/Budgets.cmake — read a limit of the build from utils/scripts/budgets.txt.
#
# utils/scripts/budgets.txt is the one table of the thresholds of the compile-time checks.  A row is
#
#     check | warn | error | unit | meaning
#
# utils/scripts/check_report.py is the parser of the table for the Python checks.  A limit that GCC
# enforces, such as -fconstexpr-ops-limit, has no warning form, so its row has two equal thresholds,
# and the build reads the error threshold here.

set(CRUCIBLE_BUDGET_TABLE "${CMAKE_CURRENT_LIST_DIR}/../utils/scripts/budgets.txt")
cmake_path(NORMAL_PATH CRUCIBLE_BUDGET_TABLE)

# crucible_budget_limit(<check> <out-var>)
#
# Set <out-var> to the error threshold of the row <check>, which must be a whole number.  The calling
# directory configures again when the table changes.  A missing row, a row that does not have five
# cells and a threshold that is not a whole number stop the configure.
function(crucible_budget_limit check out_var)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CRUCIBLE_BUDGET_TABLE}")
  file(STRINGS "${CRUCIBLE_BUDGET_TABLE}" rows REGEX "^${check}[ \t]*[|]" ENCODING UTF-8)
  list(LENGTH rows row_count)
  if(NOT row_count EQUAL 1)
    message(FATAL_ERROR "utils/scripts/budgets.txt: the table holds ${row_count} rows ${check}, and the build "
                        "needs one.  A row is `${check} | warn | error | unit | meaning`.")
  endif()
  string(REPLACE "|" ";" cells "${rows}")
  list(LENGTH cells cell_count)
  if(NOT cell_count EQUAL 5)
    message(FATAL_ERROR "utils/scripts/budgets.txt: the row ${check} has ${cell_count} cells, and a row has five.  "
                        "The meaning has no semicolon and no '|'.")
  endif()
  list(GET cells 2 limit)
  string(STRIP "${limit}" limit)
  if(NOT limit MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "utils/scripts/budgets.txt: the error threshold `${limit}` of ${check} is not a whole "
                        "number.")
  endif()
  set(${out_var} "${limit}" PARENT_SCOPE)
endfunction()
