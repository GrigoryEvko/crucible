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
foundation_test(test_lattices_counters)
foundation_test(test_counter_clock_attacks)
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

# Two counter axes are one lattice under two tags.  The element types
# have no conversion, so comparing an epoch with a generation has no
# candidate.  The second regex names the two operand types.
foundation_neg_compile_test(neg_strong_counter_axes_do_not_mix
  "no match for .operator==."
  "operand types are .const foundation::algebra::lattices::Epoch. .aka .const foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::element_type")

# A clock slot at the largest value cannot advance, because the
# increment would wrap.  The CRUCIBLE_PRE guard in successor_at poisons
# the constant evaluation, and the location regex proves the trap came
# from that guard.
foundation_neg_compile_test(neg_happens_before_successor_overflows
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .foundation::algebra::lattices::HappensBeforeLattice<2>::successor_at\\("
  "foundation/algebra/lattices/HappensBefore\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# The attacks on the counters and the clock that the compiler refuses.
# test_counter_clock_attacks holds the ones that compile.
#
# The largest count has no successor, because the step would wrap.
foundation_neg_compile_test(neg_strong_counter_successor_overflows
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::successor\\("
  "foundation/algebra/lattices/StrongCounterLattice\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
# A counter that exists cannot be edited in place.
foundation_neg_compile_test(neg_strong_counter_count_is_private
  "is private within this context"
  "StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::element_type::value_.")
# No integer states a count, no bytes of another axis rebuild one, the
# checked lifetime start refuses one, and the checked image read needs a
# context that owns IO.  Two versions have no sum.
foundation_neg_compile_test(neg_strong_counter_from_integer
  "element_type::element_type\\(uint64_t\\) .with Tag = foundation::algebra::lattices::counter_tags::epoch.* is private within this context"
  "StrongCounterLattice<Tag>::element_type::element_type")
foundation_neg_compile_test(neg_strong_counter_bit_cast_retag
  "no matching function for call to .bit_cast<foundation::algebra::lattices::Generation>"
  "the expression .is_trivially_copyable_v<_To> .with _To = foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::generation>::element_type.. evaluated to .false.")
foundation_neg_compile_test(neg_strong_counter_start_as_array
  "no matching function for call to .start_as_array<foundation::algebra::lattices::Epoch>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::element_type.")
foundation_neg_compile_test(neg_strong_counter_image_without_io
  "no matching function for call to .foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::mint_from_image"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::IO>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
foundation_neg_compile_test(neg_strong_counter_image_in_test_scope_without_io
  "no matching function for call to .foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::generation>::mint_from_image"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::IO>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test> >.")
foundation_neg_compile_test(neg_strong_counter_sum_on_version
  "no matching function for call to .foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::saturating_sum"
  "the expression .\\(Tag::claim_orientation\\) == foundation::algebra::ClaimOrientation::weaker_is_higher .with Tag = foundation::algebra::lattices::counter_tags::epoch.. evaluated to .false.")
# A generation cannot be built from an epoch.
foundation_neg_compile_test(neg_strong_counter_retag_by_construction
  "no matching function for call to .foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::generation>::element_type::element_type\\(<brace-enclosed initializer list>\\)."
  "no known conversion for argument 1 from .const foundation::algebra::lattices::Epoch. .aka .const foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>::element_type.. to .uint64_t.")
# The join of one axis takes that axis only.
foundation_neg_compile_test(neg_strong_counter_join_across_axes
  "cannot convert .element_type<foundation::algebra::lattices::counter_tags::generation>. to .element_type<foundation::algebra::lattices::counter_tags::epoch>."
  "initializing argument 2 of .static constexpr foundation::algebra::lattices::StrongCounterLattice<Tag>::element_type foundation::algebra::lattices::StrongCounterLattice<Tag>::join\\(")
# Graded reads up as the weaker claim.  A version counter in its numeric
# order, a product with one such component, and the dual of a use counter
# each read up as the stronger claim, and Graded refuses each of them.
foundation_neg_compile_test(neg_graded_version_counter_numeric_order
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +GradableLattice<L> class foundation::algebra::Graded."
  "satisfaction of .GradableLattice<L>. .with L = foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>."
  "\\(claim_orientation_v<L>\\) != foundation::algebra::ClaimOrientation::stronger_is_higher")
foundation_neg_compile_test(neg_graded_version_counter_in_product
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +GradableLattice<L> class foundation::algebra::Graded."
  "satisfaction of .GradableLattice<L>. .with L = foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch> >, foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::generation> >.")
foundation_neg_compile_test(neg_graded_use_counter_dual
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +GradableLattice<L> class foundation::algebra::Graded."
  "satisfaction of .GradableLattice<L>. .with L = foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::peak_bytes> >.")
# A counter tag that does not state its claim orientation is refused.
foundation_neg_compile_test(neg_counter_tag_without_orientation
  "template constraint failure for .template<class Tag> +requires +CounterTag<Tag> struct foundation::algebra::lattices::StrongCounterLattice."
  "the required expression .Tag::claim_orientation. is invalid")
# No array of integers states a clock, and no bytes rebuild one.
foundation_neg_compile_test(neg_happens_before_clock_from_integers
  "element_type::element_type\\(const std::array<long unsigned int, N>&\\) .with long unsigned int N = 2. Tag = void.* is private within this context"
  "HappensBeforeLattice<N, Tag>::element_type::element_type")
foundation_neg_compile_test(neg_happens_before_image_without_io
  "no matching function for call to .foundation::algebra::lattices::HappensBeforeLattice<2>::mint_from_image"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::IO>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test> >.")
foundation_neg_compile_test(neg_happens_before_bit_cast
  "no matching function for call to .bit_cast<foundation::algebra::lattices::HappensBeforeLattice<2>::element_type>"
  "the expression .is_trivially_copyable_v<_To> .with _To = foundation::algebra::lattices::HappensBeforeLattice<2, void>::element_type.. evaluated to .false.")
# A clock of two slots has no slot two.
foundation_neg_compile_test(neg_happens_before_slot_out_of_range
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .\\{anonymous\\}::clock\\.foundation::algebra::lattices::HappensBeforeLattice<2>::element_type::operator..\\(2\\)."
  "foundation/algebra/lattices/HappensBefore\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
# A receive onto a slot at the largest count cannot count the receive.
foundation_neg_compile_test(neg_happens_before_merge_overflows
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .foundation::algebra::lattices::HappensBeforeLattice<2>::causal_merge\\(local, received, 0\\)."
  "in .constexpr. expansion of .foundation::algebra::lattices::HappensBeforeLattice<2>::successor_at\\(")
# A clock that exists cannot be edited in place.
foundation_neg_compile_test(neg_happens_before_clock_is_private
  "is private within this context"
  "HappensBeforeLattice<2>::element_type::clock_.")
# Clocks of two protocols do not join.
foundation_neg_compile_test(neg_happens_before_tags_do_not_join
  "cannot convert .element_type<.*KernelOrderClock>. to .element_type<.*ReplayClock>."
  "initializing argument 2 of .static constexpr foundation::algebra::lattices::HappensBeforeLattice<N, Tag>::element_type foundation::algebra::lattices::HappensBeforeLattice<N, Tag>::join\\(")

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
  "ExecCtx<foundation::effects::Init, .*>::ExecCtx.<brace-enclosed initializer list>.")

# Route two: promotion takes the capability, not its name.
# Closing one route and not the other leaves the gate open, so these two
# fixtures are a pair.
foundation_neg_compile_test(neg_exec_ctx_with_cap_needs_the_capability
  "no matching function for call to"
  "with_cap<")

# A foreground context states that its holder runs on the thread that won
# the producer claim, so each route that builds one without the claim's
# key is refused: braces, the key itself, a byte, and a lifetime started
# over a buffer.  The key refuses the last two routes on its way into the
# mint.  The witness roster refuses a source and a key built with braces.
foundation_neg_compile_test(neg_foreground_ctx_default_built
  "no matching function for call to"
  "ctx_cap::Fg, foundation::effects::Row<> >::ExecCtx.<brace-enclosed initializer list>.")
foundation_neg_compile_test(neg_foreground_key_built
  "is private within this context"
  "foundation::effects::detail::ctx_mint::fg_key::fg_key..")
foundation_neg_compile_test(neg_foreground_ctx_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
foundation_neg_compile_test(neg_foreground_ctx_start_as_array
  "no matching function for call to .start_as_array<foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
foundation_neg_compile_test(neg_foreground_key_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::detail::ctx_mint::fg_key"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::detail::ctx_mint::fg_key")
foundation_neg_compile_test(neg_foreground_key_start_as_array
  "no matching function for call to .start_as_array<foundation::effects::detail::ctx_mint::fg_key"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = foundation::effects::detail::ctx_mint::fg_key.")

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

# The transition algebra of foundation/algebra/Transition.h: the
# coherence of a registry, the one fold, the payload preorder and the
# refinement preorder, over stand-in registries.
foundation_test(test_transition)

# A closure takes no label word: the word comes from a stable id, and a
# stable id refuses a type that names a closure, so two closure keys that
# print one name never share a word on the wire.
foundation_neg_compile_test(neg_label_word_of_a_closure_key
  "a stable id refuses the type"
  "has no declared name")

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

# A derived permission tag has the row of its parent.  An edge in
# permission_rows, or a permission_row member, that gives a shard a row of
# its own is refused, so a shard of an IO region cannot claim the empty row.
foundation_neg_compile_test(neg_permission_row_edge_for_a_derived_tag
  "static assertion failed: permission_row: a derived tag declares a row of its own"
  "required from .struct foundation::permissions::detail::permission_row_lookup<user_code::shard>.")
foundation_neg_compile_test(neg_permission_row_member_for_a_derived_tag
  "static assertion failed: permission_row: a derived tag declares a row of its own"
  "required from .struct foundation::permissions::detail::permission_row_lookup<user_code::shard>.")

# A sealed relation counts its members at every read.  An edge added from
# another file, and a second seal that tries to widen the count, each stop
# the build with the name of the fault.
foundation_neg_compile_test(neg_fail_closed_edge_after_the_seal
  "non-constant condition for static assertion"
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\).")
foundation_neg_compile_test(neg_fail_closed_relation_sealed_twice
  "non-constant condition for static assertion"
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_relation_holds_two_seals\\(\\).")

# The permission tests keep the file names of the tests they are ported
# from and carry a suffix, because test/CMakeLists.txt still registers
# the plain names and a CMake target name is global.
foundation_test(test_permissions_compile_foundation test_permissions_compile.cpp)
foundation_test(test_permission_shared_foundation test_permission_shared.cpp)
foundation_test(test_permission_shared_xor_exclusive_foundation test_permission_shared_xor_exclusive.cpp)
foundation_test(test_read_view_foundation test_read_view.cpp)
# Legal C++ that tries to carry a view out of the door.  An attack that
# still succeeds is a row of the shrink-only ledger in the file.
foundation_test(test_read_view_escape_attacks)

# A token split into subregions that no can_split_into manifest declares.
# The location regex proves the rejection came from the split's own
# static_assert in Permission.h.
foundation_neg_compile_test(neg_permission_split_undeclared_subregion
  "static assertion failed"
  "foundation/permissions/Permission\\.h:[0-9]+:[0-9]+: error: static assertion failed: mint_permission_split<L, R>\\(Permission<In>&&\\) requires can_split_into<In, L, R>::value")

# A fork whose child pack is not a declared partition of the parent.
# The satisfaction notes name the fork's fit concept and the pack trait
# that evaluated false.
foundation_neg_compile_test(neg_permission_fork_pack_does_not_partition
  "constraints not satisfied"
  "satisfaction of .CtxFitsPermissionFork<Ctx, Parent, Children \\.\\.\\.>."
  "can_split_into_pack_v<Parent, Children \\.\\.\\.>.*evaluated to .false.")

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
# A view is a proof about a region, so a borrow is bounded by who may
# hold that region.  A door that reads no context is sound only for the
# empty row, which is the rule SharedPermissionPool::lend states for the
# pooled share of the same region.  The first fixture is a declared row
# that names IO, and the second is a region that declares no row.  A read
# loan opens views through the same door, so its mint carries the gate
# too.
#
# Each second regex names the concept and the source or the tag it was
# checked for, which no fixture source spells.
foundation_neg_compile_test(neg_read_view_effectful_row_without_ctx
  "no matching function for call to .with_read_view\\("
  "required for the satisfaction of .ReadViewNeedsNoCtx<typename Source::tag_type>. .with Source = foundation::permissions::Permission<<unnamed>::MappedRegion")
foundation_neg_compile_test(neg_read_view_undeclared_row_without_ctx
  "no matching function for call to .with_read_view\\("
  "required for the satisfaction of .ReadViewNeedsNoCtx<typename Source::tag_type>. .with Source = foundation::permissions::Permission<<unnamed>::UndecidedRegion")
foundation_neg_compile_test(neg_read_loan_effectful_row_without_ctx
  "no matching function for call to .mint_read_loan\\("
  "required for the satisfaction of .ReadViewNeedsNoCtx<Tag>. .with Tag = <unnamed>::MappedRegion")
foundation_neg_compile_test(neg_read_loan_from_named_token
  "use of deleted function"
  "mint_read_loan parks the token until the loan comes back")

# ── The frame of a borrow ────────────────────────────────────────────
#
# A view lives only inside the body that with_read_view runs, so no view
# leaves the frame of its door.  Each fixture below is one way out: a
# named source that the body could end, a body that asks for a copy, a
# result that carries the view, and a view built in place in a
# longer-lived object.  A read loan is one read right, so it does not
# copy, and a loan ends only the loan of its own region.
foundation_neg_compile_test(neg_read_view_named_source_refused
  "use of deleted function"
  "with_read_view lends its source to the body and hands the source back")
# A class that copies a source's member types is not a source.  The door
# refuses it by family, so the unsatisfied check names is_read_view_source.
foundation_neg_compile_test(neg_read_view_look_alike_source_refused
  "no matching function for call to .*with_read_view"
  "is_read_view_source")
# Only an empty class can brand a view, so ReadView<Region, int> fails the
# IsBrand check inside ReadView.
foundation_neg_compile_test(neg_read_view_brand_not_empty
  "static assertion failed"
  "Brand must be an empty class type")
# A const source is refused by the deleted twin and its std::move message.
# Without the non-const condition on the primary door the call is ambiguous.
foundation_neg_compile_test(neg_read_view_const_source_refused
  "use of deleted function"
  "Pass the source with std::move")
# The deleted twin asks for a real source with an empty row, so a named
# look-alike and a named effectful source meet the rule they break, not the
# std::move message.
foundation_neg_compile_test(neg_read_view_named_look_alike_refused
  "no matching function for call to .*with_read_view"
  "is_read_view_source")
foundation_neg_compile_test(neg_read_view_named_effectful_source_refused
  "no matching function for call to .*with_read_view"
  "ReadViewNeedsNoCtx")
# A read loan names a region and a brand, as a Permission does, and a parked
# token keeps its brand: erasure runs one way only.
foundation_neg_compile_test(neg_read_loan_tag_not_empty
  "static assertion failed"
  "ReadLoan<Tag>: Tag must be an empty non-union class type")
foundation_neg_compile_test(neg_read_loan_brand_not_empty
  "static assertion failed"
  "ReadLoan<Tag, Brand>: Brand must be an empty class type")
foundation_neg_compile_test(neg_lent_permission_does_not_rebrand
  "no matching function for call to .foundation::permissions::LentPermission<"
  "is_same_v<Brand, foundation::brand::DefaultBrand>")
foundation_neg_compile_test(neg_read_view_body_by_value
  "no matching function for call to .with_read_view\\("
  "required for the satisfaction of .ReadViewBody<Body, typename Source::tag_type, typename Source::brand_type>.")
foundation_neg_compile_test(neg_read_view_returned_from_the_body
  "static assertion failed"
  "the body returns a reference, a type that names a ReadView, a type that holds an untyped address")
# A result that holds an untyped address carries the view out of the door
# through type erasure: std::function and a pointer to void name no view.
foundation_neg_compile_test(neg_read_view_body_returns_a_type_erased_callable
  "static assertion failed"
  "a type that holds an untyped address .a pointer to void or to a function")
foundation_neg_compile_test(neg_read_view_body_returns_an_untyped_pointer
  "static assertion failed"
  "a type that holds an untyped address .a pointer to void or to a function")
foundation_neg_compile_test(neg_read_view_stored_past_the_callback
  "no matching function for call to .std::optional<foundation::permissions::ReadView<.anonymous.::Region> >::emplace\\("
  "enable_if<false, foundation::permissions::ReadView<.anonymous.::Region>&>")
foundation_neg_compile_test(neg_read_loan_copied
  "use of deleted function"
  "a ReadLoan is one read right. A copy is a second right")
foundation_neg_compile_test(neg_loan_end_with_the_loan_of_another_region
  "no matching function for call to .mint_permission_after_loan\\("
  "deduced conflicting types for parameter .Brand. \\(.foundation::permissions::<lambda\\(\\)>. and .foundation::permissions::<lambda\\(\\)>.\\)")
foundation_neg_compile_test(neg_loan_end_with_the_loan_of_another_tag
  "no matching function for call to .mint_permission_after_loan\\("
  "deduced conflicting types for parameter .Tag. \\(..anonymous.::Region. and ..anonymous.::Other.\\)")

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

# A read proof comes from a door, and from nothing else.  A program that
# defines the old host type reaches a private constructor, because no
# friend names that type.
foundation_neg_compile_test(neg_read_view_host_door_closed
  "is private within this context"
  "ReadView<Tag, Brand>::ReadView\\(\\) .with Tag = .*Region")

# No route builds a token, the mint key or a read proof without a
# constructor.  std::bit_cast needs a trivially copyable type, and
# std::start_lifetime_as an implicit-lifetime one, so each of the three
# has user-provided constructors and no trivial one.
foundation_neg_compile_test(neg_permission_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::Permission<.*Region"
  "is_trivially_copyable_v<_To>")
foundation_neg_compile_test(neg_permission_forged_by_start_lifetime_as
  "static assertion failed"
  "foundation::permissions::Permission<.*Region.*. is not an implicit-lifetime type")
foundation_neg_compile_test(neg_mint_key_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::perm_mint_key>"
  "is_trivially_copyable_v<_To>")
foundation_neg_compile_test(neg_read_view_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::ReadView<.*Region"
  "is_trivially_copyable_v<_To>")
foundation_neg_compile_test(neg_read_view_forged_by_start_lifetime_as
  "static assertion failed"
  "foundation::permissions::ReadView<.*Region.*. is not an implicit-lifetime type")
foundation_neg_compile_test(neg_read_loan_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::ReadLoan<.*Region"
  "is_trivially_copyable_v<_To>")
foundation_neg_compile_test(neg_lent_permission_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::LentPermission<.*Region"
  "is_trivially_copyable_v<_To>")
foundation_neg_compile_test(neg_read_loan_forged_by_start_lifetime_as
  "static assertion failed"
  "foundation::permissions::ReadLoan<.*Region.*. is not an implicit-lifetime type")
foundation_neg_compile_test(neg_lent_permission_forged_by_start_lifetime_as
  "static assertion failed"
  "foundation::permissions::LentPermission<.*Region.*. is not an implicit-lifetime type")

# No route builds a context, a context key, the capability key, a
# capability or an execution context over Bg without a constructor.
# Each has user-provided constructors and no trivial one, for the same
# reason as the token above.  The second regex names the type that the
# trait or the mandate refused.
foundation_neg_compile_test(neg_bg_context_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::Bg"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::Bg")
foundation_neg_compile_test(neg_bg_context_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::Bg.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_init_context_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::Init"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::Init")
foundation_neg_compile_test(neg_init_context_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::Init.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_test_context_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::Test"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::Test")
foundation_neg_compile_test(neg_test_context_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::Test.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_bg_key_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::detail::ctx_mint::bg_key"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::detail::ctx_mint::bg_key")
foundation_neg_compile_test(neg_bg_key_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::detail::ctx_mint::bg_key.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_init_key_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::detail::ctx_mint::init_key"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::detail::ctx_mint::init_key")
foundation_neg_compile_test(neg_init_key_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::detail::ctx_mint::init_key.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_test_key_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::detail::ctx_mint::test_key"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::detail::ctx_mint::test_key")
foundation_neg_compile_test(neg_test_key_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::detail::ctx_mint::test_key.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_cap_mint_key_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::cap_mint_key"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::cap_mint_key")
foundation_neg_compile_test(neg_cap_mint_key_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::cap_mint_key.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_capability_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::Capability<foundation::effects::Effect::Block, foundation::effects::Bg"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::Capability<foundation::effects::Effect::Block, foundation::effects::Bg")
foundation_neg_compile_test(neg_capability_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::Capability<foundation::effects::Effect::Block, foundation::effects::Bg.* is not an implicit-lifetime type")
foundation_neg_compile_test(neg_bg_exec_ctx_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<")
foundation_neg_compile_test(neg_bg_exec_ctx_forged_by_start_lifetime_as
  "static assertion failed"
  ".foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<.* is not an implicit-lifetime type")

# The checked lifetime start walks each subobject.  An aggregate that
# holds a token passes the mandate of std::start_lifetime_as, and
# std::start_lifetime_as_array checks nothing about a token.  The checked
# start refuses each of the two.
foundation_neg_compile_test(neg_lifetime_start_as_array_proof_holder
  "no matching function for call to .start_as_array<.*HoldsToken>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = .*HoldsToken\\]")
foundation_neg_compile_test(neg_lifetime_start_as_proof_array
  "no matching function for call to .start_as_array<foundation::permissions::Permission<.*Region"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = foundation::permissions::Permission<")

# A pool that ends while a share is out aborts.  The attack runs in a
# child process, and the test passes only on that diagnostic.
foundation_test(test_share_pool_outlived_aborts)

# A share guard that gave its share away cannot lend a read view: the door
# checks holds_share() and aborts.  The attack runs in a child process,
# and a positive control lends from a guard that holds its share.
foundation_test(test_read_view_lend_needs_a_share)

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

# Legal C++ that tries to give two types one stable id.  An attack that
# still succeeds is a row of the shrink-only ledger in the file.
foundation_test(test_identity_attacks)

# Each host owner is defined beside its key, so a second definition is a
# redefinition, and the foreground key is private to the producer claim.
# Only the brand of a claim builds it, and the context of one brand
# passes no gate of another, nor the gate that asks for no brand.  A
# branded context, like the unbranded one, is not built from a byte or
# over a buffer.
foundation_test(test_host_owner_attacks)
foundation_neg_compile_test(neg_host_owner_defined_again
  "redefinition of .struct foundation::effects::host::ForegroundOwner."
  "neg_host_owner_defined_again.cpp:9")
foundation_neg_compile_test(neg_host_background_owner_defined_again
  "redefinition of .struct foundation::effects::host::BackgroundOwner."
  "neg_host_background_owner_defined_again.cpp:9")
foundation_neg_compile_test(neg_host_owner_key_private
  "is private within this context"
  "foundation::effects::host::ForegroundOwner::key\\(\\)")
foundation_neg_compile_test(neg_producer_claim_default_built
  "is private within this context"
  "ProducerClaim<Brand>::ProducerClaim\\(\\) .with Brand = .anonymous.::Brand")
foundation_neg_compile_test(neg_producer_claim_outside_brand
  "is private within this context"
  "ProducerClaim<Brand>::ProducerClaim\\(\\) .with Brand = .anonymous.::Brand"
  "neg_producer_claim_outside_brand.cpp:10")
foundation_neg_compile_test(neg_branded_ctx_other_brand
  "invalid initialization of reference"
  "ctx_cap::BrandedFg<.anonymous.::Stranger>")
foundation_neg_compile_test(neg_branded_ctx_to_unbranded_gate
  "invalid initialization of reference"
  "ctx_cap::BrandedFg<.anonymous.::SelfDeclared>")
foundation_neg_compile_test(neg_branded_foreground_ctx_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<")
foundation_neg_compile_test(neg_branded_foreground_ctx_start_as_array
  "no matching function for call to .start_as_array<foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<")

# A stable id refuses a template argument that names an object by address
# or by reference, a class value that holds an address, and a reflection
# of an entity with internal linkage.
foundation_neg_compile_test(neg_stable_id_refuses_an_object_address
  "a stable id refuses the type"
  "names an object by its address or by a reference")
foundation_neg_compile_test(neg_stable_id_refuses_an_address_inside_a_class_value
  "a stable id refuses the type"
  "cannot be read back to a declared name")
foundation_neg_compile_test(neg_stable_id_refuses_a_reflection_of_an_internal_entity
  "a stable id refuses the type"
  "has internal linkage")
# A NaN prints without its payload and a union value without its active
# member, so two different values print one text.
foundation_neg_compile_test(neg_stable_id_refuses_a_nan_value
  "a stable id refuses the type"
  "prints a text that a different value also prints")
foundation_neg_compile_test(neg_stable_id_refuses_a_union_value
  "a stable id refuses the type"
  "prints a text that a different value also prints")

# ── The owned buffers ─────────────────────────────────────────────────
#
# A buffer starts the lifetime of its elements through the checked start
# or by a value initialization, so a proof element is refused by each
# route, and a copy, which would free one allocation twice, is deleted.
# The second regex of each fixture is the constraint or the deletion as
# the compiler spells it, which the fixture source does not contain.
foundation_test(test_swiss_table_buffer)
foundation_test(test_aligned_buffer)

foundation_neg_compile_test(neg_swiss_table_buffer_proof_slot
  "template constraint failure for .template<class SlotPtr>  requires  SwissTableSlot<SlotPtr> class foundation::SwissTableBuffer."
  "required for the satisfaction of .ImplicitLifetimeThroughout<SlotPtr>. .with SlotPtr = <unnamed>::HoldsToken\\]")
foundation_neg_compile_test(neg_swiss_table_buffer_overaligned_slot
  "template constraint failure for .template<class SlotPtr>  requires  SwissTableSlot<SlotPtr> class foundation::SwissTableBuffer."
  "the expression .alignof \\(SlotPtr\\) <= foundation::swiss_table_group_width \\[with SlotPtr = <unnamed>::WideSlot\\]. evaluated to .false.")
foundation_neg_compile_test(neg_swiss_table_buffer_copied
  "use of deleted function .foundation::SwissTableBuffer<SlotPtr>::SwissTableBuffer\\(const foundation::SwissTableBuffer<SlotPtr>&\\) \\[with SlotPtr = const int\\*\\]."
  "SwissTableBuffer is move-only")

foundation_neg_compile_test(neg_aligned_buffer_proof_element
  "no matching function for call to .foundation::AlignedBuffer<\\{anonymous\\}::HoldsToken>::allocate\\(int\\)."
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = <unnamed>::HoldsToken\\]")
foundation_neg_compile_test(neg_aligned_buffer_value_initialized_proof
  "no matching function for call to .foundation::AlignedBuffer<foundation::permissions::Permission<\\{anonymous\\}::Region> >::allocate_value_initialized\\(int\\)."
  "the expression .is_nothrow_default_constructible_v<T> \\[with T = foundation::permissions::Permission<<unnamed>::Region, foundation::brand::DefaultBrand>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_aligned_buffer_alignment_not_a_power_of_two
  "template constraint failure for .template<class T, long unsigned int Alignment>"
  "the expression .has_single_bit\\(long unsigned int\\)\\(Alignment\\) \\[with Alignment = 24\\]. evaluated to .false.")
foundation_neg_compile_test(neg_aligned_buffer_alignment_below_the_element
  "template constraint failure for .template<class T, long unsigned int Alignment>"
  "the expression .Alignment >= alignof \\(T\\) \\[with Alignment = 4; T = double\\]. evaluated to .false.")
foundation_neg_compile_test(neg_aligned_buffer_copied
  "use of deleted function .foundation::AlignedBuffer<T, Alignment>::AlignedBuffer\\(const foundation::AlignedBuffer<T, Alignment>&\\) \\[with T = int; long unsigned int Alignment = 4\\]."
  "AlignedBuffer owns its storage alone and is move-only")

# ── The thread-local cell ─────────────────────────────────────────────
#
# The cell is built on the first access of each thread, with no argument,
# so its value type is default-constructible, and a store takes only a
# value the cell type is assignable from.
foundation_test(test_thread_local_ref)

foundation_neg_compile_test(neg_thread_local_ref_mint_needs_a_default
  "no matching function for call to .mint_thread_local_ref<\\{anonymous\\}::CounterTag, \\{anonymous\\}::NeedsSeed>\\(\\)."
  "the expression .is_default_constructible_v<T> \\[with T = <unnamed>::NeedsSeed\\]. evaluated to .false.")
foundation_neg_compile_test(neg_thread_local_ref_store_of_another_type
  "no matching function for call to .foundation::ThreadLocalRef<\\{anonymous\\}::CounterTag, \\{anonymous\\}::Counter>::store\\(\\{anonymous\\}::Label\\) const."
  "the expression .is_assignable_v<T&, U&&> \\[with T = <unnamed>::Counter; U = <unnamed>::Label\\]. evaluated to .false.")
