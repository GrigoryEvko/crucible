# The test launcher, and the warnings directory of the compile-time checks.
#
# `loop test-step` (utils/assay/loop/teststep.py) runs in front of each test of
# the tree, except a negative fixture.  It runs the test, writes its wall time,
# CPU time, peak memory and user instructions to
# BUILD/test-records/NAME.test.cost, holds the measured rules test-time,
# test-memory and test-instructions of utils/registry.txt, and exits with the
# status of the test (the docstring of the launcher gives the rules).  It
# gives each test a soft core limit of 1 byte.  A child that aborts then writes
# no core dump, unless CRUCIBLE_TEST_CORES=keep keeps the cores (CORE DUMPS in
# that docstring).  The driver of a negative fixture, `loop fixture`, writes the
# same record and holds the same rules itself.
#
# CMake puts CMAKE_TEST_LAUNCHER in front of a test whose command names an
# executable target.  It gives no launcher to a script test.  Each script test
# of the tree comes from crucible_script_test(), which puts the launcher in
# front of its command.  The rule test-time (`guard check --rule test-time`)
# refuses a test that the launcher and the fixture driver do not run.
#
# A target reads CMAKE_TEST_LAUNCHER when it is created, and a test reads the
# launcher when crucible_script_test() registers it.  This file must come
# before the first target and the first test.  It finds CRUCIBLE_PYTHON3, the
# cache entry that cmake/BuildLauncher.cmake also reads.  The paths of the
# masters are relative to this file.  A scratch project of a self-test can then
# include it.
#
# CRUCIBLE_CHECK_WARNINGS_DIR is where a compile-time check writes the
# warnings that do not fail it (utils/assay/core/report.py).  A check gets it
# as --warnings-dir.

set(CRUCIBLE_CHECK_WARNINGS_DIR "${CMAKE_BINARY_DIR}/check-warnings")
# An earlier version of this file wrote CTestCustom.cmake, with a command that read the log of each ctest run.  ctest
# runs that command while the file is in the build directory.  The configure removes the file.
file(REMOVE "${CMAKE_BINARY_DIR}/CTestCustom.cmake")

# crucible_script_test(NAME <name> COMMAND <program> [<arg>...] [CONFIGURATIONS <config>...]
#                      [WORKING_DIRECTORY <dir>] [COMMAND_EXPAND_LISTS])
#
# Register a script test, with the arguments of add_test(), and put the test
# launcher with the name of the test in front of its command.  With no python3,
# the test runs with no launcher.
function(crucible_script_test)
  cmake_parse_arguments(PARSE_ARGV 0 _script "COMMAND_EXPAND_LISTS" "NAME;WORKING_DIRECTORY"
                        "COMMAND;CONFIGURATIONS")
  if(NOT _script_NAME OR NOT _script_COMMAND OR _script_UNPARSED_ARGUMENTS OR _script_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "crucible_script_test() takes NAME <name> COMMAND <program> [<arg>...] and the other "
                        "keywords of add_test(), and it got: ${ARGV}")
  endif()
  set(_script_options "")
  if(DEFINED _script_WORKING_DIRECTORY)
    list(APPEND _script_options WORKING_DIRECTORY "${_script_WORKING_DIRECTORY}")
  endif()
  if(_script_CONFIGURATIONS)
    list(APPEND _script_options CONFIGURATIONS ${_script_CONFIGURATIONS})
  endif()
  if(_script_COMMAND_EXPAND_LISTS)
    list(APPEND _script_options COMMAND_EXPAND_LISTS)
  endif()
  set(_script_launcher "")
  if(CRUCIBLE_TEST_STEP)
    set(_script_launcher ${CRUCIBLE_TEST_STEP} --name "${_script_NAME}")
  endif()
  add_test(NAME "${_script_NAME}" COMMAND ${_script_launcher} ${_script_COMMAND} ${_script_options})
endfunction()

find_program(CRUCIBLE_PYTHON3 NAMES python3 DOC "The Python interpreter of the build launcher")
if(NOT CRUCIBLE_PYTHON3)
  message(WARNING "The test launcher `loop test-step` needs python3, and no python3 is on PATH, so no test writes a "
                  "cost record or holds its budget.")
  return()
endif()
cmake_path(SET _crucible_test_masters NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../utils/bin")
set(CRUCIBLE_TEST_STEP "${CRUCIBLE_PYTHON3}" -S "${_crucible_test_masters}/loop" test-step
    --warnings-dir "${CRUCIBLE_CHECK_WARNINGS_DIR}" --build-dir "${CMAKE_BINARY_DIR}")
set(CMAKE_TEST_LAUNCHER ${CRUCIBLE_TEST_STEP})
