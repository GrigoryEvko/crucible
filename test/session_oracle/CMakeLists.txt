# Differential tests of the session relations against published
# mechanisations: the projection of Tirore, Bengtson and Carbone (ITP
# 2023), their subject-reduction development (ECOOP 2025), the subtyping
# relation of Ekici (ITP 2025), and mpstk-crash-stop (CONCUR 2022).
#
# tools/session_oracle/ writes golden.csv and the eight generated_*.cpp
# files.  Each generated file holds one static_assert for each measured
# row, so a relation that changes its answer fails the build of that
# test.  The wire test is the one exception: it runs each row and fails
# when an outcome differs.  The rows of the record families (the runs,
# the subject-reduction safety, mpstk and the subtyping certificates)
# have no assertion.
# Rebuild the files with scripts/session-oracle.sh --regenerate.  The
# guards session_oracle_check and session_oracle_self_test are in
# test/CMakeLists.txt beside the other script guards.

function(session_oracle_test name source link)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${link})
  set_source_files_properties(${source} PROPERTIES
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
session_oracle_test(test_session_oracle_fixy_duality generated_fixy_duality.cpp fixy)

# fixy/session/Global.h, Projection.h and Liveness.h: well-formedness,
# projection onto each role, and the liveness claim.
session_oracle_test(test_session_oracle_fixy_projection generated_fixy_projection.cpp fixy)

# fixy/session/Subtype.h: synchronous and bounded asynchronous subtyping.
session_oracle_test(test_session_oracle_fixy_subtype generated_fixy_subtype.cpp fixy)

# fixy/session/Subtype.h on keyed choices: branches that match by label
# in any order, and a keyed step against a keyed choice.
session_oracle_test(test_session_oracle_fixy_keyed_subtype generated_fixy_keyed_subtype.cpp fixy)

# fixy/session/Projection.h and Liveness.h on global types whose choices
# carry label keys in an order that depends on the path.
session_oracle_test(test_session_oracle_fixy_keyed_projection generated_fixy_keyed_projection.cpp fixy)

# fixy/session/Projection.h and Liveness.h under crash-stop failures:
# project_crash_t and crash_live_by_construction_v.
session_oracle_test(test_session_oracle_fixy_crash generated_fixy_crash.cpp fixy)

# fixy/session/Projection.h on runtime global types: the projection, the
# liveness claim and association after the first send.
session_oracle_test(test_session_oracle_fixy_enroute generated_fixy_enroute.cpp fixy)

# fixy/session/Handle.h on the wire: keyed pairs run end to end over one
# queue of words, each run in a child process (wire_driver.h).
session_oracle_test(test_session_oracle_fixy_wire generated_fixy_wire.cpp fixy)
target_link_options(test_session_oracle_fixy_wire PRIVATE -pthread)
