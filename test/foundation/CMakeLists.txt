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
foundation_test(test_saturate_foundation test_saturate.cpp)
foundation_test(test_signature)
foundation_test(test_algebra_core)
foundation_test(test_lattices_core)
foundation_test(test_lattices_bands)
foundation_test(test_effects_core)
foundation_test(test_ctx)
foundation_test(test_capability)
foundation_test(test_computation)
foundation_test(test_diag)
foundation_test(test_pinned)
foundation_test(test_brand)

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
  # CMake counts brackets when it splits a list, so a regex carrying an
  # unbalanced `[` swallows the separator and the regex written after it.
  # The merged regex then matches nothing and the fixture fails for a
  # reason that names no defect.  ARGC counts what the caller wrote, and
  # the list length counts what survived, so the two disagree exactly
  # when a merge happened.
  math(EXPR _written "${ARGC} - 1")
  if(NOT _count EQUAL _written)
    message(FATAL_ERROR
      "foundation_neg_compile_test(${name}): ${_written} regexes were written "
      "and ${_count} survived. An unbalanced `[` in one of them swallowed the "
      "list separator and merged it with the next. Balance the bracket, or "
      "match it with `.`")
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

# The passkey constructor is the only one Permission declares, so a
# translation unit that cannot make a key cannot make a token.  The
# second regex proves the rejection is on Permission's own constructor
# and not on the fixture's tag type.
foundation_neg_compile_test(neg_permission_forged_without_key
  "no matching function for call to"
  "Permission<.*ForgedTag>::Permission")

# The post-join rebuild key is the whole gate on reissuing a parent, and
# ForkRebuildAccess::rebuild<T> is public and unconstrained, so the key's
# private constructor is what stands between a caller and a Permission
# for an arbitrary tag.  The second regex proves the refusal names that
# key and not some other private member.
foundation_neg_compile_test(neg_permission_fork_rebuild_key_unreachable
  "is private within this context"
  "ForkRebuildKey")

# The companion: the nullary door that made the parent forgeable from any
# TU must stay deleted.  Its predecessor fixture asserted a name that had
# already been renamed, so it proved a rename rather than a closure; this
# one names the symbol that actually shipped.
foundation_neg_compile_test(neg_permission_fork_rebuild_no_nullary_door
  "is not a member of"
  "rebuild_parent_after_fork_")

# Route one: a context is not evidence of a capability, so naming a
# specialization and building it from nothing is refused.  The
# second regex proves the refusal is on ExecCtx's own constructor and not
# on the fixture's row type.
foundation_neg_compile_test(neg_exec_ctx_forged_specialization
  "no matching function for call to"
  "ExecCtx<.*Init.*>::ExecCtx\\(\\)")

# Route two: promotion takes the capability, not its name.
# Closing one route and not the other leaves the gate open, so these two
# fixtures are a pair.
foundation_neg_compile_test(neg_exec_ctx_with_cap_needs_the_capability
  "no matching function for call to"
  "with_cap<")

# The one door of the contexts, mint_context, has one constraint, and
# these two fixtures stand on its two atoms.  The key is a reference the
# fixture never builds, so the refusal is the constraint's and not the
# passkey's.  The wrong key: a background key cannot mint an init
# context, because the binding is the context's own key_type.  The
# lookalike: deriving from the family base with a real key is not
# membership, because IsContext reads the roster and nothing else.
foundation_neg_compile_test(neg_mint_context_wrong_key
  "no matching function for call to .mint_context<foundation::effects::Init>\\(foundation::effects::detail::ctx_mint::bg_key\\)."
  "required for the satisfaction of .CanMintContext<Ctx, Key>. \\[with Ctx = foundation::effects::Init; Key = foundation::effects::detail::ctx_mint::bg_key\\]")
foundation_neg_compile_test(neg_mint_context_lookalike_context
  "no matching function for call to .mint_context<\\{anonymous\\}::Lookalike>\\(foundation::effects::detail::ctx_mint::bg_key\\)."
  "required for the satisfaction of .IsContext<Ctx>. \\[with Ctx = <unnamed>::Lookalike\\]")

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

# extract must not hand out an authority the carrier's row never names.
# Two fixtures, because the relation enumerates the authority kinds and
# each kind reaches it through its own specialization: a repair that
# covered capabilities alone would satisfy the first and leave the
# second open.  The second regex in each names the payload type the
# relation refused, so a fixture that rejected on the wrong type would
# be red.  The admitting direction is pinned in Computation.h, so a
# blanket refusal cannot satisfy these.
foundation_neg_compile_test(neg_computation_extract_capability_payload
  "constraints not satisfied"
  "extract_admits_payload_v<T> .with T = foundation::effects::Capability")

foundation_neg_compile_test(neg_computation_extract_permission_payload
  "constraints not satisfied"
  "extract_admits_payload_v<T> .with T = foundation::permissions::Permission")

# Two more, for a carrier that no list names.  The first holds a
# capability in a member of a plain class, which the walk over
# components reads.  The second is a lambda with a capture, which the
# walk cannot read and so refuses.
foundation_neg_compile_test(neg_computation_extract_capability_in_member
  "constraints not satisfied"
  "extract_admits_payload_v<T> .with T = <unnamed>::HoldsCapability")

foundation_neg_compile_test(neg_computation_extract_capturing_lambda
  "constraints not satisfied"
  "Computation<foundation::effects::Row<>, main\\(\\)::<lambda\\(\\)> >::extract\\(\\)")

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

# ── The lift from a marker to its effect row ─────────────────────────
foundation_test(test_lift)

# ── The folded permission mints ──────────────────────────────────────
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
# ── The signature reflection ──────────────────────────────────────────
#
# param_type_t names one parameter by index, bounded by the function's
# own arity.  The two fixtures stand on the two ends of that bound: an
# index past the parameters of a function that has some, and any index at
# all on a function that has none.  An off-by-one in the bound would
# admit the second while still rejecting the first, so neither fixture
# covers the other.  Each is matched on the constraint as the compiler
# spells it, which the fixture source never contains.
foundation_neg_compile_test(neg_signature_param_index_overflow
  "template constraint failure"
  "the expression .I < foundation::reflect::signature_traits<FnPtr>::arity \\[with I = 2; FnPtr = &witness_binary\\(int, double\\)\\]. evaluated to .false.")
foundation_neg_compile_test(neg_signature_param_index_on_nullary
  "template constraint failure"
  "the expression .I < foundation::reflect::signature_traits<FnPtr>::arity \\[with I = 0; FnPtr = &witness_nullary\\(\\)\\]. evaluated to .false.")

foundation_neg_compile_test(neg_permission_root_ctx_refuses_row
  "constraints not satisfied"
  "satisfaction of .PermissionRootArgs<Tag, Args \\.\\.\\.>. \\[with Tag = .*NeedsIo.*ctx_cap::Fg")

# ── The borrow's row gate ────────────────────────────────────────────
#
# A view is a proof about a region, so minting one is bounded by who may
# hold that region.  A factory that reads no context can be sound only
# for the empty row, which is the rule SharedPermissionPool::lend states
# for the pooled share of the same region and this factory stated
# nowhere.  The first fixture is a declared row that names IO; the
# second is a region that declares no row, and asks which way the gate
# fails when it has nothing to read.  The third is the scoped form,
# which is the second door onto the same borrow.
#
# Each second regex names the concept and the tag it was checked for,
# which no fixture source spells.  Removing the clause compiles all
# three.
foundation_neg_compile_test(neg_read_view_effectful_row_without_ctx
  "no matching function for call to .mint_read_view\\("
  "required for the satisfaction of .ReadViewNeedsNoCtx<Tag>. .with Tag = .*MappedRegion")
foundation_neg_compile_test(neg_read_view_undeclared_row_without_ctx
  "no matching function for call to .mint_read_view\\("
  "required for the satisfaction of .ReadViewNeedsNoCtx<Tag>. .with Tag = .*UndecidedRegion")
foundation_neg_compile_test(neg_read_view_scoped_form_keeps_the_row_gate
  "no matching function for call to .with_read_view\\("
  "required for the satisfaction of .ReadViewNeedsNoCtx<Tag>. .with Tag = .*MappedRegion")

# ── Brands: identity per mint ────────────────────────────────────────
#
# A root mint gives every call site a fresh brand, and every derived
# token carries it.  Each fixture below names one thing the brand
# refuses, and its second regex is the compiler's own rendering of the
# refusal, which the fixture source never spells: the two closure types
# a callee's one Brand parameter was deduced to, the constraint on a
# constructor that answered false, or a sentence from a header's
# static_assert.  Every fixture was proven live by a mutant that
# removes its mechanism and lets it compile.

# Two tokens of one tag from two roots are two brands.
foundation_neg_compile_test(neg_brand_permission_cross_brand
  "no matching function for call to .same_region\\("
  "deduced conflicting types for parameter .Brand. \\(.foundation::permissions::<lambda\\(\\)>. and .foundation::permissions::<lambda\\(\\)>.\\)")

# A share carries the brand of its exclusive.
foundation_neg_compile_test(neg_brand_shared_permission_cross_brand
  "no matching function for call to .same_region\\("
  "deduced conflicting types for parameter .Brand. \\(.foundation::permissions::<lambda\\(\\)>. and .foundation::permissions::<lambda\\(\\)>.\\)")

# A borrow proof is about the region it was borrowed from: the refusal
# by identity.
foundation_neg_compile_test(neg_brand_read_view_proves_another_region
  "no matching function for call to .read_under\\("
  "deduced conflicting types for parameter .Brand. \\(.foundation::permissions::<lambda\\(\\)>. and .foundation::permissions::<lambda\\(\\)>.\\)")

# A borrow proof of a temporary permission: the refusal by value
# category, which is the deleted rvalue twin.
foundation_neg_compile_test(neg_brand_read_view_from_temporary_permission
  "use of deleted function"
  "mint_read_view\\(const Permission<Tag, Brand>&&\\)"
  "a borrow proof minted from a temporary permission outlives what it proves")

# A read proof comes from a Permission or a live share, and from nothing
# else.  A program that defines the old host type reaches a private
# constructor, because no friend names that type.
foundation_neg_compile_test(neg_read_view_host_door_closed
  "is private within this context"
  "ReadView<Tag, Brand>::ReadView\\(\\) .with Tag = .*Region")

# A read proof of a share guard that dies at the end of the statement.
foundation_neg_compile_test(neg_read_view_from_temporary_share_guard
  "use of deleted function"
  "mint_read_view\\(const SharedPermissionGuard<Tag, Brand>&&\\)"
  "a borrow proof minted from a temporary share guard outlives the share")

# The children of two regions do not fold into one parent.
foundation_neg_compile_test(neg_brand_combine_children_of_two_regions
  "static assertion failed"
  "the two children carry different brands, so they were split from two different regions of one tag"
  "perm_brands_agree_<foundation::permissions::Permission<\\{anonymous\\}::Left")

# The fresh brand sits after the argument pack, so a spelled brand is
# read as a context and refused by the root's fit concept.
foundation_neg_compile_test(neg_brand_root_mint_refuses_spelled_brand
  "no matching function for call to .mint_permission_root<\\{anonymous\\}::Region, \\{anonymous\\}::HeldBrand>\\(\\)."
  "constraints not satisfied"
  "PermissionRootArgs<Tag, Args \\.\\.\\.>")

# Erasure runs one way.
foundation_neg_compile_test(neg_brand_erased_token_does_not_rebrand
  "no matching function for call to .foundation::permissions::Permission<\\{anonymous\\}::Region, \\{anonymous\\}::HeldBrand>::Permission\\("
  "requires \\(is_same_v<Brand, foundation::brand::DefaultBrand>\\) && \\(IsFreshBrand<Other>\\)")

# ── The row relation is closed ───────────────────────────────────────

# A tag with no declared row has none, and the root mint says so.
foundation_neg_compile_test(neg_permission_row_undeclared_tag
  "static assertion failed: mint_permission_root<Tag>: no effect row is declared for Tag"
  "has_permission_row_v<\\{anonymous\\}::Undeclared>. evaluates to false")

# One row has one source.
foundation_neg_compile_test(neg_permission_row_declared_twice
  "static assertion failed: permission_row: a tag declares its row twice"
  "\\(\\(!\\(bool\\)by_edge\\) \\|\\| \\(!\\(bool\\)by_member\\)\\). evaluates to false")

# ── The row-mismatch message builder ──────────────────────────────────
#
# The static_assert message a mint hands the compiler when a row is not
# admitted: the site, the caller's row, the row the callee requires and
# the offending atoms, built at compile time.  The test keeps the file
# name of the one it is ported from minus its _compile suffix, and the
# runtime smoke the old header carried inline is its first case.
foundation_test(test_row_mismatch)

# A type that is not a diagnostic tag in the tag slot reaches the routed
# static_assert, whose wording is the header's.  The second regex is the
# instantiation as the compiler spells it, with the boolean the primary
# template dispatched on, which the fixture source does not contain.
foundation_neg_compile_test(neg_row_mismatch_tag_not_a_tag
  "static assertion failed: foundation::diag \\[RowMismatchTag_NonTag\\]"
  "row_message_check<int, some_function, int, float, double, false>"
  "is_diagnostic_class_v<int>. evaluates to false")

# ── The row hash, as one fold ─────────────────────────────────────────
#
# The half of the cache key that separates one effect regime from
# another.  The old header carried a partial specialisation per wrapper,
# fifty-two of them, each with a hand-allocated salt byte; here one
# specialisation over Graded covers every wrapper, because a wrapper in
# this tree is a spelling over Graded.
#
# Almost everything the header owes a consumer is a constant, so the
# test is mostly static assertions and exists to make them run: an
# assertion inside a header is checked only where some translation unit
# includes it, and nothing else in the new tree includes this one yet.
# Two cases are not constants, and they are the ones a header cannot
# check on its own: that the folded values reach a running program.
#
# The wire-format literals in that file are the published on-the-wire
# contract and are reproduced from the old header unchanged, which is
# the proof that the fold did not move a single row already in a peer's
# cache.
foundation_test(test_row_hash)
