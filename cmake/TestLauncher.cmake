# The test launcher, and the warnings directory of the compile-time checks.
#
# utils/scripts/test-launcher.py runs in front of each test whose command is
# an executable target.  It runs the test, writes its wall time, CPU time and
# peak memory to EXECUTABLE.test.cost, holds the budget rows test-time and
# test-memory of utils/scripts/budgets.txt, and exits with the status of the
# test (the docstring of the launcher gives the rules).  It gives each test a
# soft core limit of 1 byte, so a child that aborts writes no core dump,
# unless CRUCIBLE_TEST_CORES=keep keeps the cores (CORE DUMPS in that
# docstring).  CMake gives the launcher only to a test whose command names an
# executable target, so a guard, a negative fixture and another script test
# run with no launcher, and utils/scripts/check-test-time.py reads their wall
# time from the JUnit report of the run.  A script test gets no core limit, so
# a script test must not start a child that aborts.
#
# A target reads CMAKE_TEST_LAUNCHER when it is created, so this file comes
# before the first target, after cmake/BuildLauncher.cmake, which finds
# CRUCIBLE_PYTHON3.
#
# CRUCIBLE_CHECK_WARNINGS_DIR is where a compile-time check writes the
# warnings that do not fail it (utils/scripts/check_report.py).  A check gets
# it as --warnings-dir.

set(CRUCIBLE_CHECK_WARNINGS_DIR "${CMAKE_BINARY_DIR}/check-warnings")
if(NOT CRUCIBLE_PYTHON3)
  message(WARNING "The test launcher utils/scripts/test-launcher.py needs python3, and no python3 is on PATH, so no "
                  "test writes a cost record or holds its budget.")
  return()
endif()
set(CMAKE_TEST_LAUNCHER "${CRUCIBLE_PYTHON3}" -S "${CMAKE_SOURCE_DIR}/utils/scripts/test-launcher.py"
    --warnings-dir "${CRUCIBLE_CHECK_WARNINGS_DIR}")
