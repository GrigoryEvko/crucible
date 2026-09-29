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
# test/fixy/test_saturate.cpp has the same file name, and a CMake target
# name is global.  Each of the two targets carries the name of its layer.
foundation_test(test_saturate_foundation test_saturate.cpp)
foundation_test(test_signature)
foundation_test(test_algebra_core)
foundation_test(test_lattices_core)
foundation_test(test_lattices_bands)
foundation_test(test_lattices_counters)
foundation_test(test_counter_clock_attacks)
foundation_test(test_effects_core)
foundation_test(test_effects_resources)
foundation_test(test_ctx)
foundation_test(test_capability)
foundation_test(test_computation)
foundation_test(test_diag)
# The JSON emitter keeps a small stack frame.  A frame larger than 8 KiB in
# a function that the test instantiates stops the build of the test.
foundation_test(test_json_emitter_frame)
target_compile_options(test_json_emitter_frame PRIVATE -Werror=frame-larger-than=8192)
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

# Two lattices that pass every law comparing elements through leq alone.
# The first has a leq that runs against its join, the second has bottom()
# above top().  The laws that tie the order to join, meet and the bounds
# refuse both, at a verify helper and at Graded.  The second regex of each
# names the law function and the refused lattice.
foundation_neg_compile_test(neg_lattice_order_against_join
  "no matching function for call to .verify_lattice_axioms_at<Inverted>"
  "the expression .\\(laws_hold<L>\\)\\(\\) \\[with L = foundation::algebra::detail::lattice_self_test::InvertedOrder\\]. evaluated to .false.")
foundation_neg_compile_test(neg_lattice_bottom_above_top
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "the expression .\\(laws_hold<L>\\)\\(\\) \\[with L = foundation::algebra::detail::lattice_self_test::ExchangedBounds\\]. evaluated to .false.")
# Two operation pairs with the signature of a semiring and not its laws:
# subtraction for add, and the maximum for both add and mul.  The laws at
# zero() and one() refuse both at the verify helper.
foundation_neg_compile_test(neg_semiring_subtraction_for_add
  "no matching function for call to .verify_semiring_axioms_at<Subtraction>"
  "the expression .\\(semiring_laws_hold<S>\\)\\(\\) \\[with S = foundation::algebra::detail::lattice_self_test::SubtractionSemiring\\]. evaluated to .false.")
foundation_neg_compile_test(neg_semiring_zero_does_not_annihilate
  "no matching function for call to .verify_semiring_axioms_at<\\{anonymous\\}::MaxMax>"
  "the expression .\\(semiring_laws_hold<S>\\)\\(\\) \\[with S = <unnamed>::MaxMax\\]. evaluated to .false.")

# A grade outside the lattice's order is rejected during constant
# evaluation by the in-body CRUCIBLE_PRE: a stored grade at the keyed
# constructor, and a value that is its own grade at the constructor from
# the value alone.  The first regex of each proves the trap came from the
# guard in Graded.h, and the second names the constructor it refused.
foundation_neg_compile_test(neg_graded_stored_grade_outside_the_order
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "Graded<foundation::algebra::detail::graded_self_test::self_test_authority>\\(foundation::algebra::detail::graded_self_test::self_test_authority::key\\(\\), .*, 9\\)")
foundation_neg_compile_test(neg_graded_element_grade_outside_the_order
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "TrivialChainLattice, unsigned char>::Graded\\(9\\)")

# Where the grade is the value, the grade that the keyed constructor
# takes is a witness.  The same guard refuses a witness inside the order
# that names another grade.
foundation_neg_compile_test(neg_graded_witness_names_another_grade
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "MonotoneLattice<int, std::less<int> >, int>::Graded<\\{anonymous\\}::fixture_authority>\\(\\{anonymous\\}::fixture_authority::key\\(\\), 5, 10\\)")

# A stored grade is a claim about the value, and each door that makes one
# asks for a grade_key<Authority>.  Braces with no key meet no
# constructor, a key built outside its authority meets the private
# constructor, and a lattice that states nothing about its grade opens
# neither the keyless write in place nor a default value.  The second
# regex of each names the refused candidate or the claim-subject gate.
foundation_neg_compile_test(neg_graded_grade_without_key
  "no matching function for call to .foundation::algebra::Graded<.*OneByteValue>::Graded\\(<brace-enclosed initializer list>\\)."
  "candidate 1: .template<class Authority> constexpr foundation::algebra::Graded<M, L, T>::Graded\\(const foundation::algebra::grade_key<Authority>&, T, grade_type\\)")
foundation_neg_compile_test(neg_graded_key_outside_its_authority
  "grade_key<Authority>::grade_key\\(\\) \\[with Authority = \\{anonymous\\}::Stranger\\]. is private within this context"
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: declared private here")
foundation_neg_compile_test(neg_graded_peek_mut_without_key
  "no matching function for call to .foundation::algebra::Graded<.*OneByteValue>::peek_mut\\(\\)."
  "the expression .\\(claim_subject_v<L>\\) == foundation::algebra::ClaimSubject::slot \\[with L = foundation::algebra::detail::lattice_self_test::TrivialBoolLattice\\]. evaluated to .false.")
foundation_neg_compile_test(neg_graded_default_value_claims_bytes
  "no matching function for call to .foundation::algebra::Graded<.*OneByteValue>::Graded\\(\\)."
  "required by the constraints of .template<foundation::algebra::ModalityKind M, class L, class T> constexpr foundation::algebra::Graded<M, L, T>::Graded\\(\\) requires ! foundation::algebra::Graded<M, L, T>::stores_grade_ \\|\\| \\(GradeIgnoresBytes<L>\\).")

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

# A concurrent row admits resource tags only, and it refuses a pack whose
# sum on one axis wraps.  A pair whose sum wraps on one axis is not
# schedulable together and has no sum type.  A tag whose kind names no
# axis is not a tag, and neither is a lookalike of a real axis.  The
# second regex of each names the constraint that refused.
foundation_neg_compile_test(neg_concurrent_row_refuses_a_non_tag
  "template constraint failure for .template<class ... Tags> +requires"
  "the expression .\\(\\.\\.\\. && ResourceTag<Tags>\\) \\[with Tags = \\{int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_concurrent_row_that_wraps_on_one_axis
  "template constraint failure for .template<class ... Tags> +requires"
  "the expression .\\(pack_sums_fit_<Tags \\.\\.\\.>\\)\\(\\) \\[with Tags = \\{foundation::effects::resource::SmBudget<18446744073709551615>, foundation::effects::resource::SmBudget<2>\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_concurrent_rows_that_wrap_are_not_schedulable
  "constraints not satisfied"
  "satisfaction of .ConcurrentlySchedulable<First, Second>.")
foundation_neg_compile_test(neg_concurrent_row_sum_that_wraps
  "template constraint failure for .template<class R1, class R2> +requires +ConcurrentlySchedulable<R1, R2> using foundation::effects::concurrent_row_sum_t"
  "the expression .\\(sums_fit_pairwise_<R1, R2>\\)\\(\\) .with R1 = foundation::effects::ConcurrentRow<foundation::effects::resource::HbmBytes<18446744073709551615> >")
# The sum of any number of rows has no type when a partial sum wraps or an
# argument is not a row.  The constraint refuses at the call site, before
# the fold names a partial sum.
foundation_neg_compile_test(neg_concurrent_row_n_that_wraps
  "template constraint failure for .template<class \\.\\.\\. Rs> +requires \\(rows_sum_fit_<Rs \\.\\.\\.>\\)\\(\\) using foundation::effects::concurrent_row_n_t"
  "the expression .\\(rows_sum_fit_<Rs \\.\\.\\.>\\)\\(\\) \\[with Rs = \\{foundation::effects::ConcurrentRow<foundation::effects::resource::SmBudget<18446744073709551615> >, foundation::effects::ConcurrentRow<foundation::effects::resource::SmBudget<1> >, foundation::effects::ConcurrentRow<>\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_concurrent_row_n_refuses_a_non_row
  "template constraint failure for .template<class \\.\\.\\. Rs> +requires \\(rows_sum_fit_<Rs \\.\\.\\.>\\)\\(\\) using foundation::effects::concurrent_row_n_t"
  "the expression .\\(rows_sum_fit_<Rs \\.\\.\\.>\\)\\(\\) \\[with Rs = \\{int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_resource_tag_refuses_a_forged_kind
  "constraints not satisfied"
  "nested requirement .IsResourceKind<.*>. is not satisfied")
foundation_neg_compile_test(neg_resource_tag_refuses_a_lookalike
  "no matching function for call to .admit_tag<LookalikeSmBudget>\\(\\)."
  "nested requirement .is_axis_tag_<T>\\(\\). is not satisfied")

# A mask built during constant evaluation from a bit that names no
# effect atom reaches the trap in from_raw.  The location regex proves
# the trap came from Row.h rather than from the fixture.
foundation_neg_compile_test(neg_effect_mask_from_raw_refuses_a_high_bit
  "is not a constant expression"
  "foundation/effects/Row\\.h:[0-9]+:[0-9]+: error: .__builtin_trap\\(\\).")
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
# In Graded, up is the weaker claim.  A version counter in its numeric
# order, a product with one such component, the dual of a use counter and
# the determinism chain each put the stronger claim higher, and the
# orientation of a chain that states none is unknown.  Graded refuses
# each of them as a stored grade.  The second regex names the refused
# lattice beside a payload that it does not name, and the third says that
# neither the orientation nor a grade derived from the value admits it.
foundation_neg_compile_test(neg_graded_version_counter_numeric_order
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "satisfaction of .LatticeGradesValue<L, T>. .with L = foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>. T = int."
  "no operand of the disjunction is satisfied")
foundation_neg_compile_test(neg_graded_version_counter_in_product
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "satisfaction of .LatticeGradesValue<L, T>. .with L = foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch> >, foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::generation> >. T = int."
  "no operand of the disjunction is satisfied")
foundation_neg_compile_test(neg_graded_use_counter_dual
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "satisfaction of .LatticeGradesValue<L, T>. .with L = foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::peak_bytes> >. T = int."
  "no operand of the disjunction is satisfied")
foundation_neg_compile_test(neg_graded_stored_grade_in_a_stronger_up_chain
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "satisfaction of .LatticeGradesValue<L, T>. .with L = foundation::algebra::lattices::DetSafeLattice. T = int."
  "no operand of the disjunction is satisfied")
foundation_neg_compile_test(neg_graded_stored_grade_in_an_unstated_order
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "satisfaction of .LatticeGradesValue<L, T>. .with L = <unnamed>::UnstatedChain. T = int."
  "no operand of the disjunction is satisfied")
# A counter tag that does not state its claim orientation is refused.
foundation_neg_compile_test(neg_counter_tag_without_orientation
  "template constraint failure for .template<class Tag> +requires +CounterTag<Tag> struct foundation::algebra::lattices::StrongCounterLattice."
  "the required expression .Tag::claim_orientation. is invalid")
# A counter tag that states a name and an orientation but carries a data
# member is refused by the emptiness check of CounterTag.
foundation_neg_compile_test(neg_counter_tag_with_state
  "template constraint failure for .template<class Tag> +requires +CounterTag<Tag> struct foundation::algebra::lattices::StrongCounterLattice."
  "the expression .is_empty_v<Tag> \\[with Tag = stateful_step\\]. evaluated to .false.")
# No array of integers states a clock, and no bytes rebuild one.
foundation_neg_compile_test(neg_happens_before_clock_from_integers
  "element_type::element_type\\(const std::array<long unsigned int, N>&\\) .with long unsigned int N = 2. Tag = void.* is private within this context"
  "HappensBeforeLattice<N, Tag>::element_type::element_type")
foundation_neg_compile_test(neg_happens_before_image_without_io
  "no matching function for call to .foundation::algebra::lattices::HappensBeforeLattice<2>::mint_from_image"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::IO>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test> >.")
# A clock over a tag with no source path has no image door, even under IO.
foundation_neg_compile_test(neg_happens_before_image_without_source_path
  "no matching function for call to .foundation::algebra::lattices::HappensBeforeLattice<2, \\{anonymous\\}::HiddenClock>::mint_from_image"
  "required for the satisfaction of .ClockWireProtocol<Tag>. .with Tag = <unnamed>::HiddenClock.")
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

# The row of a context is the bound of mint_from_ctx, not what its source
# permits, and a source is not a context.  The second regex of each names
# the conjunct of the gate and the type that it refused.
foundation_neg_compile_test(neg_mint_from_ctx_atom_outside_the_row
  "no matching function for call to .mint_from_ctx<foundation::effects::Effect::IO>\\(const foundation::effects::detail::ctx_witnesses::BgWitness&\\)."
  "required for the satisfaction of .CtxOwnsCapability<Ctx, E>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >; E = foundation::effects::Effect::IO\\]")
foundation_neg_compile_test(neg_mint_from_ctx_source_is_not_a_context
  "no matching function for call to .mint_from_ctx<foundation::effects::Effect::Alloc>\\(const foundation::effects::Bg&\\)."
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = foundation::effects::Bg\\]")

# A context lends its source only when its row covers the source, and it
# narrows its row but never widens it.  The second regex of each names the
# row check and the two rows it compared.
foundation_neg_compile_test(neg_exec_ctx_cap_from_a_narrower_row
  "no matching function for call to .foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >::cap\\(\\) const."
  "the expression .is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>; R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_exec_ctx_cap_from_init_without_block
  "no matching function for call to .foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >::cap\\(\\) const."
  "the expression .is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>; R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_exec_ctx_in_row_widens_the_drain_row
  "no matching function for call to .foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >::in_row<foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\(\\) const."
  "required for the satisfaction of .Subrow<NewRow, Row>. \\[with NewRow = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>; Row = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>\\]")
foundation_neg_compile_test(neg_exec_ctx_in_row_widens_the_init_row
  "no matching function for call to .foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >::in_row<foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >\\(\\) const."
  "required for the satisfaction of .Subrow<NewRow, Row>. \\[with NewRow = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>; Row = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>\\]")

# The passkey constructor is the only one Permission declares, so a
# translation unit that cannot make a key cannot make a token.  The
# second regex proves the rejection is on Permission's own constructor
# and not on the fixture's tag type.
foundation_neg_compile_test(neg_permission_forged_without_key
  "no matching function for call to"
  "Permission<.*ForgedTag>::Permission")

# The fork keeps the child tokens and lends each body a view, so the
# parent it combines after the join exists beside no live child.  A body
# that asks for its token is refused at the door, a body that keeps its
# view past its frame is refused at the copy, and a body that asks for the
# view of another region of the tag is refused at the door.
foundation_neg_compile_test(neg_permission_fork_body_takes_the_token
  "constraints not satisfied"
  "can_each_body_take_its_child_v<Ctx, Brand, std::tuple<[A-Za-z_]+ \\.\\.\\.>, std::tuple<Callables \\.\\.\\.> >.*evaluated to .false.")
foundation_neg_compile_test(neg_permission_fork_body_keeps_its_view
  "use of deleted function"
  "a WriteView lives in the frame of its body. A copy could outlive the join")
foundation_neg_compile_test(neg_permission_fork_body_of_another_region
  "constraints not satisfied"
  "can_each_body_take_its_child_v<Ctx, Brand, std::tuple<[A-Za-z_]+ \\.\\.\\.>, std::tuple<Callables \\.\\.\\.> >.*evaluated to .false.")

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

# Route three: an explicit specialization replaces the whole class, so a
# name match would admit a forged scope.  IsExecCtx reads the structure of
# the class by reflection.  The first specialization holds no source, and
# the second holds a source that does not permit its row.  The second
# regex of each names the specialization that the concept refused.
foundation_neg_compile_test(neg_exec_ctx_specialization_without_source
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Bg, foundation::effects::Effect::IO> >\\]"
  "the expression .is_exec_ctx_v<T> \\[with T = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Bg, foundation::effects::Effect::IO> >\\]. evaluated to .false.")
foundation_neg_compile_test(neg_exec_ctx_specialization_outside_permitted_row
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::Init> >\\]"
  "the expression .is_exec_ctx_v<T> \\[with T = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::Init> >\\]. evaluated to .false.")

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

# The witnessed lift reads the context's row, not the caller's word, and
# a capability source is not a context.
foundation_neg_compile_test(neg_lift_in_ctx_without_the_effect
  "constraints not satisfied"
  "row_contains_v")
foundation_neg_compile_test(neg_mint_computation_in_ctx_source_is_not_a_context
  "no matching function for call to .foundation::effects::Computation<foundation::effects::Row<>, int>::mint_computation_in_ctx<foundation::effects::Effect::Bg>\\(const foundation::effects::Bg&, int\\)."
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = foundation::effects::Bg\\]")

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

# The substrate is a private base, so its unchecked consume and peek_mut
# are not members of a Computation.  The second regex of each names the
# base that the access check refused.
foundation_neg_compile_test(neg_computation_consume_is_not_a_member
  "constexpr T foundation::algebra::Graded<M, L, T>::consume\\(\\) && \\[with foundation::algebra::ModalityKind M = foundation::algebra::ModalityKind::Relative; L = foundation::effects::EffectRowLattice::At<>; T = foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test>\\]. is inaccessible within this context"
  "is not an accessible base of .std::remove_reference<foundation::effects::Computation<foundation::effects::Row<>, foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test> >&>::type.")
foundation_neg_compile_test(neg_computation_peek_mut_is_not_a_member
  "constexpr T& foundation::algebra::Graded<M, L, T>::peek_mut\\(const foundation::algebra::grade_key<Authority>&\\) & requires .* \\[with Authority = \\{anonymous\\}::outside_authority; foundation::algebra::ModalityKind M = foundation::algebra::ModalityKind::Relative; L = foundation::effects::EffectRowLattice::At<foundation::effects::Effect::Bg>; T = int\\]. is inaccessible within this context"
  ".foundation::algebra::Graded<foundation::algebra::ModalityKind::Relative, foundation::effects::EffectRowLattice::At<foundation::effects::Effect::Bg>, int>. is not an accessible base of .foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>, int>.")

# An engaged row is built only by the witnessed mint: the constructor from
# a value is private, and the default constructor admits the empty row
# alone.
foundation_neg_compile_test(neg_computation_engaged_row_from_a_value
  "constexpr foundation::effects::Computation<R, T>::Computation\\(T\\) \\[with R = foundation::effects::Row<foundation::effects::Effect::IO>; T = int\\]. is private within this context"
  "foundation/effects/Computation\\.h:[0-9]+:[0-9]+: note: declared private here")
foundation_neg_compile_test(neg_computation_engaged_row_default_built
  "no matching function for call to .foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int>::Computation\\(\\)."
  "the expression .\\(row_size_v<R>\\) == 0 \\[with R = foundation::effects::Row<foundation::effects::Effect::IO>\\]. evaluated to .false.")

# Each route that hands the payload out carries the payload constraint of
# extract: the substrate view in both forms, map and then.  The second
# regex of each names the payload that the constraint refused.
foundation_neg_compile_test(neg_computation_graded_move_of_a_capability
  "no matching function for call to .foundation::effects::Computation<foundation::effects::Row<>, foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test> >::graded\\(\\)."
  "the expression .extract_admits_payload_v<T> \\[with T = foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_computation_graded_view_of_a_context
  "no matching function for call to .foundation::effects::Computation<foundation::effects::Row<>, foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> > >::graded\\(\\) const."
  "the expression .extract_admits_payload_v<T> \\[with T = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]. evaluated to .false.")
foundation_neg_compile_test(neg_computation_map_hands_out_a_capability
  "no matching function for call to .foundation::effects::Computation<foundation::effects::Row<>, foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test> >::map\\(main\\(\\)::<lambda\\(IoCap\\)>\\)."
  "the expression .extract_admits_payload_v<T> \\[with T = foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_computation_then_hands_out_a_capability
  "no matching function for call to .foundation::effects::Computation<foundation::effects::Row<>, foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test> >::then\\(main\\(\\)::<lambda\\(IoCap\\)>\\)."
  "the expression .extract_admits_payload_v<T> \\[with T = foundation::effects::Capability<foundation::effects::Effect::IO, foundation::effects::Test>\\]. evaluated to .false.")

foundation_test(test_decide)
foundation_test(test_decide_fuzz)
foundation_test(test_pre_post)

# The contract handler of src/foundation/ContractHandler.cpp, in child
# processes.  A report names the condition, its position and the note of a
# message form.  A second violation that interrupts a blocked report on the
# same thread aborts at once.
foundation_test(test_contract_handler)

# CRUCIBLE_PRE under the ignore semantic must compile to the same machine
# code as a bare [[assume]].  This target is built with -O3 and
# CRUCIBLE_CONTRACT_IGNORE_OPTIONS instead of the preset's debug flags,
# and the sanitizer flags are left off because they change code
# generation.  Each function goes in its own section so the __start_ and
# __stop_ symbols bound one probe each.  See test_pre_post_cost.cpp.
add_executable(test_pre_post_cost test_pre_post_cost.cpp)
target_link_libraries(test_pre_post_cost PRIVATE foundation)
target_compile_options(test_pre_post_cost PRIVATE -O3 -ffunction-sections
  ${CRUCIBLE_CONTRACT_IGNORE_OPTIONS})
add_test(NAME test_pre_post_cost COMMAND test_pre_post_cost)
set_tests_properties(test_pre_post_cost PROPERTIES LABELS "foundation")

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

# test/CMakeLists.txt registers test_reflect for test/test_reflect.cpp,
# and a CMake target name is global.  This target carries the name of
# its layer.
foundation_test(test_reflect_foundation test_reflect.cpp)

# The enumerator walk calls its callback with a value and a name, so a
# callback of another arity meets no call.  bits_to_string admits a null
# buffer only with a capacity of 0, and its precondition stops a constant
# evaluation that breaks the rule.  GCC names no call site for a failed
# contract predicate, so the second regex of that fixture is the header
# that holds the contract.
foundation_neg_compile_test(neg_reflect_for_each_enumerator_zero_arg_callback
  "foundation/reflect/EnumName\\.h:[0-9]+:[0-9]+: error: no match for call to .\\(main\\(\\)::<lambda\\(\\)>\\) \\(const \\{anonymous\\}::Signal&, const std::basic_string_view<char>&\\)."
  "candidate expects 0 arguments, 2 provided")
foundation_neg_compile_test(neg_reflect_bits_to_string_null_buffer_with_capacity
  "error: contract predicate is false in constant expression"
  "foundation/reflect/Enumerate\\.h:[0-9]+:[0-9]+: error: contract predicate")

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

# A combinator and its dual agree on whether a plain protocol may hold
# them.  The coherence check names the pair from each side.
foundation_neg_compile_test(neg_transition_plainness_differs_on_the_shape
  "the registration of template<class K> struct neg_transition_plainness_differs_on_the_shape_types::Mark is incoherent"
  "the dual disagrees on whether a plain protocol may hold it")
foundation_neg_compile_test(neg_transition_plainness_differs_on_the_dual
  "the registration of template<class K> struct neg_transition_plainness_differs_on_the_dual_types::Unmark is incoherent"
  "the dual disagrees on whether a plain protocol may hold it")

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

# Each query of a relation asks for the reflection of a namespace.  A
# class that holds a static member of type edge is not one, and each
# query refuses it on its own.
foundation_neg_compile_test(neg_fail_closed_admits_of_a_class
  "static assertion failed: fail_closed::admits<Ns, From, To>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_edge_count_of_a_class
  "static assertion failed: fail_closed::edge_count<Ns>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_is_antisymmetric_of_a_class
  "static assertion failed: fail_closed::is_antisymmetric<Ns>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_is_intra_namespace_of_a_class
  "static assertion failed: fail_closed::is_intra_namespace<Ns>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_has_edge_from_of_a_class
  "static assertion failed: fail_closed::has_edge_from<Ns, T>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_has_edge_to_of_a_class
  "static assertion failed: fail_closed::has_edge_to<Ns, T>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_edge_count_from_of_a_class
  "static assertion failed: fail_closed::edge_count_from<Ns, T>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")
foundation_neg_compile_test(neg_fail_closed_every_class_in_has_edge_of_a_class
  "static assertion failed: fail_closed::every_class_in_has_edge<Ns, TagNs, End, Excluded\\.\\.\\.>: Ns and TagNs must be reflections of namespaces"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\)")
foundation_neg_compile_test(neg_fail_closed_every_edge_is_admitted_of_a_class
  "static assertion failed: fail_closed::every_edge_is_admitted<Ns>: Ns must be the reflection of a namespace"
  "is_namespace\\(\\^\\^\\{anonymous\\}::NotARelation\\). evaluates to false")

# Each query reads the seal before it answers, so a member added after
# the header stops the build at every query, not only at admits.
foundation_neg_compile_test(neg_fail_closed_edge_count_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::edge_count<\\^\\^\\{anonymous\\}::ingest>\\(\\).")
foundation_neg_compile_test(neg_fail_closed_is_antisymmetric_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::is_antisymmetric<\\^\\^\\{anonymous\\}::ingest>\\(\\).")
foundation_neg_compile_test(neg_fail_closed_is_intra_namespace_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::is_intra_namespace<\\^\\^\\{anonymous\\}::ingest>\\(\\).")
foundation_neg_compile_test(neg_fail_closed_has_edge_from_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::has_edge_from<\\^\\^\\{anonymous\\}::ingest, \\{anonymous\\}::Raw>\\(\\).")
foundation_neg_compile_test(neg_fail_closed_has_edge_to_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::has_edge_to<\\^\\^\\{anonymous\\}::ingest, \\{anonymous\\}::Checked>\\(\\).")
foundation_neg_compile_test(neg_fail_closed_edge_count_from_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::edge_count_from<\\^\\^\\{anonymous\\}::ingest, \\{anonymous\\}::Raw>\\(\\).")
foundation_neg_compile_test(neg_fail_closed_every_class_in_has_edge_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::every_class_in_has_edge<\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::vacant")
foundation_neg_compile_test(neg_fail_closed_every_edge_is_admitted_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::every_edge_is_admitted<\\^\\^\\{anonymous\\}::ingest>\\(\\).")

# unique_target reads a relation as a function of its From, so it
# refuses a From with no edge and a From with two.
foundation_neg_compile_test(neg_fail_closed_unique_target_without_an_edge
  "static assertion failed: fail_closed::unique_target<Ns, T>: the relation declares no edge from T"
  "unique_target\\(\\) \\[with std::meta::info Ns = \\^\\^\\{anonymous\\}::ingest; T = \\{anonymous\\}::Stored")
foundation_neg_compile_test(neg_fail_closed_unique_target_of_two_edges
  "static assertion failed: fail_closed::unique_target<Ns, T>: the relation declares more than one edge from T"
  "unique_target\\(\\) \\[with std::meta::info Ns = \\^\\^\\{anonymous\\}::ingest; T = \\{anonymous\\}::Raw")

foundation_test(test_permissions_compile)
foundation_test(test_permission_shared)
foundation_test(test_permission_shared_xor_exclusive)
foundation_test(test_read_view)
# Legal C++ that tries to carry a view out of the door.  An attack that
# still succeeds is a row of the shrink-only ledger in the file.
foundation_test(test_read_view_escape_attacks)

# A token split into subregions that no can_split_into manifest declares.
# The location regex proves the rejection came from the split's own
# static_assert in Permission.h.
foundation_neg_compile_test(neg_permission_split_undeclared_subregion
  "static assertion failed"
  "foundation/permissions/Permission\\.h:[0-9]+:[0-9]+: error: static assertion failed: mint_permission_split<L, R>\\(Permission<In>&&\\) requires can_split_into<In, L, R>::value")

# A union of two permission sets that share a tag.  The location regex
# proves the refusal came from the union's own class in PermSet.h.
foundation_neg_compile_test(neg_perm_set_union_shared_tag
  "foundation/permissions/PermSet\\.h:[0-9]+:[0-9]+: error: static assertion failed: foundation::permissions \\[PermissionImbalance\\]: perm_set_union_t requires disjoint operands"
  "In instantiation of .struct foundation::permissions::perm_set_union<foundation::permissions::PermSet<user_code::Alpha, user_code::Beta>, foundation::permissions::PermSet<user_code::Alpha> >.")

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

# Each conjunct of the inline arm's fit, refused at the door.  Without
# the conjunct the call passes the door and fails later, inside the body,
# with a message that names no fork, so each note below tells the two
# apart.
foundation_neg_compile_test(neg_permission_fork_parent_row_not_admitted
  "constraints not satisfied"
  "satisfaction of .CtxAdmitsPermission<Parent, Ctx>.")
foundation_neg_compile_test(neg_permission_fork_child_row_not_admitted
  "constraints not satisfied"
  "\\(CtxAdmitsPermission<Children, Ctx> && \\.\\.\\.\\)")
foundation_neg_compile_test(neg_permission_fork_split_witness_missing
  "constraints not satisfied"
  "has_split_pack_authoring_witness_v<Parent, Children \\.\\.\\.>.*evaluated to .false.")
foundation_neg_compile_test(neg_permission_fork_body_may_throw
  "constraints not satisfied"
  "can_each_body_take_its_child_v<Ctx, Brand, std::tuple<[A-Za-z_]+ \\.\\.\\.>, std::tuple<Callables \\.\\.\\.> >.*evaluated to .false.")

# A manifest that lists one child twice passes the door, and the fork's
# own check refuses it with a message that names the arm.
foundation_neg_compile_test(neg_permission_fork_duplicate_child_names_the_fork
  "static assertion failed"
  "mint_permission_fork_inline: Child region tags must be PAIRWISE DISTINCT")

# The fork body is a private member of PermissionForkRunner, and the two
# fork mints are its only friends: no other scope calls it, derives from
# the runner to reach it, or takes its address.
foundation_neg_compile_test(neg_permission_fork_runner_body_is_private
  "is private within this context"
  "PermissionForkRunner::run_")
foundation_neg_compile_test(neg_permission_fork_runner_derived_look_alike
  "cannot derive from .final. base"
  "PermissionForkRunner")
foundation_neg_compile_test(neg_permission_fork_runner_body_address
  "is private within this context"
  "PermissionForkRunner::run_")

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
# defines foundation::permissions::host::BorrowIssuer reaches a private
# constructor, because no friend names that type.
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

# A token and a share guard live in a scope or a handle.  Each deletes its
# class-scope operator new, so a new-expression and std::make_unique reach
# a deleted function.  The second regex is the reason that the header
# gives for the refusal.
foundation_neg_compile_test(neg_permission_heap_new
  "use of deleted function .static void\\* foundation::permissions::Permission<Tag, Brand>::operator new\\(std::size_t\\)"
  "a Permission lives in a scope or a handle, not on the heap")
foundation_neg_compile_test(neg_permission_make_unique
  "use of deleted function .static void\\* foundation::permissions::Permission<Tag, Brand>::operator new\\(std::size_t\\)"
  "a Permission lives in a scope or a handle, not on the heap")
foundation_neg_compile_test(neg_shared_permission_guard_heap_new
  "use of deleted function .static void\\* foundation::permissions::SharedPermissionGuard<Tag, Brand>::operator new\\(std::size_t\\)"
  "a SharedPermissionGuard lives in the scope that holds its share, not on the heap")

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
# A closure with captures is implicit-lifetime, and GCC 16 reflects no
# capture, so a walk over bases and members finds nothing in it.  The
# checked start refuses a class whose state the walk cannot read.
foundation_neg_compile_test(neg_lifetime_start_as_capturing_closure
  "no matching function for call to .start_as_array<.*EpochCarrier>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = <unnamed>::._anon_[0-9]+\\]")

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

# The public row alias refuses a tag with no row, rather than read as
# void, and it refuses a member that names a type which is not a row.
foundation_neg_compile_test(neg_permission_row_t_undeclared_tag
  "static assertion failed: permission_row: no effect row is declared for this permission tag"
  "permission_row_lookup<\\{anonymous\\}::Undeclared>"
  "the comparison reduces to .\\(\\^\\^void != \\^\\^void\\).")
foundation_neg_compile_test(neg_permission_row_not_a_row
  "static assertion failed: permission_row: the row declared for this permission tag is not a foundation::effects::Row"
  "permission_row_lookup<\\{anonymous\\}::RowIsAnInt>"
  "IsInstanceOf<int, \\^\\^foundation::effects::Row>\\)?. evaluates to false")

# ── The tag, the context and the token concepts ───────────────────────

# A tag with a data member is a class, and the emptiness half of
# PermissionTag is what refuses it.
foundation_neg_compile_test(neg_permission_tag_not_empty
  "static assertion failed: Permission<Tag>: Tag must be an empty non-union class type"
  "is_empty_v<T> \\[with T = <unnamed>::Stateful\\]. evaluated to .false."
  ".\\{anonymous\\}::Stateful. is not an empty class")

# A class that declares a row_type is not an execution context, so it
# cannot claim an effect for a root mint.
foundation_neg_compile_test(neg_permission_root_ctx_forged
  "constraints not satisfied"
  "satisfaction of .PermissionRootArgs<Tag, Args \\.\\.\\.>. \\[with Tag = <unnamed>::NeedsIo. Args = \\{<unnamed>::LooksLikeACtx\\}\\]")

# IsPermission holds for an exclusive token only, and IsSharedPermission
# for a shared token only.
foundation_neg_compile_test(neg_permission_is_permission_refuses_plain
  "no matching function for call to .takes_a_token\\(int\\)."
  "is_permission_v<T> \\[with T = int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_permission_is_shared_permission_refuses_exclusive
  "no matching function for call to .takes_a_shared_token"
  "is_shared_permission_v<T> \\[with T = foundation::permissions::Permission<<unnamed>::Region, .*\\]. evaluated to .false.")

# ── The fit of a combine and of the n-ary mints ───────────────────────

# A ctx-bound combine rebuilds the parent under its context, so the
# context must admit every row the call names.
foundation_neg_compile_test(neg_permission_combine_ctx_refuses_row
  "constraints not satisfied"
  "satisfaction of .PermissionCombineArgs<In, Args \\.\\.\\.>. \\[with In = <unnamed>::Whole. Args = \\{foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg")

# The n-ary mints consume their tokens, so an lvalue token would stay
# live beside what the mint returns.  The shape check refuses it.
foundation_neg_compile_test(neg_permission_split_n_lvalue_token
  "satisfaction of .PermissionSplitNArgs<std::tuple<[A-Za-z_]+ \\.\\.\\.>, Args \\.\\.\\.>."
  "mint_args_shaped_<MaxCtx, NPerms, Args \\.\\.\\.>\\)\\(\\) \\[with MaxCtx = 1. NPerms = 1. Args = \\{foundation::permissions::Permission<<unnamed>::Whole, [^}]*>&\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_permission_combine_n_lvalue_token
  "satisfaction of .PermissionCombineNArgs<Parent, Args \\.\\.\\.>."
  "mint_args_shaped_<MaxCtx, NPerms, Args \\.\\.\\.>\\)\\(\\) \\[with MaxCtx = 1. NPerms = 0. Args = \\{foundation::permissions::Permission<<unnamed>::Left, [^}]*>&, ")

# ── The brand, the root and the admission doors ───────────────────────

# A brand with a data member is a class, and the emptiness check in
# Permission's class body is what refuses it.
foundation_neg_compile_test(neg_permission_brand_not_empty
  "static assertion failed: Permission<Tag, Brand>: Brand must be an empty class type"
  "is_empty_v<B> \\[with B = <unnamed>::StatefulBrand\\]. evaluated to .false.")

# The token form of the root mint has no context to compare a row
# against, so the static_assert in its body refuses an effectful tag.
foundation_neg_compile_test(neg_permission_root_without_ctx_refuses_row
  "static assertion failed: mint_permission_root<Tag>\\(\\) without an ExecCtx is only valid"
  "permission_row_empty_v<foundation::permissions::tag::HugePageTag>\\)\\). evaluates to false")

# A federation peer token comes only from a verified handshake.  The root
# mint refuses the tag without a context and with one.
foundation_neg_compile_test(neg_permission_root_refuses_federated_peer
  "no matching function for call to .mint_permission_root<foundation::permissions::tag::FederatedPeer<\\{anonymous\\}::PeerOrg> >\\(\\)."
  "required for the satisfaction of .RootMintableTag<Tag>. \\[with Tag = foundation::permissions::tag::FederatedPeer<<unnamed>::PeerOrg>\\]"
  "the expression .!\\(is_federated_peer_tag_v<Tag>\\)")
foundation_neg_compile_test(neg_permission_root_ctx_refuses_federated_peer
  "no matching function for call to .mint_permission_root<foundation::permissions::tag::FederatedPeer<\\{anonymous\\}::PeerOrg> >\\(\\{anonymous\\}::IoCtx&\\)."
  "required for the satisfaction of .RootMintableTag<Tag>. \\[with Tag = foundation::permissions::tag::FederatedPeer<<unnamed>::PeerOrg>\\]"
  "the expression .!\\(is_federated_peer_tag_v<Tag>\\)")

# Only an admission builds the key of the federation peer token.
foundation_neg_compile_test(neg_federation_admission_key_outside_admission
  ".constexpr foundation::permissions::federation_admission_key::federation_admission_key\\(\\). is private within this context"
  "foundation/permissions/Permission\\.h:[0-9]+:[0-9]+: note: declared private here")

# The admission and the handoff each refuse a context that does not
# admit the tag's row, at their own door.
foundation_neg_compile_test(neg_permission_admit_ctx_refuses_row
  "no matching function for call to .admit_permission\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "required for the satisfaction of .CtxAdmitsPermission<Tag, Ctx>. \\[with Tag = <unnamed>::NeedsIo")
foundation_neg_compile_test(neg_permission_handoff_ctx_refuses_row
  "no matching function for call to .permission_handoff\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "required for the satisfaction of .CtxAdmitsPermission<Tag, Ctx>. \\[with Tag = <unnamed>::NeedsIo")

# ── The checks in the bodies of split and combine ─────────────────────
#
# The fit concepts of the token forms compare no row, so each check
# below is the only one that refuses its program.
foundation_neg_compile_test(neg_permission_split_child_row_undeclared
  "static assertion failed: mint_permission_split<L, R>: a tag named here declares no effect row"
  "has_permission_row_v<.anonymous.::RowlessRight>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_without_ctx_refuses_row
  "static assertion failed: mint_permission_split<L, R>\\(Permission<In>&&\\) without ExecCtx is only valid"
  "has_permission_row_v<.anonymous.::Whole>\\)")
foundation_neg_compile_test(neg_permission_split_witness_missing
  "static assertion failed: can_split_into<In, L, R> is true but the accompanying has_split_authoring_witness"
  "has_split_authoring_witness_v<.anonymous.::Whole, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_duplicate_child
  "static assertion failed: mint_permission_split<L, R> requires L and R to be DISTINCT region tags"
  "all_distinct_tags_v<.anonymous.::Half, .anonymous.::Half>. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_child_row_undeclared
  "static assertion failed: mint_permission_combine<In>: a tag named here declares no effect row"
  "has_permission_row_v<.anonymous.::RowlessWhole>. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_without_ctx_refuses_row
  "static assertion failed: mint_permission_combine<In>\\(Permission<L>&&, Permission<R>&&\\) without ExecCtx is only valid"
  "mint_permission_combine\\(Args&& \\.\\.\\.\\) \\[with In = .anonymous.::Whole")
# The manifest check and the witness check each refuse a forger who
# wrote only the other trait.
foundation_neg_compile_test(neg_permission_combine_undeclared_parent
  "static assertion failed: mint_permission_combine<In>\\(Permission<L>&&, Permission<R>&&\\) requires can_split_into"
  "can_split_into_v<.anonymous.::Other, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_witness_missing
  "static assertion failed: has_split_authoring_witness<In, L, R> missing for combine"
  "has_split_authoring_witness_v<.anonymous.::Other, .anonymous.::Left, .anonymous.::Right>. evaluates to false")

# The same four checks in the body of split_n, and the five in the body
# of combine_n.
foundation_neg_compile_test(neg_permission_split_n_child_row_undeclared
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.>: a tag named here declares no effect row"
  "has_permission_row_v<.anonymous.::RowlessRight>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_without_ctx_refuses_row
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.>\\(Permission<In>&&\\) without ExecCtx is only valid"
  "has_permission_row_v<.anonymous.::Whole>\\)")
foundation_neg_compile_test(neg_permission_split_n_undeclared_pack
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.>\\(Permission<In>&&\\) requires can_split_into_pack"
  "can_split_into_pack_v<.anonymous.::Whole, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_witness_missing
  "static assertion failed: has_split_pack_authoring_witness<In, Children\\.\\.\\.> missing"
  "has_split_pack_authoring_witness_v<.anonymous.::Whole, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_duplicate_child
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.> requires the child tags to be PAIRWISE DISTINCT"
  "all_distinct_tags_v<.anonymous.::Half, .anonymous.::Half>. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_parent_row_undeclared
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>: a tag named here declares no effect row"
  "combine_n_manifest<.anonymous.::RowlessWhole, std::tuple<.anonymous.::Left, .anonymous.::Right> >::rows_declared. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_without_ctx_refuses_row
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>\\(\\.\\.\\.\\) without ExecCtx is only valid"
  "combine_n_manifest<.anonymous.::Whole, std::tuple<.anonymous.::Left, .anonymous.::Right> >::rows_declared\\)")
foundation_neg_compile_test(neg_permission_combine_n_undeclared_parent
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>\\(Permission<Children>&&\\.\\.\\.\\) requires can_split_into_pack"
  "combine_n_manifest<.anonymous.::Other, std::tuple<.anonymous.::Left, .anonymous.::Right> >::declared. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_witness_missing
  "static assertion failed: has_split_pack_authoring_witness<Parent, Children\\.\\.\\.> missing for combine_n"
  "combine_n_manifest<.anonymous.::Other, std::tuple<.anonymous.::Left, .anonymous.::Right> >::witnessed. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_children_of_two_regions
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>\\(\\.\\.\\.\\): the children carry different brands"
  "perm_brands_agree_<foundation::permissions::Permission<.anonymous.::Left")
# A brand names a mint site, so two root mints through one site are two
# tokens of one tag that the brand agreement admits.  The distinctness
# check is what refuses them.
foundation_neg_compile_test(neg_permission_combine_n_duplicate_child
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.> requires the child tags to be PAIRWISE DISTINCT"
  "combine_n_manifest<.anonymous.::Whole, std::tuple<.anonymous.::Half, .anonymous.::Half> >::distinct. evaluates to false")

# ── The share, the guard and the pool ─────────────────────────────────

# A share proof carries a brand, and a brand with a data member is
# refused by the emptiness check in the class body.
foundation_neg_compile_test(neg_shared_permission_brand_not_empty
  "static assertion failed: SharedPermission<Tag, Brand>: Brand must be an empty class type"
  "is_empty_v<B> \\[with B = <unnamed>::StatefulBrand\\]. evaluated to .false.")
# The token form of the share compares no row in its fit concept, so the
# two checks in its body are the only ones that refuse a row.
foundation_neg_compile_test(neg_permission_share_row_undeclared
  "static assertion failed: mint_permission_share: the tag declares no effect row"
  "has_permission_row_v<.anonymous.::RowlessRegion>. evaluates to false")
foundation_neg_compile_test(neg_permission_share_without_ctx_refuses_row
  "static assertion failed: mint_permission_share\\(Permission<Tag>&&\\) without ExecCtx is only valid"
  "permission_row_empty_v<.anonymous.::NeedsIo>\\)\\). evaluates to false")
# A token minted from a guard that dies at the end of the statement
# proves a share that is gone.  Only the deleted const&& twin refuses it.
foundation_neg_compile_test(neg_share_token_from_temporary_guard
  "use of deleted function .constexpr foundation::permissions::SharedPermission<Tag, Brand> foundation::permissions::SharedPermissionGuard<Tag, Brand>::token\\(\\) const &&"
  "a share proof minted from a temporary guard outlives the share")
# The pool's context check refuses a lend and an upgrade under a
# context that does not admit the tag's row, and the checks in their
# bodies refuse an effectful tag when no context is passed.
foundation_neg_compile_test(neg_share_pool_lend_ctx_refuses_row
  "no matching function for call to .foundation::permissions::SharedPermissionPool<.anonymous.::NeedsIo, .*>::lend\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "required for the satisfaction of .PoolCtx<Tag, Ctx>. \\[with Tag = <unnamed>::NeedsIo")
foundation_neg_compile_test(neg_share_pool_upgrade_ctx_refuses_row
  "no matching function for call to .foundation::permissions::SharedPermissionPool<.anonymous.::NeedsIo, .*>::try_upgrade\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "required for the satisfaction of .PoolCtx<Tag, Ctx>. \\[with Tag = <unnamed>::NeedsIo")
foundation_neg_compile_test(neg_share_pool_lend_without_ctx_refuses_row
  "static assertion failed: SharedPermissionPool<Tag>::lend\\(\\) without ExecCtx is only valid"
  "permission_row_empty_v<.anonymous.::NeedsIo>\\)\\). evaluates to false")
foundation_neg_compile_test(neg_share_pool_upgrade_without_ctx_refuses_row
  "static assertion failed: SharedPermissionPool<Tag>::try_upgrade\\(\\) without ExecCtx is only valid"
  "permission_row_empty_v<.anonymous.::NeedsIo>\\)\\). evaluates to false")
# with_shared_read lends from an lvalue pool only, in its value form and
# in its void form.
foundation_neg_compile_test(neg_shared_read_of_a_temporary_pool
  "no matching function for call to .with_shared_read\\("
  "required for the satisfaction of .WithSharedReadArgs<Args \\.\\.\\.>. \\[with Args = \\{foundation::permissions::SharedPermissionPool<<unnamed>::Region")
foundation_neg_compile_test(neg_shared_read_of_a_temporary_pool_void_body
  "no matching function for call to .with_shared_read\\("
  "required for the satisfaction of .WithSharedReadArgs<Args \\.\\.\\.>. \\[with Args = \\{foundation::permissions::SharedPermissionPool<<unnamed>::Region")

# A pool that still parks its exclusive refuses a second deposit.  The
# attack runs in a child process, and the test passes only on the
# violated condition.
foundation_test(test_share_pool_double_deposit_aborts)

# ── The row-mismatch message builder ──────────────────────────────────
#
# The static_assert message a mint hands the compiler when a row is not
# admitted: the site, the caller's row, the row the callee requires and
# the offending atoms, built at compile time.  The first case reads the
# message from a running program.
foundation_test(test_row_mismatch)

# A type that is not a diagnostic tag in the tag slot reaches the routed
# static_assert, whose wording is the header's.  The second regex is the
# instantiation as the compiler spells it, with the boolean the primary
# template dispatched on, which the fixture source does not contain.
foundation_neg_compile_test(neg_row_mismatch_tag_not_a_tag
  "static assertion failed: foundation::diag \\[RowMismatchTag_NonTag\\]"
  "row_message_check<int, some_function, int, float, double, false>"
  "is_diagnostic_class_v<int>. evaluates to false")

# mint_diagnostic admits a class that derives from tag_base and is not
# tag_base itself.  A class without the lineage and the root tag_base are
# the two ways to miss.  The second regex names the refused constraint.
foundation_neg_compile_test(neg_diag_mint_diagnostic_refuses_a_class_without_the_tag_base
  "no matching function for call to .mint_diagnostic<LooksLikeADiagnosticTag>\\(\\)."
  "the expression .is_diagnostic_class_v<Tag> \\[with Tag = LooksLikeADiagnosticTag\\]. evaluated to .false.")
foundation_neg_compile_test(neg_diag_mint_diagnostic_refuses_the_tag_base_itself
  "no matching function for call to .mint_diagnostic<foundation::diag::tag_base>\\(\\)."
  "the expression .is_diagnostic_class_v<Tag> \\[with Tag = foundation::diag::tag_base\\]. evaluated to .false.")

# A tag accessor over a plain type, and category_of_v over a tag that the
# closed Catalog does not hold, each reach a static_assert in the header.
# The second regex names the instantiation the compiler refused.
foundation_neg_compile_test(neg_diag_accessor_refuses_a_non_tag
  "static assertion failed: foundation::diag \\[DiagnosticAccessor_NonTag\\]"
  "required from .constexpr const std::string_view foundation::diag::diagnostic_name_v<int>.")
foundation_neg_compile_test(neg_diag_category_of_refuses_an_unregistered_tag
  "static assertion failed: category_of_v<Tag>: Tag is not registered in the foundation Catalog"
  "In instantiation of .struct foundation::diag::detail::category_of_impl<\\{anonymous\\}::user_local_tag>.")

# ── The row hash, as one fold ─────────────────────────────────────────
#
# The half of the cache key that separates one effect regime from
# another.  One specialisation over Graded covers each wrapper, because
# a wrapper in this tree is a spelling over Graded.
#
# Almost everything that the header owes a consumer is a constant, so
# the test is mostly static assertions.  Two cases are not constants,
# and a header cannot check them on its own: the folded values reach a
# running program.
#
# The wire-format literals in that file are the published on-the-wire
# contract.  The test fails when the fold moves a row that a peer
# already holds in its cache.
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
# A producer claim that one shared object wins and a second shared object
# destroys, as the PyTorch vessel does with a Vigil.  Under -fvisibility=hidden
# each shared object holds its own record of a brand, so the destructor must
# leave the record that the win entered.
foreach(_claim_library producer_claim_winner producer_claim_owner)
  add_library(test_${_claim_library} SHARED ${_claim_library}.cpp)
  target_link_libraries(test_${_claim_library} PRIVATE foundation)
endforeach()
foundation_test(test_producer_claim_across_libraries)
target_link_libraries(test_producer_claim_across_libraries PRIVATE
  test_producer_claim_winner test_producer_claim_owner)
foundation_neg_compile_test(neg_host_owner_defined_again
  "redefinition of .struct foundation::effects::host::ForegroundOwner."
  "neg_host_owner_defined_again.cpp:9")
foundation_neg_compile_test(neg_host_background_owner_defined_again
  "redefinition of .struct foundation::effects::host::BackgroundOwner."
  "neg_host_background_owner_defined_again.cpp:9")
foundation_neg_compile_test(neg_host_owner_key_private
  "is private within this context"
  "foundation::effects::host::ForegroundOwner::key\\(\\)")
# The init owner is the one production door of the init context.  No
# second definition of it compiles, no translation unit builds the init
# key for a door of its own, and no free function opens the door.
foundation_neg_compile_test(neg_host_init_owner_defined_again
  "redefinition of .struct foundation::effects::host::InitOwner."
  "neg_host_init_owner_defined_again.cpp:9")
foundation_neg_compile_test(neg_init_door_forged_outside_owner
  "is private within this context"
  "foundation::effects::detail::ctx_mint::init_key::init_key\\(\\)")
foundation_neg_compile_test(neg_init_door_without_owner
  "is not a member of .foundation::effects."
  ".mint_init_context.")
# The background owner is the one production door of the background
# context, on the same terms: no translation unit builds the background
# key for a door of its own, and no free function opens the door.
foundation_neg_compile_test(neg_bg_door_forged_outside_owner
  "is private within this context"
  "foundation::effects::detail::ctx_mint::bg_key::bg_key\\(\\)")
foundation_neg_compile_test(neg_bg_door_without_owner
  "is not a member of .foundation::effects."
  ".mint_background_context.")
foundation_neg_compile_test(neg_producer_claim_default_built
  "is private within this context"
  "ProducerClaim<Brand>::ProducerClaim\\(\\) .with Brand = .anonymous.::Brand")
foundation_neg_compile_test(neg_producer_claim_outside_brand
  "is private within this context"
  "ProducerClaim<Brand>::ProducerClaim\\(\\) .with Brand = .anonymous.::Brand"
  "neg_producer_claim_outside_brand.cpp:10")
# A claim keys its brand in the process-wide registry by the stable id of
# the brand, so a brand with no stable identity is refused where it mints.
foundation_neg_compile_test(neg_producer_claim_brand_internal_linkage
  "a stable id refuses the type .anonymous.::Hidden, because its part .anonymous.::Hidden has internal linkage"
  "ProducerClaim<Brand>::brand_key_\\(\\) .with Brand = .anonymous.::Hidden")
foundation_neg_compile_test(neg_producer_claim_brand_in_closure
  "because its part main\\(\\)::<lambda\\(\\)> has no declared name"
  "ProducerClaim<Brand>::brand_key_\\(\\) .with Brand = main\\(\\)::<lambda\\(\\)>::InClosure")
foundation_neg_compile_test(neg_branded_ctx_other_brand
  "invalid initialization of reference"
  "ctx_cap::BrandedFg<other_brand_probe::Stranger>")
foundation_neg_compile_test(neg_branded_ctx_to_unbranded_gate
  "invalid initialization of reference"
  "ctx_cap::BrandedFg<unbranded_gate_probe::SelfDeclared>")
foundation_neg_compile_test(neg_branded_foreground_ctx_bit_cast
  "no matching function for call to .bit_cast<foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<"
  "is_trivially_copyable_v<_To> .with _To = foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<")
foundation_neg_compile_test(neg_branded_foreground_ctx_start_as_array
  "no matching function for call to .start_as_array<foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<")
foundation_neg_compile_test(neg_branded_ctx_lambda_capture_copy
  "use of deleted function .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Brand>, foundation::effects::Row<> >::ExecCtx.const foundation::effects::ExecCtx<"
  "use of deleted function .foundation::effects::ctx_cap::BrandedFg<Brand>::BrandedFg.const foundation::effects::ctx_cap::BrandedFg<Brand>&. .with Brand = .anonymous.::Brand..: a copy of a branded source can go to a thread that holds no claim")
foundation_neg_compile_test(neg_branded_ctx_jthread_argument
  "no matching function for call to .std::tuple<void .{4}const foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Brand>, foundation::effects::Row<> >&. noexcept, foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Brand>, foundation::effects::Row<> > >::tuple"
  "neg_branded_ctx_jthread_argument.cpp:25:[0-9]+: +required from here")

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

# The same Swiss table test under the ignore semantic, where no contract
# clause checks anything.  The capacity check and the size check are plain
# code, so each still aborts.
foundation_test(test_swiss_table_buffer_ignore test_swiss_table_buffer.cpp)
target_compile_options(test_swiss_table_buffer_ignore PRIVATE ${CRUCIBLE_CONTRACT_IGNORE_OPTIONS})

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

foundation_neg_compile_test(neg_thread_local_ref_needs_a_default
  "template constraint failure for .template<class Tag, class T>  requires  is_default_constructible_v<T> class foundation::ThreadLocalRef."
  "the expression .is_default_constructible_v<T> \\[with T = <unnamed>::NeedsSeed\\]. evaluated to .false.")
foundation_neg_compile_test(neg_thread_local_ref_store_of_another_type
  "no matching function for call to .foundation::ThreadLocalRef<\\{anonymous\\}::CounterTag, \\{anonymous\\}::Counter>::store\\(\\{anonymous\\}::Label\\) const."
  "the expression .is_assignable_v<T&, U&&> \\[with T = <unnamed>::Counter; U = <unnamed>::Label\\]. evaluated to .false.")

# ── Hard errors in a body ─────────────────────────────────────────────
#
# Neither template has a constraint.  The refusal is an error in the body
# of the template, and no concept can read it.  For that reason each
# refusal has a fixture.
foundation_neg_compile_test(neg_signature_traits_of_a_non_function
  "In instantiation of .struct foundation::reflect::signature_traits<42>."
  "reflection does not represent a function or function type")
foundation_neg_compile_test(neg_simd_iota_of_a_scalar
  "In instantiation of .constexpr V foundation::simd::iota_v\\(\\) \\[with V = int\\]."
  ".int. is not a class, struct, or union type")
