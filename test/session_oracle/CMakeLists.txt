# Differential tests of the session relations against published
# mechanisations: the projection of Tirore, Bengtson and Carbone (ITP
# 2023), their subject-reduction development (ECOOP 2025), the subtyping
# relation of Ekici (ITP 2025), and mpstk-crash-stop (CONCUR 2022).
#
# utils/tools/session_oracle/ writes golden.csv, the generated_*.cpp
# files and generated_sources.cmake.  Each test is one executable whose
# rows go into several generated files (shards), so that no translation
# unit holds all the rows of a test.  generated_sources.cmake lists the
# shards of each test.  Each generated file holds one static_assert for
# each measured row, so a relation that changes its answer fails the
# build of that test.  The wire test is the one exception: it runs each
# row and fails when an outcome differs.  The rows of the record families
# (the runs, the subject-reduction safety, mpstk and the subtyping
# certificates) have no assertion.
# Rebuild the files with utils/scripts/session-oracle.sh --regenerate.  The
# guard session_oracle_check and the parts session_oracle_self_test_K of
# the self-test are in test/CMakeLists.txt beside the other script guards.

include(${CMAKE_CURRENT_SOURCE_DIR}/generated_sources.cmake)

function(session_oracle_test name link)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE ${link})
  set_source_files_properties(${ARGN} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-Wno-alloc-size-larger-than>")
  target_compile_options(${name} PRIVATE -UNDEBUG)
  if(CRUCIBLE_SAN_COMPILE_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_SAN_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${CRUCIBLE_SAN_LINK_FLAGS})
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "session_oracle")
endfunction()

# fixy/session/Protocol.h: duality, involution, well-formedness and the
# acceptance of the naive reading of each global type.
session_oracle_test(test_session_oracle_fixy_duality fixy ${SESSION_ORACLE_FIXY_DUALITY_SOURCES})

# fixy/session/Global.h, Projection.h and Liveness.h: well-formedness,
# projection onto each role, and the liveness claim.
session_oracle_test(test_session_oracle_fixy_projection fixy ${SESSION_ORACLE_FIXY_PROJECTION_SOURCES})

# fixy/session/Subtype.h: synchronous and bounded asynchronous subtyping.
session_oracle_test(test_session_oracle_fixy_subtype fixy ${SESSION_ORACLE_FIXY_SUBTYPE_SOURCES})

# fixy/session/Subtype.h on keyed choices: branches that match by label
# in any order, and a keyed step against a keyed choice.
session_oracle_test(test_session_oracle_fixy_keyed_subtype fixy ${SESSION_ORACLE_FIXY_KEYED_SUBTYPE_SOURCES})

# fixy/session/Projection.h and Liveness.h on global types whose choices
# carry label keys in an order that depends on the path.
session_oracle_test(test_session_oracle_fixy_keyed_projection fixy ${SESSION_ORACLE_FIXY_KEYED_PROJECTION_SOURCES})

# fixy/session/Projection.h and Liveness.h under crash-stop failures:
# project_crash_t and crash_live_by_construction_v.
session_oracle_test(test_session_oracle_fixy_crash fixy ${SESSION_ORACLE_FIXY_CRASH_SOURCES})

# fixy/session/Projection.h on runtime global types: the projection, the
# liveness claim and association after the first send.
session_oracle_test(test_session_oracle_fixy_enroute fixy ${SESSION_ORACLE_FIXY_ENROUTE_SOURCES})

# fixy/session/Handle.h on the wire: keyed pairs run end to end over one
# queue of words, each run in a child process (wire_driver.h).
session_oracle_test(test_session_oracle_fixy_wire fixy ${SESSION_ORACLE_FIXY_WIRE_SOURCES})
target_link_options(test_session_oracle_fixy_wire PRIVATE -pthread)
