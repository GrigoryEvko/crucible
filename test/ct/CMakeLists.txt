# Constant-time proofs: the ctgrind taint tests and the dudect timing
# harness.
#
# A taint test marks every secret operand as undefined to memcheck and
# runs the constant-time code under valgrind.  A conditional jump or an
# address that depends on a secret bit is then a memcheck error, and
# --error-exitcode turns it into a test failure.
#
# The tests build with release optimization, because the optimizer is
# what turns arithmetic into a branch.  They build for x86-64-v3, the
# widest ISA that valgrind decodes: valgrind 3.27 stops on AVX-512 with an
# illegal opcode.  The rule codegen-shapes covers the -march=native code
# instead.  The tests link no library from the tree: the code under test is
# header-only, and a sanitizer flag inherited from a library target would
# stop valgrind.  -fno-sanitize=all removes a sanitizer flag that a preset
# puts in CMAKE_CXX_FLAGS.
#
# crucible_release_code_target() of cmake/CodeFlags.cmake gives each test
# and the timing harness the code flags of a Release unit of the tree, also
# in a Debug build directory: the hardening flags of each build, the flags
# that each build other than Debug takes, the zero fill, FORTIFY and the FP
# floor.  The library takes the same lists.  So a test runs the code that a
# Release build runs, at the ISA level that valgrind decodes.  The test
# ct_taint_flags (cmake/CodeFlagsCheck.cmake) reads the flags of each
# compile from the CMake file API.  It fails when a target of this directory
# and the foundation library do not have the same code flags, apart from the
# flags that change with the kind of build.
#
# The observe semantic keeps the checks of CRUCIBLE_PRE, as in a Release
# library, and a check references the violation handler.  Because of this,
# each binary compiles the handler source of foundation with its own flags,
# and the formatter that the handler writes with.  These sources call only
# functions of the C library, so they need no archive of the toolchain.
set(CRUCIBLE_CT_HANDLER_SOURCES
  ${CMAKE_SOURCE_DIR}/src/foundation/ContractHandler.cpp
  ${CMAKE_SOURCE_DIR}/src/foundation/core/Report.cpp)

find_program(CRUCIBLE_VALGRIND valgrind)
find_path(CRUCIBLE_VALGRIND_INCLUDE valgrind/memcheck.h HINTS $ENV{HOME}/.local/include)
option(CRUCIBLE_REQUIRE_CT_TAINT
  "Fail the configure step when valgrind or its client-request headers are missing" OFF)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  message(STATUS "ct_taint: the taint tests target x86-64-v3, and this host is ${CMAKE_SYSTEM_PROCESSOR}")
  return()
endif()

# The rule codegen-shapes (utils/assay/guard/engines/elf/codegen_shapes.py)
# compiles each constant-time primitive with the flags of each kind of build:
# the Debug, Release and verify flags, at their levels, for three ISA levels.
# It refuses a conditional branch, a call, a division or a data-dependent load
# in the body of a primitive.  The compile of ct_dudect gives the rule the
# flags of a Release build in each build type.

# The dudect timing harness is evidence, not a test: a timing result
# depends on the host and its load.  Build it on request with
# `cmake --build <dir> --target ct_dudect` and run it on an isolated core.
# It builds with the Release optimization for this host.
add_executable(ct_dudect EXCLUDE_FROM_ALL dudect.cpp ${CRUCIBLE_CT_HANDLER_SOURCES})
target_include_directories(ct_dudect PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(ct_dudect PRIVATE
  -O3 -march=native -DNDEBUG -fno-sanitize=all
  -freflection -fcontracts -fcontract-evaluation-semantic=observe)
crucible_release_code_target(ct_dudect)
target_link_options(ct_dudect PRIVATE -fno-sanitize=all)

# Registers the check of the code flags of TARGETS and its self-test.  The
# check reads only the reply of the file API that the configure step
# writes, so it needs no build of the targets.
function(ct_code_flags_tests)
  list(JOIN ARGN "," targets)
  crucible_script_test(NAME ct_taint_flags
    COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGETS=${targets}
            -P ${CMAKE_SOURCE_DIR}/cmake/CodeFlagsCheck.cmake)
  crucible_script_test(NAME ct_taint_flags_self_test
    COMMAND ${CMAKE_COMMAND} -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/code_flags_self_test -DSELF_TEST=ON
            -P ${CMAKE_SOURCE_DIR}/cmake/CodeFlagsCheck.cmake)
  set_tests_properties(ct_taint_flags ct_taint_flags_self_test PROPERTIES LABELS "ci_guard;ct")
endfunction()

if(NOT CRUCIBLE_VALGRIND OR NOT CRUCIBLE_VALGRIND_INCLUDE)
  if(CRUCIBLE_REQUIRE_CT_TAINT)
    message(FATAL_ERROR
      "ct_taint: valgrind (${CRUCIBLE_VALGRIND}) or valgrind/memcheck.h (${CRUCIBLE_VALGRIND_INCLUDE}) "
      "is missing. Install valgrind and valgrind-devel, or set CRUCIBLE_REQUIRE_CT_TAINT=OFF.")
  endif()
  message(STATUS
    "ct_taint: valgrind or valgrind/memcheck.h is missing, so the constant-time taint tests are "
    "registered as disabled")
  crucible_script_test(NAME ct_taint_unavailable COMMAND ${CMAKE_COMMAND} -E false)
  set_tests_properties(ct_taint_unavailable PROPERTIES DISABLED TRUE LABELS "ct_taint")
  ct_code_flags_tests(ct_dudect)
  return()
endif()

set(CRUCIBLE_CT_TAINT_FLAGS
  -march=x86-64-v3 -DNDEBUG -g -fno-sanitize=all
  -freflection -fcontracts -fcontract-evaluation-semantic=observe)

# One taint test per source and optimization level.  The same binary also
# runs the two leaking controls, which pass only when memcheck reports the
# leak, so a blind harness fails.
#
# When DEBUGINFOD_URLS names a server, valgrind asks that server for the
# debug information of each loaded object that has none on the host.  A
# lookup goes over the network and can take seconds.  So each taint test
# runs with no DEBUGINFOD_URLS, and the harness refuses a run that has one.
function(ct_taint_test source opt)
  set(name ${source}_${opt})
  add_executable(${name} ${source}.cpp ${CRUCIBLE_CT_HANDLER_SOURCES})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include ${CRUCIBLE_VALGRIND_INCLUDE})
  target_compile_options(${name} PRIVATE -${opt} ${CRUCIBLE_CT_TAINT_FLAGS})
  crucible_release_code_target(${name})
  target_link_options(${name} PRIVATE -fno-sanitize=all)
  set(_valgrind ${CRUCIBLE_VALGRIND} --tool=memcheck -q)
  crucible_script_test(NAME ${name}
    COMMAND ${_valgrind} --error-exitcode=99 $<TARGET_FILE:${name}>)
  crucible_script_test(NAME ${name}_control_branch
    COMMAND ${_valgrind} $<TARGET_FILE:${name}> --control-branch)
  crucible_script_test(NAME ${name}_control_index
    COMMAND ${_valgrind} $<TARGET_FILE:${name}> --control-index)
  set_tests_properties(${name} ${name}_control_branch ${name}_control_index
    PROPERTIES LABELS "ct_taint" ENVIRONMENT_MODIFICATION "DEBUGINFOD_URLS=unset:")
endfunction()

ct_taint_test(test_ct_taint_fixy O2)
ct_taint_test(test_ct_taint_fixy O3)
ct_code_flags_tests(ct_dudect test_ct_taint_fixy_O2 test_ct_taint_fixy_O3)
