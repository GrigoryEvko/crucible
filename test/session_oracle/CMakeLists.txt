# Differential tests of the session relations against the projection
# oracle of Tirore, Bengtson and Carbone (ITP 2023).
#
# tools/session_oracle/ writes golden.csv and the four generated_*.cpp
# files.  Each generated file holds one static_assert for each measured
# row, so a relation that changes its answer fails the build of that
# test.  Rebuild the files with scripts/session-oracle.sh --regenerate.
# The guards session_oracle_check and session_oracle_self_test are in
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

# crucible/sessions/SessionGlobal.h (the frozen tree): projection and
# well-formedness.
session_oracle_test(test_session_oracle_old_projection generated_old_projection.cpp crucible)
