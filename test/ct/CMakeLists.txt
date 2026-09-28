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
# illegal opcode.  The disassembly guard covers the -march=native code
# instead.  The tests link no library from the tree: the code under test is
# header-only, and a sanitizer flag inherited from a library target would
# stop valgrind.  -fno-sanitize=all removes a sanitizer flag that a preset
# puts in CMAKE_CXX_FLAGS.
#
# The observe semantic keeps the checks of CRUCIBLE_PRE, as in a Release
# library, and a check references the violation handler.  Because of this,
# each binary compiles the handler source of foundation with its own flags.
# The handler calls only functions of the C library, so it needs no
# archive of the toolchain.

find_program(CRUCIBLE_VALGRIND valgrind)
find_path(CRUCIBLE_VALGRIND_INCLUDE valgrind/memcheck.h HINTS $ENV{HOME}/.local/include)
option(CRUCIBLE_REQUIRE_CT_TAINT
  "Fail the configure step when valgrind or its client-request headers are missing" OFF)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  message(STATUS "ct_taint: the taint tests target x86-64-v3, and this host is ${CMAKE_SYSTEM_PROCESSOR}")
  return()
endif()

# The disassembly guard compiles each constant-time primitive at -O2 and
# -O3 for three ISA levels, and refuses a conditional branch, a call, a
# division or a data-dependent load in its body.  Its self-test plants a
# branch, a table lookup, a call and a division, and a census escape.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(CRUCIBLE_OBJDUMP objdump REQUIRED)
add_test(NAME check_ct_disassembly
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check-ct-disassembly.py
          --cxx ${CMAKE_CXX_COMPILER})
add_test(NAME check_ct_disassembly_self_test
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check-ct-disassembly.py
          --cxx ${CMAKE_CXX_COMPILER} --self-test)
set_tests_properties(check_ct_disassembly check_ct_disassembly_self_test
  PROPERTIES LABELS "ci_guard;ct" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# The dudect timing harness is evidence, not a test: a timing result
# depends on the host and its load.  Build it on request with
# `cmake --build <dir> --target ct_dudect` and run it on an isolated core.
# It builds with the Release optimization for this host.
add_executable(ct_dudect EXCLUDE_FROM_ALL dudect.cpp ${CMAKE_SOURCE_DIR}/src/foundation/ContractHandler.cpp)
target_include_directories(ct_dudect PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(ct_dudect PRIVATE
  -O3 -march=native -DNDEBUG -fno-sanitize=all
  -freflection -fcontracts -fcontract-evaluation-semantic=observe)
target_link_options(ct_dudect PRIVATE -fno-sanitize=all)

if(NOT CRUCIBLE_VALGRIND OR NOT CRUCIBLE_VALGRIND_INCLUDE)
  if(CRUCIBLE_REQUIRE_CT_TAINT)
    message(FATAL_ERROR
      "ct_taint: valgrind (${CRUCIBLE_VALGRIND}) or valgrind/memcheck.h (${CRUCIBLE_VALGRIND_INCLUDE}) "
      "is missing. Install valgrind and valgrind-devel, or set CRUCIBLE_REQUIRE_CT_TAINT=OFF.")
  endif()
  message(STATUS
    "ct_taint: valgrind or valgrind/memcheck.h is missing, so the constant-time taint tests are "
    "registered as disabled")
  add_test(NAME ct_taint_unavailable COMMAND ${CMAKE_COMMAND} -E false)
  set_tests_properties(ct_taint_unavailable PROPERTIES DISABLED TRUE LABELS "ct_taint")
  return()
endif()

set(CRUCIBLE_CT_TAINT_FLAGS
  -march=x86-64-v3 -DNDEBUG -g -fno-sanitize=all
  -freflection -fcontracts -fcontract-evaluation-semantic=observe)

# One taint test per source and optimization level.  The same binary also
# runs the two leaking controls, which pass only when memcheck reports the
# leak, so a blind harness fails.
function(ct_taint_test source opt)
  set(name ${source}_${opt})
  add_executable(${name} ${source}.cpp ${CMAKE_SOURCE_DIR}/src/foundation/ContractHandler.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include ${CRUCIBLE_VALGRIND_INCLUDE})
  target_compile_options(${name} PRIVATE -${opt} ${CRUCIBLE_CT_TAINT_FLAGS})
  target_link_options(${name} PRIVATE -fno-sanitize=all)
  set(_valgrind ${CRUCIBLE_VALGRIND} --tool=memcheck -q)
  add_test(NAME ${name}
    COMMAND ${_valgrind} --error-exitcode=99 $<TARGET_FILE:${name}>)
  add_test(NAME ${name}_control_branch
    COMMAND ${_valgrind} $<TARGET_FILE:${name}> --control-branch)
  add_test(NAME ${name}_control_index
    COMMAND ${_valgrind} $<TARGET_FILE:${name}> --control-index)
  set_tests_properties(${name} ${name}_control_branch ${name}_control_index
    PROPERTIES LABELS "ct_taint")
endfunction()

ct_taint_test(test_ct_taint_fixy O2)
ct_taint_test(test_ct_taint_fixy O3)
