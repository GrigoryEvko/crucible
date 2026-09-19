# Tests for the foundation layer.
#
# A foundation test links `foundation` and nothing above it.  The sanitizer
# flags come from the enclosing test/CMakeLists.txt, which adds this
# directory after it computes them.

function(foundation_test name)
  if(ARGN)
    set(_sources ${ARGN})
  else()
    set(_sources ${name}.cpp)
  endif()
  add_executable(${name} ${_sources})
  target_link_libraries(${name} PRIVATE foundation)
  set_source_files_properties(${_sources} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-Wno-alloc-size-larger-than>")
  # Assertions stay armed in every configuration; see crucible_test.
  target_compile_options(${name} PRIVATE -UNDEBUG)
  if(CRUCIBLE_SAN_COMPILE_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_SAN_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${CRUCIBLE_SAN_LINK_FLAGS})
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "foundation")
  if(CRUCIBLE_TSAN)
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT
        "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/test/tsan-suppressions.txt:halt_on_error=1")
  endif()
endfunction()

foundation_test(test_platform)
foundation_test(test_algebra_core)
foundation_test(test_lattices_core)
foundation_test(test_lattices_bands)
foundation_test(test_effects_core)
foundation_test(test_ctx)
foundation_test(test_capability)
foundation_test(test_computation)
foundation_test(test_diag)
foundation_test(test_pinned)

# Negative-compile fixtures for the foundation layer.
#
# A fixture is a translation unit that must fail to compile.  The test
# passes only when every regex given here appears in what the compiler
# itself said (test/neg_compile_driver.py strips the caret display, so a
# fixture's own echoed source cannot satisfy a regex).
#
# Two regexes are the minimum, by construction: the boilerplate reason
# and a project-specific token.  A gate made of one bare identifier is
# satisfied by "'X' was not declared in this scope", which proves that
# the fixture failed, not that it failed for the documented reason.
#
# CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1 is set on every fixture, so a file
# that rejects at two distinct lines is red: a fixture documents one
# rejection, and a second one means the regexes may be witnessing the
# wrong thing.
function(foundation_neg_compile_test name)
  set(_diags ${ARGN})
  list(LENGTH _diags _count)
  if(_count LESS 2)
    message(FATAL_ERROR
      "foundation_neg_compile_test(${name}): two required-diagnostic "
      "regexes are the minimum (the reason and the project token); got ${_count}")
  endif()
  add_executable(${name} EXCLUDE_FROM_ALL neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE foundation)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/neg/${name}.cpp
            ${name}
            ${_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "foundation_neg"
    ENVIRONMENT "CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1")
endfunction()

# The fixture that proves the harness fires: a concept the argument does
# not satisfy, gated on the reason and on the concept's name as the
# compiler spells it in its satisfaction note.
foundation_neg_compile_test(neg_constraint_failure_names_the_concept
  "constraints not satisfied"
  "satisfaction of .HalvesEvenly<T>.")

# A stored grade outside the lattice's order is rejected by the primary
# Graded constructor's contract_assert.  The location regex proves the
# rejection came from inside Graded.h and not from the fixture's own line.
foundation_neg_compile_test(neg_graded_stored_grade_outside_the_order
  "contract predicate is false in constant expression"
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: error")

# Weakening down the lattice is rejected during constant evaluation by
# the in-body CRUCIBLE_PRE.  The first regex is the trap the shim uses to
# poison the evaluation, and the second proves the trap came from the
# guard in weaken rather than from anywhere else.
foundation_neg_compile_test(neg_graded_weaken_below_current_grade
  "is not a constant expression"
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro")

# A capability source authorizes only its permitted row.
foundation_neg_compile_test(neg_mint_cap_outside_permitted_row
  "constraints not satisfied"
  "CanMintCap")

# A thread atom has no value-level tag, so the one extract_bare template
# leaves the overload set by substitution failure on HasBareTag.  The
# first regex proves the call found no candidate, and the second proves
# the reason was the constraint rather than a hard error in the body.
foundation_neg_compile_test(neg_extract_bare_thread_atom
  "no matching function for call to .extract_bare"
  "satisfaction of .HasBareTag<E>.")

# The witnessed lift reads the context's row, not the caller's word.
foundation_neg_compile_test(neg_lift_in_ctx_without_the_effect
  "constraints not satisfied"
  "row_contains_v")

# The contracts tests keep the file names of the tests they are ported
# from.  The target names carry the contracts prefix because the old
# tests still register the plain names in test/CMakeLists.txt, and a
# CMake target name is global.
foundation_test(test_contracts_decide test_decide.cpp)
foundation_test(test_contracts_decide_fuzz test_decide_fuzz.cpp)
foundation_test(test_contracts_pre_post test_pre_post.cpp)

# CRUCIBLE_PRE under NDEBUG must compile to the same machine code as a
# bare [[assume]].  This target is built with -O3 and NDEBUG defined
# instead of the preset's debug flags, and the sanitizer flags are left
# off because they change code generation.  Each function goes in its
# own section so the __start_ and __stop_ symbols bound one probe each.
# See test_pre_post_cost.cpp.
add_executable(test_contracts_pre_post_cost test_pre_post_cost.cpp)
target_link_libraries(test_contracts_pre_post_cost PRIVATE foundation)
target_compile_options(test_contracts_pre_post_cost PRIVATE -O3 -ffunction-sections)
target_compile_definitions(test_contracts_pre_post_cost PRIVATE NDEBUG)
add_test(NAME test_contracts_pre_post_cost COMMAND test_contracts_pre_post_cost)
set_tests_properties(test_contracts_pre_post_cost PROPERTIES LABELS "foundation")

# CRUCIBLE_PRE on a struct by const reference, violated in a constant
# expression: the shape a native pre() clause skips.  The location regex
# proves the trap fired inside Pre.h, and the note regex that it was
# reached through the macro.
foundation_neg_compile_test(neg_pre_violated_at_consteval
  "non-constant condition for static assertion"
  "foundation/contracts/Pre\\.h:[0-9]+:[0-9]+: error: .__builtin_trap\\(\\). is not a constant expression"
  "in expansion of macro .CRUCIBLE_PRE.")

# CRUCIBLE_POST on a field of a returned struct, violated in a constant
# expression.  The two note regexes prove the trap was reached through
# CRUCIBLE_POST in Post.h and not through a CRUCIBLE_PRE of the fixture.
foundation_neg_compile_test(neg_post_violated_at_consteval
  "non-constant condition for static assertion"
  "foundation/contracts/Pre\\.h:[0-9]+:[0-9]+: error: .__builtin_trap\\(\\). is not a constant expression"
  "foundation/contracts/Post\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "in expansion of macro .CRUCIBLE_POST.")

# The reflect test keeps the file name of the crucible test it stands
# beside.  The target carries a suffix because test/CMakeLists.txt still
# registers test_reflect for test/test_reflect.cpp, and a CMake target
# name is global.
foundation_test(test_reflect_foundation test_reflect.cpp)

# The namespace-member relation of foundation/diag/FailClosed.h.
foundation_test(test_fail_closed)

# The only Raw -> Checked edge is declared outside the relation's
# namespace, so the pair is not admitted.  The satisfaction note names
# the concept and the namespace the check was given.
foundation_neg_compile_test(neg_fail_closed_edge_outside_the_namespace
  "constraints not satisfied"
  "satisfaction of .Admitted<\\^\\^.*ingest, From, To>. \\[with From = .*Raw; To = .*Checked\\]")

# A struct named edge declared inside the relation's namespace is not
# foundation::fail_closed::edge, so a variable of that type is skipped.
# The evaluation note shows admits<> answering false for the pair.
foundation_neg_compile_test(neg_fail_closed_foreign_edge_type
  "constraints not satisfied"
  "admits<Ns, From, To>.*\\[with Ns = \\^\\^ namespace .*ingest.*From = .*Raw; To = .*Checked\\].*evaluated to .false.")

# The permission tests keep the file names of the tests they are ported
# from and carry a suffix, because test/CMakeLists.txt still registers
# the plain names and a CMake target name is global.
foundation_test(test_permissions_compile_foundation test_permissions_compile.cpp)
foundation_test(test_permission_shared_foundation test_permission_shared.cpp)
foundation_test(test_permission_shared_xor_exclusive_foundation test_permission_shared_xor_exclusive.cpp)
foundation_test(test_read_view_foundation test_read_view.cpp)

# A token split into subregions that no splits_into manifest declares.
# The location regex proves the rejection came from the split's own
# static_assert in Permission.h.
foundation_neg_compile_test(neg_permission_split_undeclared_subregion
  "static assertion failed"
  "foundation/permissions/Permission\\.h:[0-9]+:[0-9]+: error: static assertion failed: mint_permission_split<L, R>\\(Permission<In>&&\\) requires splits_into<In, L, R>::value")

# A fork whose child pack is not a declared partition of the parent.
# The satisfaction notes name the fork's fit concept and the pack trait
# that evaluated false.
foundation_neg_compile_test(neg_permission_fork_pack_does_not_partition
  "constraints not satisfied"
  "satisfaction of .CtxFitsPermissionFork<Ctx, Parent, Children \\.\\.\\.>."
  "splits_into_pack_v<Parent, Children \\.\\.\\.>.*evaluated to .false.")

# The spawning arm in a context whose row has no Bg effect.  The
# partition is declared, so the only atom that fails is the capability.
foundation_neg_compile_test(neg_permission_fork_without_bg
  "constraints not satisfied"
  "satisfaction of .CtxFitsPermissionFork<Ctx, Parent, Children \\.\\.\\.>."
  "satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Bg>.")

# ── Task #100 (A9): the lift from a marker to its effect row ─────────
foundation_test(test_lift)

# ── Task #104 (A10.4) part 2: the folded permission mints ────────────
#
# Each permission mint is one template gated by the shape of its
# argument list.  A token passed as an lvalue is not consumed, so it is
# not a split argument; the notes name the split's fit concept and the
# shape check that answered false.
foundation_neg_compile_test(neg_permission_split_lvalue_token
  "constraints not satisfied"
  "satisfaction of .PermissionSplitArgs<L, R, Args \\.\\.\\.>."
  "mint_args_shaped_<MaxCtx, NPerms, Args \\.\\.\\.>.*evaluated to .false.")

# The ctx-bound root mint in a context whose row does not cover the
# tag's row: the one root template's fit concept names the tag and the
# context it refused.
foundation_neg_compile_test(neg_permission_root_ctx_refuses_row
  "constraints not satisfied"
  "satisfaction of .PermissionRootArgs<Tag, Args \\.\\.\\.>. \\[with Tag = .*NeedsIo.*ctx_cap::Fg")
