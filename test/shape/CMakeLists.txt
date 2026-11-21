# The probe unit of the core families.
#
# core_shapes.cpp pairs each hot operation of include/foundation/core with
# the raw form that it replaces.  The rule codegen-shapes
# (utils/assay/guard/engines/elf/codegen_shapes.py) compiles the unit with the
# command of the object of this directory in the compile database, reads the
# disassembly, and compares the two functions of each pair.  The build
# compiles the object too, and the quarantine plugin, the warnings and the
# compile budgets then see the unit.
#
# The object takes the flags of the foundation library in each build: it
# links crucible_dialect, and the root CMakeLists.txt makes each library of
# the tree position-independent.
# Each preset then compiles the two functions of a pair with its own level,
# sanitizer, hardening flags and contract semantic, and the rule compares the
# pair with the flags of that build.  The rule replaces the target words of
# the build, such as -march=native, with each target of a fixed list:
# x86-64, x86-64-v3 and x86-64-v4.  Thus each host gives the same verdict.
# The rule refuses a probe object with code flags that are not the code flags
# of the units of the foundation library, other than the target words.
#
# The expectations are for x86_64.  On another architecture, the directory
# builds no object, and the rule does not apply.

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  return()
endif()

add_library(core_shape_probes OBJECT core_shapes.cpp)
target_include_directories(core_shape_probes PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(core_shape_probes PRIVATE crucible_dialect)

# The steady loop of a session send on a local handle.  The check compiles
# session_send.cpp with the command of core_shapes.cpp, and it refuses a call
# and each read of memory other than the slot of the thread in that loop
# (test/shape/check_session_send.py).  A build below -O2 gives a skip, so the
# check judges the Release builds.
crucible_script_test(NAME session_send_steady_loop
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/check_session_send.py ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})
set_tests_properties(session_send_steady_loop PROPERTIES SKIP_RETURN_CODE 77)

# The loops of one block of the refill and of the flush of the allocator.  The
# check compiles Refill.cpp and Flush.cpp with their own commands, and it
# refuses a setcc or more than ten instructions in the loop of one block of
# take_slots_, and a call or a read of memory other than the block of the run
# in the loop of one block of give_back_run_ (test/shape/check_alloc_loops.py).
# A build below -O2 gives a skip.
crucible_script_test(NAME alloc_block_loops
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/check_alloc_loops.py ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})
set_tests_properties(alloc_block_loops PROPERTIES SKIP_RETURN_CODE 77)
