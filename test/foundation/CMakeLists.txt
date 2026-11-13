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
  # Assertions stay armed in every configuration; see crucible_test.
  target_compile_options(${name} PRIVATE -UNDEBUG)
  if(CRUCIBLE_SAN_COMPILE_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_SAN_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${CRUCIBLE_SAN_LINK_FLAGS})
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "foundation")
  if(CRUCIBLE_TSAN)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1")
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
# itself said (`loop fixture` reads the SARIF log of the compile, which
# holds no source line, so the source of a fixture cannot satisfy a regex).
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
    COMMAND python3 -S ${CMAKE_SOURCE_DIR}/utils/bin/loop fixture
            --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR}
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/neg/${name}.cpp
            ${name}
            ${_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "foundation_neg"
    ENVIRONMENT "CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1")
  set_property(GLOBAL APPEND PROPERTY CRUCIBLE_NEG_FIXTURE_TESTS ${name})
endfunction()

# The fixture that proves the harness fires: a concept the argument does
# not satisfy, gated on the reason and on the concept's name as the
# compiler spells it in its satisfaction note.
foundation_neg_compile_test(neg_constraint_failure_names_the_concept
  "constraints not satisfied"
  "satisfaction of .HalvesEvenly<T>.")

# The platform floor of foundation/Platform.h refuses a target that breaks
# one of its assumptions.  Each fixture includes the header, and the test
# gives the compiler the flags of the refused target in
# CRUCIBLE_NEG_EXTRA_FLAGS.  The flags do not go on the fixture target: the
# compile database then holds a unit of the host, which each guard that
# preprocesses the database can read.
#
# GCC lets -U remove a predefined macro.  The first two fixtures set the
# architecture macros of an unsupported architecture and of an Apple aarch64
# target.  The first regex names the #error of the header, and the second
# names its rule.  With these macros, a system header of an x86_64 host looks
# for the headers of a 32-bit target, and a fixture must fail only for its own
# reason.  So -Wfatal-errors stops each compile at the #error.
foundation_neg_compile_test(neg_platform_unsupported_architecture
  "Platform\\.h:[0-9]+:[0-9]+: error: #error"
  "foundation supports x86_64 and aarch64 only")
set_property(TEST neg_platform_unsupported_architecture APPEND PROPERTY ENVIRONMENT
  "CRUCIBLE_NEG_EXTRA_FLAGS=-U__x86_64__ -U__aarch64__ -Wfatal-errors")
foundation_neg_compile_test(neg_platform_apple_aarch64
  "Platform\\.h:[0-9]+:[0-9]+: error: #error"
  "Apple aarch64 cores have 128-byte cache lines")
set_property(TEST neg_platform_apple_aarch64 APPEND PROPERTY ENVIRONMENT
  "CRUCIBLE_NEG_EXTRA_FLAGS=-U__x86_64__ -D__aarch64__ -D__APPLE__ -Wfatal-errors")
# The floor also reads the interference sizes that GCC gives the target, and
# a --param option sets each of them.  The first regex names the static
# assertion of the header, and the second names its rule.  The destructive
# check applies only on x86_64, because GCC gives most aarch64 targets a
# destructive size of 256 that names no cache line.  So the fixture of that
# check exists only on x86_64.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  foundation_neg_compile_test(neg_platform_destructive_interference_size
    "Platform\\.h:[0-9]+:[0-9]+: error: static assertion failed"
    "the tree separates two shared atomics by 64 bytes")
  set_property(TEST neg_platform_destructive_interference_size APPEND PROPERTY ENVIRONMENT
    "CRUCIBLE_NEG_EXTRA_FLAGS=--param=destructive-interference-size=128")
endif()
foundation_neg_compile_test(neg_platform_longer_cache_line
  "Platform\\.h:[0-9]+:[0-9]+: error: static assertion failed"
  "the tree assumes 64-byte cache lines, and GCC gives a longer line")
set_property(TEST neg_platform_longer_cache_line APPEND PROPERTY ENVIRONMENT
  "CRUCIBLE_NEG_EXTRA_FLAGS=--param=l1-cache-line-size=128 --param=constructive-interference-size=128")

# Two lattices that pass every law comparing elements through leq alone.
# The first has a leq that runs against its join, the second has bottom()
# above top().  The laws that tie the order to join, meet and the bounds
# refuse both, at a verify helper and at Graded.  The second regex of each
# names the law function and the refused lattice.
foundation_neg_compile_test(neg_lattice_order_against_join
  "no matching function for call to .verify_lattice_axioms_at<Inverted>"
  "the expression .\\(laws_hold<L>\\)\\(\\) \\[with L = foundation::algebra::detail::InvertedOrder\\]. evaluated to .false.")
foundation_neg_compile_test(neg_lattice_bottom_above_top
  "template constraint failure for .template<foundation::algebra::ModalityKind M, class L, class T> +requires +LatticeGradesValue<L, T> class foundation::algebra::Graded."
  "the expression .\\(laws_hold<L>\\)\\(\\) \\[with L = foundation::algebra::detail::ExchangedBounds\\]. evaluated to .false.")
# Two operation pairs with the signature of a semiring and not its laws:
# subtraction for add, and the maximum for both add and mul.  The laws at
# zero() and one() refuse both at the verify helper.
foundation_neg_compile_test(neg_semiring_subtraction_for_add
  "no matching function for call to .verify_semiring_axioms_at<Subtraction>"
  "the expression .\\(semiring_laws_hold<S>\\)\\(\\) \\[with S = foundation::algebra::detail::SubtractionSemiring\\]. evaluated to .false.")
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
  "Graded<foundation::algebra::detail::self_test_authority>\\(foundation::algebra::detail::self_test_authority::key\\(\\), .*, 9\\)")
foundation_neg_compile_test(neg_graded_element_grade_outside_the_order
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "TrivialChainLattice, unsigned char>::Graded\\(9\\)")

# Where the grade is the value, the grade that the keyed constructor
# takes is a witness.  The same guard refuses a witness inside the order
# that names another grade.
foundation_neg_compile_test(neg_graded_witness_names_another_grade
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "MonotoneLattice<int(, std::less<int> )?>, int>::Graded<\\{anonymous\\}::fixture_authority>\\(\\{anonymous\\}::fixture_authority::key\\(\\), 5, 10\\)")

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
  "the expression .\\(claim_subject_v<L>\\) == foundation::algebra::ClaimSubject::slot \\[with L = foundation::algebra::detail::TrivialBoolLattice\\]. evaluated to .false.")
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

# The check file of Resources.h asks each class template in namespace
# resource for a top-level alias of its name.  The fixture plants a class
# template with no alias and includes the check file after it.
foundation_neg_compile_test(neg_resource_tag_template_without_an_alias
  "static assertion failed: A tag template in namespace resource has no top-level alias of its name in foundation::effects"
  "layer/checks/foundation/effects/Resources\\.cpp:[0-9]+:[0-9]+: error: static assertion failed")

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
  "no matching function for call to .mint_from_image<foundation::algebra::lattices::EpochLattice>\\("
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::IO>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
foundation_neg_compile_test(neg_strong_counter_image_in_test_scope_without_io
  "no matching function for call to .mint_from_image<foundation::algebra::lattices::GenerationLattice>\\("
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
  "no matching function for call to .mint_from_image<HB>\\(.*foundation::algebra::lattices::HappensBeforeLattice<2>::image_type\\)"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::IO>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test> >.")
# A clock over a tag with no source path has no image door, even under IO.
foundation_neg_compile_test(neg_happens_before_image_without_source_path
  "no matching function for call to .mint_from_image<HB>\\(.*foundation::algebra::lattices::HappensBeforeLattice<2, \\{anonymous\\}::HiddenClock>::image_type&\\)"
  "required for the satisfaction of .ImageLattice<Lattice>. .with Lattice = foundation::algebra::lattices::HappensBeforeLattice<2, <unnamed>::HiddenClock>.")
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
  "in .constexpr. expansion of .foundation::algebra::lattices::HappensBeforeLattice<2>::successor_at\\("
  "foundation/algebra/lattices/HappensBefore\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
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
  "the expression .is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>; R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_exec_ctx_cap_from_init_without_block
  "no matching function for call to .foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >::cap\\(\\) const."
  "the expression .is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>; R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>\\]. evaluated to .false.")
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
  "each_body_takes_its_child\\(.*\\^\\^ std::tuple<Callables \\.\\.\\.>\\) \\[with .*evaluated to .false.")
foundation_neg_compile_test(neg_permission_fork_body_keeps_its_view
  "use of deleted function"
  "a WriteView lives in the frame of its body. A copy could outlive the join")
foundation_neg_compile_test(neg_permission_fork_body_of_another_region
  "constraints not satisfied"
  "each_body_takes_its_child\\(.*\\^\\^ std::tuple<Callables \\.\\.\\.>\\) \\[with .*evaluated to .false.")

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
  "the expression .is_exec_ctx\\(.* \\[with T = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Bg, foundation::effects::Effect::IO> >\\]. evaluated to .false.")
foundation_neg_compile_test(neg_exec_ctx_specialization_outside_permitted_row
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::Init> >\\]"
  "the expression .is_exec_ctx\\(.* \\[with T = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::Init> >\\]. evaluated to .false.")
# IsExecCtx reads a function that is not a template, so an explicit
# specialization of that function that makes each type a context matches
# no template.
foundation_neg_compile_test(neg_exec_ctx_specialized_trait
  "template-id .is_exec_ctx<>. for .consteval bool foundation::effects::is_exec_ctx\\(std::meta::info\\). does not match any template declaration"
  "neg_exec_ctx_specialized_trait\\.cpp:12:[0-9]+: error:")

# Each answer about a row, an atom or a context is a function at namespace
# scope that is not a template.  Each fixture below writes an explicit
# specialization of one of them that says yes for every argument, and no
# template matches it.
foundation_neg_compile_test(neg_effects_row_contains_specialized
  "template-id .row_contains<>. for .consteval bool foundation::effects::row_contains\\(std::meta::info, Effect\\). does not match any template declaration"
  "neg_effects_row_contains_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_subrow_specialized
  "template-id .is_subrow<>. for .consteval bool foundation::effects::is_subrow\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_effects_is_subrow_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_effect_row_specialized
  "template-id .is_effect_row<>. for .consteval bool foundation::effects::is_effect_row\\(std::meta::info\\). does not match any template declaration"
  "neg_effects_is_effect_row_specialized\\.cpp:11:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_row_size_specialized
  "template-id .row_size<>. for .consteval std::size_t foundation::effects::row_size\\(std::meta::info\\). does not match any template declaration"
  "neg_effects_row_size_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_concurrent_row_specialized
  "template-id .is_concurrent_row<>. for .consteval bool foundation::effects::is_concurrent_row\\(std::meta::info\\). does not match any template declaration"
  "neg_effects_is_concurrent_row_specialized\\.cpp:11:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_effect_atom_specialized
  "template-id .is_effect_atom<>. for .consteval bool foundation::effects::is_effect_atom\\(Effect\\). does not match any template declaration"
  "neg_effects_is_effect_atom_specialized\\.cpp:10:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_rostered_context_specialized
  "template-id .is_rostered_context<>. for .consteval bool foundation::effects::is_rostered_context\\(std::meta::info\\). does not match any template declaration"
  "neg_effects_is_rostered_context_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_resource_kind_atom_specialized
  "template-id .is_resource_kind_atom<>. for .consteval bool foundation::effects::is_resource_kind_atom\\(ResourceKind\\). does not match any template declaration"
  "neg_effects_is_resource_kind_atom_specialized\\.cpp:11:[0-9]+: error:")
foundation_neg_compile_test(neg_effects_is_branded_foreground_specialized
  "template-id .is_branded_foreground<>. for .consteval bool foundation::effects::is_branded_foreground\\(std::meta::info\\). does not match any template declaration"
  "neg_effects_is_branded_foreground_specialized\\.cpp:12:[0-9]+: error:")

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
  "row_contains\\(.*\\(\\^\\^ typename Ctx::row_type, Cap\\) \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >; Cap = foundation::effects::Effect::IO\\]")
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

# The gate reads the annotation authority on a complete class.  A unit
# that only declares Permission cannot answer, so the gate stops the build.
foundation_neg_compile_test(neg_computation_extract_declared_specialization
  "uncaught exception of type .std::meta::exception."
  "the payload names a specialization with no definition"
  "Phantom<foundation::permissions::Permission<")

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
  "the expression .row_size\\(.*\\(\\^\\^ R\\) == 0 \\[with R = foundation::effects::Row<foundation::effects::Effect::IO>\\]. evaluated to .false.")

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
# same thread aborts at once.  The parent sleeps between two waits for the
# child through the bounded sleeper of fixy/os/Time.h, so the test links fixy.
foundation_test(test_contract_handler)
target_link_libraries(test_contract_handler PRIVATE fixy)

# CRUCIBLE_PRE under the ignore semantic must compile to the same machine
# code as a bare [[assume]].  This target is built with -O3 and
# CRUCIBLE_CONTRACT_IGNORE_OPTIONS instead of the preset's debug flags,
# and the sanitizer flags are left off because they change code
# generation.  Each function goes in its own section so the __start_ and
# __stop_ symbols bound one probe each.  See test_pre_post_cost.cpp.
add_executable(test_pre_post_cost test_pre_post_cost.cpp)
target_link_libraries(test_pre_post_cost PRIVATE foundation)
target_compile_options(test_pre_post_cost PRIVATE -O3 -ffunction-sections)
crucible_contract_ignore_target(test_pre_post_cost)
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

# The contract rule of utils/tools/quarantine/contract.cpp rejects each
# P2900 contract specifier, and each build loads a plugin that applies it.
# The fixture driver keeps the plugin in each compile of a fixture, and the
# error of this rule is no finding, so the regexes read it.
# One fixture for each form: a precondition on a function, a postcondition on
# a function, and a precondition on a member of a class template.  The first
# regex gives the place and the specifier, the second the macro that replaces
# it, and the last the reason.
foundation_neg_compile_test(neg_contract_specifier_pre
  "neg_contract_specifier_pre\\.cpp:9:[0-9]+: error: the .pre. contract specifier is not permitted in this tree"
  "write .CRUCIBLE_PRE\\(condition\\). of .foundation/contracts/Pre\\.h. as the first statement"
  "does not keep the contract specifier of a template in a header unit or in a precompiled header")
foundation_neg_compile_test(neg_contract_specifier_post
  "neg_contract_specifier_post\\.cpp:8:[0-9]+: error: the .post. contract specifier is not permitted in this tree"
  "write .CRUCIBLE_POST\\(result, condition\\). of .foundation/contracts/Post\\.h. before each return statement"
  "does not keep the contract specifier of a template in a header unit or in a precompiled header")
foundation_neg_compile_test(neg_contract_specifier_template_member
  "neg_contract_specifier_template_member\\.cpp:12:[0-9]+: error: the .pre. contract specifier is not permitted"
  "write .CRUCIBLE_PRE\\(condition\\). of .foundation/contracts/Pre\\.h."
  "does not keep the contract specifier of a template in a header unit or in a precompiled header")

# The rule specialization-home of utils/tools/quarantine/specialization.cpp
# makes each of its findings a compile error in each build.  Its error does
# not start with "quarantine: ", so the regexes read it, as they read the
# error of the contract rule.  One fixture for each kind: a specialization
# outside the home of its template, and a variable template that a file
# declares with extern before its header.  The first regex gives the place
# and the error, and the second the forged template.  The variable template
# of the last fixture is a template of the base, so its declaration is also a
# declaration in a namespace of the base outside the base, which the rule
# base-namespace refuses.  Each of its two regexes then gives one of the two
# errors, with the place and the template.
# Two fixtures hold the orphan rule of an extension point: a niche of a base
# type, which forges a null Ref through the niche key, and a niche of each
# pointer type, which no file owns.
foundation_neg_compile_test(neg_specialization_home_explicit
  "neg_specialization_home_explicit\\.cpp:10:[0-9]+: error: this file specializes the template .* outside its home .include/foundation/effects/Resources\\.h."
  "the template .foundation::effects::resource::SmBudget. outside its home")
foundation_neg_compile_test(neg_specialization_home_forward_declaration
  "neg_specialization_home_forward_declaration\\.cpp:12:[0-9]+: error: this file declares the variable template .foundation::algebra::is_graded_v. with .extern. before its definition"
  "neg_specialization_home_forward_declaration\\.cpp:12:[0-9]+: error: this file declares .foundation::algebra::is_graded_v. in a namespace of the base, outside the base")
foundation_neg_compile_test(neg_specialization_home_orphan_ref
  "neg_specialization_home_orphan_ref\\.cpp:19:[0-9]+: error: this file specializes the extension point .foundation::core::niche. for a type that a different file defines"
  "the extension point .foundation::core::niche. for a type")
# An explicit specialization of a function template outside its home is a
# site of the rule too.  A friend function template of a class of the
# language, a member template of a door and a befriended detail template give
# each specialization the private members of the class.  Each fixture reads
# only the texts of the rule, so a change of the class does not change it.
foundation_neg_compile_test(neg_specialization_home_member_ref_door
  "neg_specialization_home_member_ref_door\\.cpp:15:[0-9]+: error: this file specializes the template .foundation::core::detail::RefDoor::lend. outside its home .include/foundation/core/Ref\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_friend_box
  "neg_specialization_home_friend_box\\.cpp:14:[0-9]+: error: this file specializes the template .foundation::core::mint_box. outside its home .include/foundation/core/Ref\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_friend_address_of
  "neg_specialization_home_friend_address_of\\.cpp:15:[0-9]+: error: this file specializes the template .foundation::core::address_of. outside its home .include/foundation/core/Ref\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_friend_heap_array
  "neg_specialization_home_friend_heap_array\\.cpp:15:[0-9]+: error: this file specializes the template .foundation::core::mint_heap_array. outside its home .include/foundation/core/Array\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_detail_view_over
  "neg_specialization_home_detail_view_over\\.cpp:13:[0-9]+: error: this file specializes the template .foundation::core::detail::view_over_. outside its home .include/foundation/core/Region\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_detail_text_bytes
  "neg_specialization_home_detail_text_bytes\\.cpp:11:[0-9]+: error: this file specializes the template .foundation::core::detail::text_bytes_. outside its home .include/foundation/core/Text\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_detail_fmt_text
  "neg_specialization_home_detail_fmt_text\\.cpp:11:[0-9]+: error: this file specializes the template .foundation::core::detail::fmt_text_. outside its home .include/foundation/core/Report\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_detail_alternative_of
  "neg_specialization_home_detail_alternative_of\\.cpp:15:[0-9]+: error: this file specializes the template .foundation::core::detail::alternative_of_. outside its home .include/foundation/core/Choice\\.h."
  "an explicit specialization gives the function a different body")
foundation_neg_compile_test(neg_specialization_home_orphan_partial
  "neg_specialization_home_orphan_partial\\.cpp:9:[0-9]+: error: this file specializes the extension point .foundation::core::niche. for a type that a different file defines"
  "specialize the template in the file that defines each of its arguments")

# The rule base-namespace of utils/tools/quarantine/namespace.cpp makes each
# declaration in a namespace of the base outside the base a compile error in
# each build.  Its error does not start with "quarantine: ", so the regexes
# read it.  The fixtures hold a new template of the base, a new overload
# before the header, the body of a member that the base only declares, and a
# using-directive of the global namespace before a header of the base.  The
# first regex gives the place and the error, and the second the action.
foundation_neg_compile_test(neg_base_namespace_planted_template
  "neg_base_namespace_planted_template\\.cpp:10:[0-9]+: error: this file declares .foundation::forged::planted_trait. in a namespace of the base, outside the base"
  "declare the entity in a namespace of this file")
foundation_neg_compile_test(neg_base_namespace_overload_before_header
  "neg_base_namespace_overload_before_header\\.cpp:9:[0-9]+: error: this file declares .foundation::core::unreachable. in a namespace of the base, outside the base"
  "a new overload changes which function a call of the base reaches")
foundation_neg_compile_test(neg_base_namespace_text_cursor_body
  "neg_base_namespace_text_cursor_body\\.cpp:10:[0-9]+: error: this file declares .foundation::core::TextView::begin. in a namespace of the base, outside the base"
  "a body of a function that the base only declares replaces the code of the base")
foundation_neg_compile_test(neg_base_namespace_global_directive
  "neg_base_namespace_global_directive\\.cpp:11:[0-9]+: error: this using-directive of the namespace .planted. can change a lookup of the base, outside the base"
  "at the global scope before a file of the base")

# test/CMakeLists.txt registers test_reflect for test/test_reflect.cpp,
# and a CMake target name is global.  This target carries the name of
# its layer.
foundation_test(test_reflect_foundation test_reflect.cpp)

# The enumerator walk calls its callback with a value and a name, so a
# callback of another arity meets no call.  bits_to_string admits a null
# buffer only with a capacity of 0, and its precondition stops a constant
# evaluation that breaks the rule.  The second regex of that fixture names
# the call and its arguments, and the third names the header that holds the
# precondition.
foundation_neg_compile_test(neg_reflect_for_each_enumerator_zero_arg_callback
  "foundation/reflect/EnumName\\.h:[0-9]+:[0-9]+: error: no match for call to .\\(main\\(\\)::<lambda\\(\\)>\\) \\(const \\{anonymous\\}::Signal&, const std::basic_string_view<char>&\\)."
  "candidate expects 0 arguments, 2 provided")
foundation_neg_compile_test(neg_reflect_bits_to_string_null_buffer_with_capacity
  "__builtin_trap.*is not a constant expression"
  "foundation::reflect::bits_to_string<\\{anonymous\\}::Signal>\\(.*, 0, 100\\)"
  "foundation/reflect/Enumerate\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

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
# The evaluation note shows admits answering false for the pair.
foundation_neg_compile_test(neg_fail_closed_foreign_edge_type
  "constraints not satisfied"
  "the expression .admits\\(.*\\[with Ns = \\^\\^ namespace .*ingest.*From = .*Raw; To = .*Checked\\]. evaluated to .false.")

# admits is a function at namespace scope that is not a template, so an
# explicit specialization that admits a pair of the caller matches no
# template.
foundation_neg_compile_test(neg_fail_closed_admits_specialized
  "template-id .admits<>. for .consteval bool foundation::fail_closed::admits\\(std::meta::info, std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_fail_closed_admits_specialized\\.cpp:12:[0-9]+: error:")

# A derived permission tag has the row of its parent.  An edge in
# permission_rows, or a permission_row member, that gives a shard a row of
# its own is refused, so a shard of an IO region cannot claim the empty row.
foundation_neg_compile_test(neg_permission_row_edge_for_a_derived_tag
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .permission_row: a derived tag declares a row of its own"
  "using foundation::permissions::permission_row_t = .* \\[with Tag = user_code::shard\\]")
foundation_neg_compile_test(neg_permission_row_member_for_a_derived_tag
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .permission_row: a derived tag declares a row of its own"
  "using foundation::permissions::permission_row_t = .* \\[with Tag = user_code::shard\\]")

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
  "call to non-.constexpr. function .void foundation::fail_closed::detail::admits_reads_the_reflection_of_a_namespace\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::admits\\(\\^\\^\\{anonymous\\}::NotARelation, \\^\\^\\{anonymous\\}::Raw, \\^\\^\\{anonymous\\}::Checked\\).")
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
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .fail_closed::has_edge_from: the first argument must be the reflection of a namespace"
  "neg_fail_closed_has_edge_from_of_a_class\\.cpp:24:[0-9]+: error: uncaught exception")
foundation_neg_compile_test(neg_fail_closed_has_edge_to_of_a_class
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .fail_closed::has_edge_to: the first argument must be the reflection of a namespace"
  "neg_fail_closed_has_edge_to_of_a_class\\.cpp:24:[0-9]+: error: uncaught exception")
foundation_neg_compile_test(neg_fail_closed_edge_count_from_of_a_class
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .fail_closed::edge_count_from: the first argument must be the reflection of a namespace"
  "neg_fail_closed_edge_count_from_of_a_class\\.cpp:24:[0-9]+: error: uncaught exception")
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
  "in .constexpr. expansion of .foundation::fail_closed::has_edge_from\\(\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::Raw\\).")
foundation_neg_compile_test(neg_fail_closed_has_edge_to_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::has_edge_to\\(\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::Checked\\).")
foundation_neg_compile_test(neg_fail_closed_edge_count_from_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::edge_count_from\\(\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::Raw\\).")
foundation_neg_compile_test(neg_fail_closed_every_class_in_has_edge_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::every_class_in_has_edge<\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::vacant")
foundation_neg_compile_test(neg_fail_closed_every_edge_is_admitted_after_the_seal
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::every_edge_is_admitted<\\^\\^\\{anonymous\\}::ingest>\\(\\).")

# A query reads one walk for each count of the members of a relation, so
# a member added after a read changes the count, and the next read walks
# again and finds the member outside the seal.
foundation_neg_compile_test(neg_fail_closed_edge_after_a_read
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::admits\\(\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::Checked, \\^\\^\\{anonymous\\}::Stored\\).")

# The walk of a relation is the value of a variable template, which a
# translation unit can specialize.  A specialization cannot build or copy
# the cells or the view of a walk, and a view of the cells of another walk
# names another namespace, which the read refuses.
foundation_neg_compile_test(neg_fail_closed_state_forged_by_copy
  "relation_state::relation_state\\(const foundation::fail_closed::detail::relation_state&\\). is private within this context"
  "neg_fail_closed_state_forged_by_copy\\.cpp:36:[0-9]+: error:")
foundation_neg_compile_test(neg_fail_closed_state_views_other_cells
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_relation_state_stands_for_another_reading\\(\\)."
  "in .constexpr. expansion of .foundation::fail_closed::admits\\(\\^\\^\\{anonymous\\}::ingest, \\^\\^\\{anonymous\\}::Checked, \\^\\^\\{anonymous\\}::Raw\\).")
foundation_neg_compile_test(neg_fail_closed_cells_forged_by_copy
  "relation_cells<Capacity>::relation_cells\\(const foundation::fail_closed::detail::relation_cells<Capacity>&\\) \\[with long unsigned int Capacity = 1\\]. is private within this context"
  "neg_fail_closed_cells_forged_by_copy\\.cpp:37:[0-9]+: error:")

# unique_target reads a relation as a function of its From, so it
# refuses a From with no edge and a From with two.
foundation_neg_compile_test(neg_fail_closed_unique_target_without_an_edge
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .fail_closed::unique_target: the relation declares no edge from the type"
  "using foundation::fail_closed::unique_target_t = .* \\[with std::meta::info Ns = \\^\\^\\{anonymous\\}::ingest. T = \\{anonymous\\}::Stored\\]")
foundation_neg_compile_test(neg_fail_closed_unique_target_of_two_edges
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .fail_closed::unique_target: the relation declares more than one edge from the type"
  "using foundation::fail_closed::unique_target_t = .* \\[with std::meta::info Ns = \\^\\^\\{anonymous\\}::ingest. T = \\{anonymous\\}::Raw\\]")

# Each answer of the permission layer that a mint reads is a function at
# namespace scope that is not a template, or an alias over one call, so no
# specialization matches it.
foundation_neg_compile_test(neg_permission_tags_are_distinct_specialized
  "template-id .tags_are_distinct<>. for .consteval bool foundation::permissions::tags_are_distinct\\(std::initializer_list<std::meta::info>\\). does not match any template declaration"
  "neg_permission_tags_are_distinct_specialized\\.cpp:13:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_has_row_specialized
  "template-id .has_permission_row<>. for .consteval bool foundation::permissions::has_permission_row\\(std::meta::info\\). does not match any template declaration"
  "neg_permission_has_row_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_row_empty_specialized
  "template-id .permission_row_empty<>. for .consteval bool foundation::permissions::permission_row_empty\\(std::meta::info\\). does not match any template declaration"
  "neg_permission_row_empty_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_mint_args_shaped_specialized
  "template-id .permission_mint_args_shaped<>. for .consteval bool foundation::permissions::detail::permission_mint_args_shaped\\(std::size_t, std::size_t, std::initializer_list<std::meta::info>\\). does not match any template declaration"
  "neg_permission_mint_args_shaped_specialized\\.cpp:14:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_brands_agree_specialized
  "template-id .perm_brands_agree<>. for .consteval bool foundation::permissions::detail::perm_brands_agree\\(std::initializer_list<std::meta::info>\\). does not match any template declaration"
  "neg_permission_brands_agree_specialized\\.cpp:13:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_ctx_admits_each_tag_of_specialized
  "template-id .ctx_admits_each_tag_of<>. for .consteval bool foundation::permissions::detail::ctx_admits_each_tag_of\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_permission_ctx_admits_each_tag_of_specialized\\.cpp:13:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_combine_n_facts_specialized
  "template-id .combine_n_facts<>. for .consteval foundation::permissions::detail::combine_n_manifest foundation::permissions::detail::combine_n_facts\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_permission_combine_n_facts_specialized\\.cpp:13:[0-9]+: error:")
foundation_neg_compile_test(neg_shared_read_shaped_specialized
  "template-id .with_shared_read_shaped<>. for .consteval bool foundation::permissions::detail::with_shared_read_shaped\\(std::initializer_list<std::meta::info>\\). does not match any template declaration"
  "neg_shared_read_shaped_specialized\\.cpp:13:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_fork_each_body_specialized
  "template-id .each_body_takes_its_child<>. for .consteval bool foundation::permissions::detail::each_body_takes_its_child\\(std::meta::info, std::meta::info, std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_permission_fork_each_body_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_perm_set_contains_specialized
  "template-id .perm_set_contains<>. for .consteval bool foundation::permissions::perm_set_contains\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_perm_set_contains_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_perm_set_subset_specialized
  "template-id .perm_set_subset<>. for .consteval bool foundation::permissions::perm_set_subset\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_perm_set_subset_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_perm_set_equal_specialized
  "template-id .perm_set_equal<>. for .consteval bool foundation::permissions::perm_set_equal\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_perm_set_equal_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_perm_set_union_alias_specialized
  "specialization of alias template .template<class PS1, class PS2> using foundation::permissions::perm_set_union_t = "
  "neg_perm_set_union_alias_specialized\\.cpp:15:[0-9]+: error:")
foundation_neg_compile_test(neg_fail_closed_has_edge_from_specialized
  "template-id .has_edge_from<>. for .consteval bool foundation::fail_closed::has_edge_from\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_fail_closed_has_edge_from_specialized\\.cpp:12:[0-9]+: error:")
foundation_neg_compile_test(neg_fail_closed_unique_target_specialized
  "template-id .unique_target<>. for .consteval std::meta::info foundation::fail_closed::unique_target\\(std::meta::info, std::meta::info\\). does not match any template declaration"
  "neg_fail_closed_unique_target_specialized\\.cpp:12:[0-9]+: error:")

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
# proves the refusal came from the union alias in PermSet.h.
foundation_neg_compile_test(neg_perm_set_union_shared_tag
  "foundation/permissions/PermSet\\.h:[0-9]+:[0-9]+: error: uncaught exception of type .std::meta::exception.. .what\\(\\).: .foundation::permissions \\[PermissionImbalance\\]: perm_set_union_t requires disjoint operands"
  "using foundation::permissions::perm_set_union_t = .* \\[with PS1 = foundation::permissions::PermSet<user_code::Alpha, user_code::Beta>. PS2 = foundation::permissions::PermSet<user_code::Alpha>\\]")

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
  "each_body_takes_its_child\\(.*\\^\\^ std::tuple<Callables \\.\\.\\.>\\) \\[with .*evaluated to .false.")

# A manifest that lists one child twice passes the door, and the fork's
# own check refuses it with a message that names the arm.
foundation_neg_compile_test(neg_permission_fork_duplicate_child_names_the_fork
  "static assertion failed"
  "mint_permission_fork_inline: Child region tags must be PAIRWISE DISTINCT")

# The write borrow door lends a token that the caller gives up for the
# time of the body.  A named token stays with the caller, so the door
# refuses it, and a context that does not admit the row of the tag is
# refused at the door.
foundation_neg_compile_test(neg_with_write_view_named_token
  "use of deleted function"
  "with_write_view lends its token to the body and hands the token back")
foundation_neg_compile_test(neg_with_write_view_row_not_admitted
  "no matching function for call to .with_write_view"
  "satisfaction of .CtxAdmitsPermission<Tag, Ctx>.")

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
  "permission_mint_args_shaped\\(std::size_t, std::size_t, std::initializer_list<std::meta::info>\\)\\(MaxCtx, NPerms, \\{\\^\\^ Args \\.\\.\\.\\}\\) \\[with MaxCtx = 2. NPerms = 1. .*evaluated to .false.")

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
# token keeps its brand: no conversion drops or changes it.
foundation_neg_compile_test(neg_read_loan_tag_not_empty
  "static assertion failed"
  "ReadLoan<Tag>: Tag must be an empty non-union class type")
foundation_neg_compile_test(neg_read_loan_brand_not_empty
  "static assertion failed"
  "ReadLoan<Tag, Brand>: Brand must be an empty class type")
foundation_neg_compile_test(neg_lent_permission_does_not_rebrand
  "no matching function for call to .foundation::permissions::LentPermission<"
  "no known conversion for argument 1 from .* to .foundation::permissions::LentPermission<neg_lent_permission_does_not_rebrand::Region, foundation::permissions::<lambda\\(\\)> >&&.")
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

# A share, a loan and a view of one region are not a share, a loan and a
# view of the erased identity: no conversion drops the brand.
foundation_neg_compile_test(neg_brand_shared_permission_does_not_erase
  "could not convert .branded. from .SharedPermission<"
  ",foundation::permissions::<lambda\\(\\)>>. to .SharedPermission<\\[\\.\\.\\.\\],foundation::brand::DefaultBrand>.")
foundation_neg_compile_test(neg_brand_read_loan_does_not_erase
  "conversion from .ReadLoan<\\[\\.\\.\\.\\],foundation::permissions::<lambda\\(\\)>>."
  "to non-scalar type .ReadLoan<\\[\\.\\.\\.\\],foundation::brand::DefaultBrand>. requested")
foundation_neg_compile_test(neg_brand_read_view_body_asks_the_erased_view
  "no matching function for call to .with_read_view\\("
  "no known conversion for argument 1 from .const foundation::permissions::ReadView<\\{anonymous\\}::Region, foundation::permissions::<lambda\\(\\)> >. to .const foundation::permissions::ReadView<\\{anonymous\\}::Region>&.")

# A branded token does not erase implicitly.  Only permission_erase_brand
# drops a brand, so a search for that name lists each site that drops one.
foundation_neg_compile_test(neg_brand_permission_does_not_erase_implicitly
  "conversion from .Permission<\\[\\.\\.\\.\\],foundation::permissions::<lambda\\(\\)>>."
  "to non-scalar type .Permission<\\[\\.\\.\\.\\],foundation::brand::DefaultBrand>. requested")

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

# No route builds a token, a share, the mint key or a read proof without
# a constructor.  std::bit_cast needs a trivially copyable type, and
# std::start_lifetime_as an implicit-lifetime one.  The key and a read
# proof have user-provided constructors and no trivial one.  A token, a
# share and a loan keep a trivial move constructor, so the ABI passes them
# in a register, and a user-provided assignment refuses std::bit_cast.
# That trivial constructor makes them implicit-lifetime: the rule
# owners refuses std::start_lifetime_as in the tree, and their
# annotation no_start_over_bytes refuses the checked lifetime start.
foundation_neg_compile_test(neg_permission_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::Permission<.*Region"
  "is_trivially_copyable_v<_To>")
foundation_neg_compile_test(neg_shared_permission_forged_by_bit_cast
  "no matching function for call to .bit_cast<foundation::permissions::SharedPermission<.*Region"
  "is_trivially_copyable_v<_To>")
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
foundation_neg_compile_test(neg_lent_permission_forged_by_start_lifetime_as
  "static assertion failed"
  "foundation::permissions::LentPermission<.*Region.*. is not an implicit-lifetime type")

# The linear marker of foundation/Linear.h.  A class that carries the
# annotation linear has the shape of a linear class, or the trait refuses
# it.  The drop door takes only a value that the caller gives up.
foundation_neg_compile_test(neg_linear_marked_class_without_the_shape
  "uncaught exception of type .std::meta::exception."
  "a class carries the annotation linear and does not have the shape of a linear class"
  "non-constant condition for static assertion")
foundation_neg_compile_test(neg_linear_drop_of_a_named_value
  "use of deleted function .constexpr void foundation::linear::drop\\(T&\\)"
  "the drop door ends a linear value that the caller gives up.  Pass it with std::move")

# A token and a share guard live in a scope or a handle.  Each deletes its
# class-scope operator new, so a new-expression and std::make_unique reach
# a deleted function.  The second regex is the reason that the header
# gives for the refusal.
foundation_neg_compile_test(neg_permission_heap_new
  "use of deleted function .static void\\* foundation::permissions::Permission<Tag, Brand>::operator new\\(std::size_t\\)"
  "a Permission lives in a scope or a handle, not on the heap")
foundation_neg_compile_test(neg_permission_make_unique
  "use of deleted function .static void\\* foundation::permissions::Permission<Tag, Brand>::operator new\\(std::size_t\\)"
  "a Permission lives in a scope or a handle, not on the heap"
  "use of deleted function .static void foundation::permissions::Permission<Tag, Brand>::operator delete\\(void\\*\\)")
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
# IsCapability and the instance query are concepts, and the template-id of
# a concept declares nothing.  The effect of a capability is a function
# that is not a template, so an explicit specialization of it matches no
# template.
foundation_neg_compile_test(neg_capability_specialized_trait
  "template-id .IsCapability<.anonymous.::Fake>. used as a declarator"
  "neg_capability_specialized_trait\\.cpp:18:[0-9]+: error: too many template headers for .foundation::effects::IsCapability. \\(should be 0\\)")
foundation_neg_compile_test(neg_capability_effect_specialized_trait
  "template-id .cap_of<>. for .consteval foundation::effects::Effect foundation::effects::cap_of\\(std::meta::info\\). does not match any template declaration"
  "neg_capability_effect_specialized_trait\\.cpp:13:[0-9]+: error:")
foundation_neg_compile_test(neg_instance_of_specialized_trait
  "template-id .IsInstanceOf<.anonymous.::Fake, \\^\\^template<foundation::effects::Effect Cap, class Source> class foundation::effects::Capability>. used as a declarator"
  "neg_instance_of_specialized_trait\\.cpp:19:[0-9]+: error: too many template headers for .foundation::reflect::IsInstanceOf. \\(should be 0\\)")
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
# A new object over bytes holds the bytes of the last store to them, also
# when that store has a different type.  The read runs in a function that
# GCC does not inline, so a Release build of a start with no barrier reads
# the first store.
foundation_test(test_lifetime_start)

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
  "detail::perm_brands_agree\\(std::initializer_list<std::meta::info>\\{.*\\^\\^foundation::permissions::Permission<\\{anonymous\\}::Left")

# The fresh brand sits after the argument pack, so a spelled brand is
# read as a context and refused by the root's fit concept.
foundation_neg_compile_test(neg_brand_root_mint_refuses_spelled_brand
  "no matching function for call to .mint_permission_root<\\{anonymous\\}::Region, \\{anonymous\\}::HeldBrand>\\(\\)."
  "constraints not satisfied"
  "PermissionRootArgs<Tag, Args \\.\\.\\.>")

# The result type holds the brand in undeducible_brand_t, so the target type
# of the root mint as a function sets no brand, and a binding to a target
# with a held brand is refused.
foundation_neg_compile_test(neg_brand_root_mint_address
  "invalid initialization of non-const reference of type .foundation::permissions::Permission<\\{anonymous\\}::Region, foundation::permissions::<lambda\\(\\)> > \\(&\\)\\(\\) noexcept. from an rvalue of type .<unresolved overloaded function type>."
  "neg_brand_root_mint_address\\.cpp:21:[0-9]+: error:")

# Erasure runs one way.
foundation_neg_compile_test(neg_brand_erased_token_does_not_rebrand
  "no matching function for call to .foundation::permissions::Permission<\\{anonymous\\}::Region, \\{anonymous\\}::HeldBrand>::Permission\\("
  "requires \\(is_same_v<Brand, foundation::brand::DefaultBrand>\\) && \\(IsFreshBrand<Other>\\)")

# ── The row relation is closed ───────────────────────────────────────

# A tag with no declared row has none, and the root mint says so.
foundation_neg_compile_test(neg_permission_row_undeclared_tag
  "static assertion failed: mint_permission_root<Tag>: no effect row is declared for Tag"
  "foundation::permissions::has_permission_row\\(\\^\\^\\{anonymous\\}::Undeclared\\). evaluates to false")

# One row has one source.
foundation_neg_compile_test(neg_permission_row_declared_twice
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .permission_row: a tag declares its row twice"
  "neg_permission_row_declared_twice\\.cpp:20:[0-9]+: error: non-constant condition for static assertion")

# The public row alias refuses a tag with no row, rather than read as
# void, and it refuses a member that names a type which is not a row.
foundation_neg_compile_test(neg_permission_row_t_undeclared_tag
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .permission_row: no effect row is declared for this permission tag"
  "using foundation::permissions::permission_row_t = .* \\[with Tag = \\{anonymous\\}::Undeclared\\]")
foundation_neg_compile_test(neg_permission_row_not_a_row
  "uncaught exception of type .std::meta::exception.. .what\\(\\).: .permission_row: the row declared for this permission tag is not a foundation::effects::Row"
  "using foundation::permissions::permission_row_t = .* \\[with Tag = \\{anonymous\\}::RowIsAnInt\\]")

# The row of a tag is the walk that the variable template row_cell_at
# keeps, which a translation unit can specialize.  A specialization cannot
# copy a cell, and the walk of another tag names that tag, which the read
# refuses.
foundation_neg_compile_test(neg_permission_row_cell_forged_by_copy
  "row_cell::row_cell\\(const foundation::permissions::detail::row_cell&\\). is private within this context"
  "neg_permission_row_cell_forged_by_copy\\.cpp:35:[0-9]+: error:")
foundation_neg_compile_test(neg_permission_row_cell_of_another_tag
  "call to non-.constexpr. function .void foundation::permissions::detail::a_row_cell_names_another_tag\\(\\)."
  "in .constexpr. expansion of .foundation::permissions::permission_row_empty\\(\\^\\^\\{anonymous\\}::IoTag\\).")

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
  "required for the satisfaction of .IsPermission<T>. \\[with T = int\\]"
  "required for the satisfaction of .IsInstanceOf<T, \\^\\^foundation::permissions::Permission>. \\[with T = int\\]")
foundation_neg_compile_test(neg_permission_is_shared_permission_refuses_exclusive
  "no matching function for call to .takes_a_shared_token"
  "required for the satisfaction of .IsSharedPermission<T>. \\[with T = foundation::permissions::Permission<<unnamed>::Region, .*\\]"
  "template_of\\(.*\\) == Template \\[with T = foundation::permissions::Permission<<unnamed>::Region, .*evaluated to .false.")

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
  "permission_mint_args_shaped\\(.*\\)\\(MaxCtx, NPerms, \\{\\^\\^ Args \\.\\.\\.\\}\\) \\[with MaxCtx = 1. NPerms = 1. Args = \\{foundation::permissions::Permission<<unnamed>::Whole, [^}]*>&\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_permission_combine_n_lvalue_token
  "satisfaction of .PermissionCombineNArgs<Parent, Args \\.\\.\\.>."
  "permission_mint_args_shaped\\(.*\\)\\(MaxCtx, NPerms, \\{\\^\\^ Args \\.\\.\\.\\}\\) \\[with MaxCtx = 1. NPerms = 0. Args = \\{foundation::permissions::Permission<<unnamed>::Left, [^}]*>&, ")

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
  "foundation::permissions::permission_row_empty\\(\\^\\^foundation::permissions::tag::HugePageTag\\)\\). evaluates to false")

# A federation peer token comes only from a verified handshake.  The root
# mint refuses the tag without a context and with one.
foundation_neg_compile_test(neg_permission_root_refuses_federated_peer
  "no matching function for call to .mint_permission_root<foundation::permissions::tag::FederatedPeer<\\{anonymous\\}::PeerOrg> >\\(\\)."
  "required for the satisfaction of .RootMintableTag<Tag>. \\[with Tag = foundation::permissions::tag::FederatedPeer<<unnamed>::PeerOrg>\\]"
  "the expression .!\\(IsFederatedPeerTag<Tag>\\) \\[with Tag = foundation::permissions::tag::FederatedPeer<<unnamed>::PeerOrg>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_permission_root_ctx_refuses_federated_peer
  "no matching function for call to .mint_permission_root<foundation::permissions::tag::FederatedPeer<\\{anonymous\\}::PeerOrg> >\\(\\{anonymous\\}::IoCtx&\\)."
  "required for the satisfaction of .RootMintableTag<Tag>. \\[with Tag = foundation::permissions::tag::FederatedPeer<<unnamed>::PeerOrg>\\]"
  "the expression .!\\(IsFederatedPeerTag<Tag>\\) \\[with Tag = foundation::permissions::tag::FederatedPeer<<unnamed>::PeerOrg>\\]. evaluated to .false.")

# Only an admission builds the key of the federation peer token.
foundation_neg_compile_test(neg_federation_admission_key_outside_admission
  ".constexpr foundation::permissions::federation_admission_key::federation_admission_key\\(\\). is private within this context"
  "foundation/permissions/FederationDoor\\.h:[0-9]+:[0-9]+: note: declared private here")

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
  "foundation::permissions::has_permission_row\\(\\^\\^.anonymous.::RowlessRight\\). evaluates to false")
foundation_neg_compile_test(neg_permission_split_without_ctx_refuses_row
  "static assertion failed: mint_permission_split<L, R>\\(Permission<In>&&\\) without ExecCtx is only valid"
  "mint_permission_split\\(Args&& \\.\\.\\.\\) .with L = .anonymous.::Left. R = .anonymous.::Right. Args = \\{Permission<.anonymous.::Whole, "
  "foundation::permissions::permission_row_empty\\(\\^\\^.anonymous.::Left\\)\\) && foundation::permissions::permission_row_empty\\(\\^\\^.anonymous.::Right\\)\\)\\). evaluates to false")
foundation_neg_compile_test(neg_permission_split_witness_missing
  "static assertion failed: can_split_into<In, L, R> is true but the accompanying has_split_authoring_witness"
  "has_split_authoring_witness_v<.anonymous.::Whole, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_duplicate_child
  "static assertion failed: mint_permission_split<L, R> requires L and R to be DISTINCT region tags"
  "foundation::permissions::tags_are_distinct\\(std::initializer_list<std::meta::info>\\{.*\\{\\^\\^.anonymous.::Half, \\^\\^.anonymous.::Half\\}.*\\). evaluates to false")
foundation_neg_compile_test(neg_permission_combine_child_row_undeclared
  "static assertion failed: mint_permission_combine<In>: a tag named here declares no effect row"
  "foundation::permissions::has_permission_row\\(\\^\\^.anonymous.::RowlessWhole\\). evaluates to false")
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
  "foundation::permissions::has_permission_row\\(\\^\\^.anonymous.::RowlessRight\\). evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_without_ctx_refuses_row
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.>\\(Permission<In>&&\\) without ExecCtx is only valid"
  "mint_permission_split_n\\(Args&& \\.\\.\\.\\) .with Children = \\{.anonymous.::Left, .anonymous.::Right\\}. Args = \\{Permission<.anonymous.::Whole, "
  "foundation::permissions::permission_row_empty\\(\\^\\^.anonymous.::Left\\) && foundation::permissions::permission_row_empty\\(\\^\\^.anonymous.::Right\\)\\)\\)\\). evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_undeclared_pack
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.>\\(Permission<In>&&\\) requires can_split_into_pack"
  "can_split_into_pack_v<.anonymous.::Whole, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_witness_missing
  "static assertion failed: has_split_pack_authoring_witness<In, Children\\.\\.\\.> missing"
  "has_split_pack_authoring_witness_v<.anonymous.::Whole, .anonymous.::Left, .anonymous.::Right>. evaluates to false")
foundation_neg_compile_test(neg_permission_split_n_duplicate_child
  "static assertion failed: mint_permission_split_n<Children\\.\\.\\.> requires the child tags to be PAIRWISE DISTINCT"
  "foundation::permissions::tags_are_distinct\\(std::initializer_list<std::meta::info>\\{.*\\{\\^\\^.anonymous.::Half, \\^\\^.anonymous.::Half\\}.*\\). evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_parent_row_undeclared
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>: a tag named here declares no effect row"
  "mint_permission_combine_n\\(Args&& \\.\\.\\.\\) .with Parent = .anonymous.::RowlessWhole. Args = \\{Permission<.anonymous.::Left, "
  "manifest.foundation::permissions::detail::combine_n_manifest::rows_declared. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_without_ctx_refuses_row
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>\\(\\.\\.\\.\\) without ExecCtx is only valid"
  "mint_permission_combine_n\\(Args&& \\.\\.\\.\\) .with Parent = .anonymous.::Whole. Args = \\{Permission<.anonymous.::Left, "
  "\\(\\(\\(!\\(bool\\)manifest.foundation::permissions::detail::combine_n_manifest::rows_declared\\) \\|\\| ")
foundation_neg_compile_test(neg_permission_combine_n_undeclared_parent
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>\\(Permission<Children>&&\\.\\.\\.\\) requires can_split_into_pack"
  "mint_permission_combine_n\\(Args&& \\.\\.\\.\\) .with Parent = .anonymous.::Other. Args = \\{Permission<.anonymous.::Left, "
  "manifest.foundation::permissions::detail::combine_n_manifest::declared. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_witness_missing
  "static assertion failed: has_split_pack_authoring_witness<Parent, Children\\.\\.\\.> missing for combine_n"
  "mint_permission_combine_n\\(Args&& \\.\\.\\.\\) .with Parent = .anonymous.::Other. Args = \\{Permission<.anonymous.::Left, "
  "manifest.foundation::permissions::detail::combine_n_manifest::witnessed. evaluates to false")
foundation_neg_compile_test(neg_permission_combine_n_children_of_two_regions
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.>\\(\\.\\.\\.\\): the children carry different brands"
  "detail::perm_brands_agree\\(std::initializer_list<std::meta::info>\\{.*\\^\\^foundation::permissions::Permission<.anonymous.::Left")
# A brand names a mint site, so two root mints through one site are two
# tokens of one tag that the brand agreement admits.  The distinctness
# check is what refuses them.
foundation_neg_compile_test(neg_permission_combine_n_duplicate_child
  "static assertion failed: mint_permission_combine_n<Parent, Children\\.\\.\\.> requires the child tags to be PAIRWISE DISTINCT"
  "mint_permission_combine_n\\(Args&& \\.\\.\\.\\) .with Parent = .anonymous.::Whole. Args = \\{Permission<.anonymous.::Half, "
  "manifest.foundation::permissions::detail::combine_n_manifest::distinct. evaluates to false")

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
  "mint_permission_share\\(Args&& \\.\\.\\.\\) .with Args = \\{Permission<.anonymous.::RowlessRegion, "
  "foundation::permissions::has_permission_row\\(\\^\\^Tag\\). evaluates to false")
foundation_neg_compile_test(neg_permission_share_without_ctx_refuses_row
  "static assertion failed: mint_permission_share\\(Permission<Tag>&&\\) without ExecCtx is only valid"
  "mint_permission_share\\(Args&& \\.\\.\\.\\) .with Args = \\{Permission<.anonymous.::NeedsIo, "
  "foundation::permissions::permission_row_empty\\(\\^\\^Tag\\)\\). evaluates to false")
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
  "foundation::permissions::permission_row_empty\\(\\^\\^.anonymous.::NeedsIo\\)\\). evaluates to false")
foundation_neg_compile_test(neg_share_pool_upgrade_without_ctx_refuses_row
  "static assertion failed: SharedPermissionPool<Tag>::try_upgrade\\(\\) without ExecCtx is only valid"
  "foundation::permissions::permission_row_empty\\(\\^\\^.anonymous.::NeedsIo\\)\\). evaluates to false")
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
# CRUCIBLE_DIAG_ASSERT names its tag through an enumerator of Category, the
# closed set of the catalog, so a tag class outside the catalog does not
# compile, also when the condition holds.
foundation_neg_compile_test(neg_diag_assert_refuses_a_tag_outside_the_catalog
  ".LooksLikeACatalogTag. is not a member of .foundation::diag::Category."
  "neg_diag_assert_refuses_a_tag_outside_the_catalog\\.cpp:15:[0-9]+: error:")

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
# Two translation units each build a class local to f<UnitLocal>, where
# UnitLocal has internal linkage.  The two classes print one name and
# hold two numbers, so a stable id refuses each of them.
foundation_test(test_stable_id_function_local
  test_stable_id_function_local.cpp
  test_stable_id_function_local_second_unit.cpp)

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
  "In instantiation of .constexpr const uint64_t foundation::effects::host::ProducerClaim<.anonymous.::Hidden>::brand_key_.")
foundation_neg_compile_test(neg_producer_claim_brand_in_closure
  "because its part main\\(\\)::<lambda\\(\\)>::InClosure is declared in a function body"
  "In instantiation of .constexpr const uint64_t foundation::effects::host::ProducerClaim<main\\(\\)::<lambda\\(\\)>::InClosure>::brand_key_.")
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
# A class that a function body declares prints the name of its function,
# so two local classes of one name in one function print one name.
foundation_neg_compile_test(neg_stable_id_refuses_a_function_local_class
  "a stable id refuses the type main\\(\\)::Local"
  "because its part main\\(\\)::Local is declared in a function body")
# A NaN prints without its payload and a union value without its active
# member, so two different values print one text.
foundation_neg_compile_test(neg_stable_id_refuses_a_nan_value
  "a stable id refuses the type"
  "prints a text that a different value also prints")
foundation_neg_compile_test(neg_stable_id_refuses_a_union_value
  "a stable id refuses the type"
  "prints a text that a different value also prints")
# The gate calls the identity walk itself, so a translation unit cannot
# put a verdict of no fault in front of the walk for a closure type.
foundation_neg_compile_test(neg_stable_identity_verdict_forged
  "expected initializer before .<. token"
  "neg_stable_identity_verdict_forged\\.cpp:22:[0-9]+: error:")
# Each stable id is a function that is not a template, and the identity
# walk reads the function of a pointer through an alias template.  A unit
# has no template to specialize there, so no unit can forge an id.
foundation_neg_compile_test(neg_stable_type_id_specialized
  "expected initializer before .<. token"
  "neg_stable_type_id_specialized\\.cpp:19:[0-9]+: error:")
foundation_neg_compile_test(neg_stable_identity_function_reader_specialized
  "expected initializer before .<. token"
  "neg_stable_identity_function_reader_specialized\\.cpp:21:[0-9]+: error:")
foundation_neg_compile_test(neg_stable_function_name_id_specialized
  "expected initializer before .<. token"
  "neg_stable_function_name_id_specialized\\.cpp:18:[0-9]+: error:")
# Only function_key makes a key, so a holder that pairs one function with
# a pointer to a different function is refused.
foundation_neg_compile_test(neg_stable_function_name_id_of_a_forged_holder
  "uncaught exception of type .std::meta::exception.*a function id takes the reflection of a function key"
  "neg_stable_function_name_id_of_a_forged_holder\\.cpp:21:[0-9]+: error:")

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
crucible_contract_ignore_target(test_swiss_table_buffer_ignore)

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

# ── The Report family: texts, reports and the fatal exit ─────────────
#
# A text of a report is a string literal with one placeholder `{}` for each
# argument, and with `{{` and `}}` for a brace.  An argument is an integer,
# a bool, a TextView, a float or a double.  fatal and unreachable end the
# process in each build.  format() writes into a FixedText and gives a
# Result, and it refuses a temporary FixedText.  test_core_float_format
# compares the text of each float with the text of std::to_chars.  parse()
# reads an integer of a text, and test_core_parse compares it with a
# reference of the grammar and with std::from_chars.
foundation_test(test_core_report)
foundation_test(test_core_text)
foundation_test(test_core_float_format)
foundation_test(test_core_parse)

foundation_neg_compile_test(neg_core_parse_character_type
  "no matching function for call to .parse<char>\\(const char \\[2\\]\\)."
  "the expression .!\\(same_as<T, char>\\) \\[with T = char\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_parse_floating_type
  "no matching function for call to .parse<double>\\(const char \\[2\\]\\)."
  "required for the satisfaction of .Integer<T>. \\[with T = double\\]")

foundation_neg_compile_test(neg_core_fmt_more_placeholders_than_arguments
  "call to consteval function .foundation::core::Fmt<>\\(\"the queue holds \\{\\} items\", __builtin_FILE\\(\\), __builtin_LINE\\(\\), __builtin_FUNCTION\\(\\)\\). is not a constant expression"
  "call to non-.constexpr. function .void foundation::core::detail::fmt_text_has_more_placeholders_than_arguments\\(\\).")
foundation_neg_compile_test(neg_core_fmt_fewer_placeholders_than_arguments
  "call to consteval function .foundation::core::Fmt<int>\\(\"the queue is full\", __builtin_FILE\\(\\), __builtin_LINE\\(\\), __builtin_FUNCTION\\(\\)\\). is not a constant expression"
  "call to non-.constexpr. function .void foundation::core::detail::fmt_text_has_fewer_placeholders_than_arguments\\(\\).")
foundation_neg_compile_test(neg_core_fmt_lone_brace
  "call to consteval function .foundation::core::Fmt<>\\(\"the set \\{ a, b.012\", __builtin_FILE\\(\\), __builtin_LINE\\(\\), __builtin_FUNCTION\\(\\)\\). is not a constant expression"
  "call to non-.constexpr. function .void foundation::core::detail::fmt_text_holds_a_brace_that_is_no_placeholder_and_no_escape\\(\\).")
foundation_neg_compile_test(neg_core_fmt_brace_spec
  "call to consteval function .foundation::core::Fmt<int>\\(\"the mask is \\{:x\\}\", __builtin_FILE\\(\\), __builtin_LINE\\(\\), __builtin_FUNCTION\\(\\)\\). is not a constant expression"
  "call to non-.constexpr. function .void foundation::core::detail::fmt_text_holds_a_brace_that_is_no_placeholder_and_no_escape\\(\\).")
foundation_neg_compile_test(neg_core_fmt_argument_long_double
  "template constraint failure for .template<class ... Args>  requires \\(Formattable<Args> && ...\\) class foundation::core::Fmt."
  "the expression .\\(Formattable<Args> && ...\\) \\[with Args = \\{long double\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_fmt_argument_float16
  "template constraint failure for .template<class ... Args>  requires \\(Formattable<Args> && ...\\) class foundation::core::Fmt."
  "the expression .\\(Formattable<Args> && ...\\) \\[with Args = \\{_Float16\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_fmt_argument_pointer
  "template constraint failure for .template<class ... Args>  requires \\(Formattable<Args> && ...\\) class foundation::core::Fmt."
  "the expression .\\(Formattable<Args> && ...\\) \\[with Args = \\{int\\*\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_fatal_runtime_text
  "no matching function for call to .fatal\\(const char\\*\\)."
  "no known conversion for argument 1 from .const char\\*. to .foundation::core::Fmt<>.")
foundation_neg_compile_test(neg_core_text_view_no_terminating_zero
  "in .constexpr. expansion of .*foundation::core::TextView::TextView<3>\\(letters\\)."
  "call to non-.constexpr. function .void foundation::core::detail::text_literal_has_no_terminating_zero\\(\\).")
foundation_neg_compile_test(neg_core_text_view_from_pointer
  "no matching function for call to .foundation::core::TextView::TextView\\(<brace-enclosed initializer list>\\)."
  "mismatched types .const char \\[Length\\]. and .const char\\*.")
foundation_neg_compile_test(neg_core_c_text_no_terminating_zero
  "in .constexpr. expansion of .*foundation::core::CText::CText<3>\\(letters\\)."
  "call to non-.constexpr. function .void foundation::core::detail::text_literal_has_no_terminating_zero\\(\\).")
foundation_neg_compile_test(neg_core_c_text_zero_before_its_end
  "in .constexpr. expansion of .*foundation::core::CText::CText<19>\\(.*\\)."
  "call to non-.constexpr. function .void foundation::core::detail::text_literal_holds_a_zero_byte_before_its_end\\(\\).")
foundation_neg_compile_test(neg_core_format_temporary_text
  "use of deleted function .void foundation::core::format\\(FixedText<Capacity>&&, const Fmt<typename std::type_identity<[A-Za-z]+>::type ...>&, Args ...\\) \\[with long unsigned int Capacity = 16. Args = \\{int\\}\\]"
  "format\\(\\) gives a view of its FixedText, and a temporary FixedText ends with the full expression")
foundation_neg_compile_test(neg_core_format_result_dropped
  "ignoring return value of .foundation::core::Result<foundation::core::TextView, foundation::core::Truncated> foundation::core::format\\("
  "declared with attribute .nodiscard.")
foundation_neg_compile_test(neg_core_fixed_text_view_of_temporary
  "use of deleted function .foundation::core::TextView foundation::core::FixedText<Capacity>::view\\(\\) const && \\[with long unsigned int Capacity = 8\\]."
  "a view of a temporary FixedText points into an object that the full expression ends")
foundation_neg_compile_test(neg_core_fixed_text_zero_capacity
  "template constraint failure for .template<long unsigned int Capacity>  requires  Capacity > 0 && Capacity <= static_cast<std::size_t>\\(9223372036854775807\\) class foundation::core::FixedText."
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")

# ── The Choice family ─────────────────────────────────────────────────
#
# An Option holds an object whose move does not throw.  The reason of
# expect() is a text of a report.  A cursor of a temporary and a copy of an
# owning Option are refused.  The error of a Result is a scoped enum or a
# small trivially copyable class.  An Err is neither a payload nor an error,
# and a niche that a payload declares must be well formed.  An Err of a
# different error converts only through an implicit conversion that does not
# throw, and an Option and a Result do not convert to each other.
# CRUCIBLE_TRY consumes an rvalue of an Option or a Result, in a function
# whose return takes the failure.
foundation_test(test_core_choice)
foundation_test(test_core_try)
foundation_test(test_core_one_of)

foundation_neg_compile_test(neg_core_option_reference_payload
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "the expression .is_object_v<T> \\[with T = int&\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_option_throwing_move_payload
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "the expression .is_nothrow_move_constructible_v<T> \\[with T = ThrowingMove\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_option_expect_runtime_reason
  "cannot convert .const char\\*. to .foundation::core::Fmt<>."
  "initializing argument 1 of .constexpr T foundation::core::Option<T>::expect\\(foundation::core::Fmt<>\\) && \\[with T = int\\].")
foundation_neg_compile_test(neg_core_option_expect_reason_with_zero
  "call to consteval function .foundation::core::Fmt<>\\(\"the option.000 holds a value\", __builtin_FILE\\(\\), __builtin_LINE\\(\\), __builtin_FUNCTION\\(\\)\\). is not a constant expression"
  "call to non-.constexpr. function .void foundation::core::detail::fmt_text_holds_a_zero_byte_before_its_end\\(\\).")
foundation_neg_compile_test(neg_core_option_cursor_of_temporary
  "use of deleted function .void foundation::core::Option<T>::begin\\(\\) && \\[with T = int\\]."
  "a cursor of a temporary Option points into an object that the full expression ends")
foundation_neg_compile_test(neg_core_option_owning_copied
  "use of deleted function .foundation::core::Option<Owner>::Option\\(const foundation::core::Option<Owner>&\\)."
  "an owning Option is move-only: a copy would duplicate what the payload owns")
foundation_neg_compile_test(neg_core_result_integer_error
  "template constraint failure for .template<class T, class E>  requires \\(ChoicePayload<T>\\) && \\(ErrorValue<E>\\)"
  "class foundation::core::Result \\[with T = int. E = int\\]")
foundation_neg_compile_test(neg_core_result_owning_error
  "template constraint failure for .template<class T, class E>  requires \\(ChoicePayload<T>\\) && \\(ErrorValue<E>\\)"
  "class foundation::core::Result \\[with T = int. E = CountedError\\]")
# The error gate admits a class of at most 16 bytes whose copy, move and end
# are those of its bytes.  The gate is a disjunction, so each fixture takes
# a concept depth of 3, and its regex names the part of the shape that fails.
foundation_neg_compile_test(neg_core_result_error_nontrivial_destructor
  "required for the satisfaction of .TrivialCopyShape<E>. \\[with E = LoggedError\\]"
  "the expression .is_trivially_copy_constructible_v<T> \\[with T = LoggedError\\]. evaluated to .false.")
set_property(TEST neg_core_result_error_nontrivial_destructor APPEND PROPERTY ENVIRONMENT
  "CRUCIBLE_NEG_EXTRA_FLAGS=-fconcepts-diagnostics-depth=3")
foundation_neg_compile_test(neg_core_result_error_sealed_too_wide
  "required for the satisfaction of .ErrorValue<E>. \\[with E = WideSealedError\\]"
  "the expression .sizeof \\(E\\) <= 16 \\[with E = WideSealedError\\]. evaluated to .false.")
set_property(TEST neg_core_result_error_sealed_too_wide APPEND PROPERTY ENVIRONMENT
  "CRUCIBLE_NEG_EXTRA_FLAGS=-fconcepts-diagnostics-depth=3")
foundation_neg_compile_test(neg_core_result_err_payload
  "template constraint failure for .template<class T, class E>  requires \\(ChoicePayload<T>\\) && \\(ErrorValue<E>\\)"
  "the expression .!\\(ErrMark<T>\\) \\[with T = foundation::core::Err<Code>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_option_match_one_arm
  "no matching function for call to .foundation::core::Option<int>::match\\(main\\(\\)::<lambda\\(int\\)>\\)."
  "candidate expects 2 arguments, 1 provided")
foundation_neg_compile_test(neg_core_option_match_returns_reference
  "no matching function for call to .foundation::core::Option<int>::match\\(main\\(\\)::<lambda\\(int&&\\)>, main\\(\\)::<lambda\\(\\)>\\)."
  "the expression .!\\(is_reference_v<typename std::invoke_result<OnNone>::type>\\) \\[with OnNone = main::._anon_[0-9]+\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_option_match_arm_types
  "no matching function for call to .foundation::core::Option<int>::match\\(main\\(\\)::<lambda\\(int\\)>, main\\(\\)::<lambda\\(\\)>\\)."
  "the expression .is_same_v<_Tp, _Up> \\[with _Tp = int. _Up = long int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_option_err_payload
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "the expression .!\\(ErrMark<T>\\) \\[with T = foundation::core::Err<Code>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_result_other_err_payload
  "template constraint failure for .template<class T, class E>  requires \\(ChoicePayload<T>\\) && \\(ErrorValue<E>\\)"
  "the expression .!\\(ErrMark<T>\\) \\[with T = foundation::core::Err<Other>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_result_err_mark_error
  "template constraint failure for .template<class T, class E>  requires \\(ChoicePayload<T>\\) && \\(ErrorValue<E>\\)"
  "the expression .!\\(ErrMark<E>\\) \\[with E = foundation::core::Err<Code>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_option_niche_throwing_empty
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "required for the satisfaction of .WellFormedNiche<T>. \\[with T = SlotNumber\\]")
foundation_neg_compile_test(neg_core_option_niche_owning_payload
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "required for the satisfaction of .WellFormedNiche<T>. \\[with T = Handle\\]")
foundation_neg_compile_test(neg_core_option_niche_member_half_protocol
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "required for the satisfaction of .WellFormedNiche<T>. \\[with T = SlotNumber\\]")
foundation_neg_compile_test(neg_core_option_niche_member_wrong_type
  "template constraint failure for .template<class T>  requires  ChoicePayload<T> class foundation::core::Option."
  "required for the satisfaction of .WellFormedNiche<T>. \\[with T = SlotNumber\\]")
foundation_neg_compile_test(neg_core_niche_ref_forge
  "no matching function for call to .foundation::core::niche<foundation::core::Ref<int, ForgeBrand> >::empty\\(\\)."
  "candidate expects 1 argument, 0 provided")
foundation_neg_compile_test(neg_core_niche_ref_brace_key
  ".constexpr foundation::core::niche_key<T>::niche_key\\(\\) \\[with T = foundation::core::Ref<int, ForgeBrand>\\]. is private within this context"
  "foundation/core/Choice.h:[0-9]+:[0-9]+: note: declared private here")
foundation_neg_compile_test(neg_core_niche_member_no_key
  "no matching function for call to .SlotNumber::niche_empty\\(\\)."
  "candidate expects 1 argument, 0 provided")
foundation_neg_compile_test(neg_core_niche_key_of_other_type
  "cannot convert .const foundation::core::niche_key<SlotNumber>. to .const foundation::core::niche_key<foundation::core::Ref<int, ForgeBrand> >&."
  "initializing argument 1 of .static constexpr foundation::core::Ref<[A-Za-z]+, [A-Za-z]+> foundation::core::niche<foundation::core::Ref<[A-Za-z]+, [A-Za-z]+> >::empty\\(")
foundation_neg_compile_test(neg_core_ref_door_forges_null
  ".int\\* foundation::core::Ref<int, ForgeBrand>::object_. is private within this context"
  "foundation/core/Ref.h:[0-9]+:[0-9]+: note: declared private here")
foundation_neg_compile_test(neg_core_niche_ref_outside_specialization
  "no matching function for call to .foundation::core::Ref<Forged, ForgeBrand>::Ref\\(<brace-enclosed initializer list>\\)."
  "no known conversion for argument 1 from .Forged\\*. to .foundation::core::Ref<Forged, ForgeBrand>::NullTag.")
foundation_neg_compile_test(neg_core_option_niche_lifetime_start
  "no matching function for call to .start_as_array<foundation::core::Option<SlotNumber> >"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = foundation::core::Option<SlotNumber>\\]")
foundation_neg_compile_test(neg_core_result_lifetime_start
  "no matching function for call to .start_as_array<foundation::core::Result<unsigned int, Code> >"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = foundation::core::Result<unsigned int, Code>\\]")
foundation_neg_compile_test(neg_core_result_error_unrelated
  "use of deleted function .foundation::core::Result<T, E>::Result\\(foundation::core::Err<F>\\) \\[with F = Other. T = int. E = Code\\]."
  "the error type does not convert to the error type of this Result: give E a noexcept implicit constructor from F, or map the error with match")
foundation_neg_compile_test(neg_core_result_error_explicit_conversion
  "use of deleted function .foundation::core::Result<T, E>::Result\\(foundation::core::Err<F>\\) \\[with F = Code. T = int. E = NetError\\]."
  "the error type does not convert to the error type of this Result")
foundation_neg_compile_test(neg_core_result_error_throwing_conversion
  "use of deleted function .foundation::core::Result<T, E>::Result\\(foundation::core::Err<F>\\) \\[with F = Code. T = int. E = NetError\\]."
  "give E a noexcept implicit constructor from F")
foundation_neg_compile_test(neg_core_option_from_err
  "use of deleted function .foundation::core::Option<T>::Option\\(foundation::core::Err<F>\\) \\[with F = Code. T = int\\]."
  "a Result carries an error that an Option drops: write std::move\\(result\\).ok\\(\\)")
foundation_neg_compile_test(neg_core_result_from_none
  "use of deleted function .foundation::core::Result<T, E>::Result\\(foundation::core::NoValue\\) \\[with T = int. E = Code\\]."
  "an empty Option has no error for a Result: write std::move\\(option\\).ok_or\\(error\\)")
foundation_neg_compile_test(neg_core_option_ok_or_integer_error
  "no matching function for call to .foundation::core::Option<int>::ok_or\\(int\\)."
  "required for the satisfaction of .ErrorValue<E>. \\[with E = int\\]")
foundation_neg_compile_test(neg_core_try_lvalue_operand
  "static assertion failed: CRUCIBLE_TRY consumes a Result or an Option: give it a temporary or std::move\\(name\\)"
  "in expansion of macro .CRUCIBLE_TRY.")
foundation_neg_compile_test(neg_core_try_non_choice_operand
  "static assertion failed: CRUCIBLE_TRY consumes a Result or an Option: give it a temporary or std::move\\(name\\)"
  "in expansion of macro .CRUCIBLE_TRY.")
foundation_neg_compile_test(neg_core_try_result_in_option_function
  "use of deleted function .foundation::core::Option<T>::Option\\(foundation::core::Err<F>\\) \\[with F = Code. T = int\\]."
  "a Result carries an error that an Option drops")
foundation_neg_compile_test(neg_core_try_option_in_result_function
  "use of deleted function .foundation::core::Result<T, E>::Result\\(foundation::core::NoValue\\) \\[with T = int. E = Code\\]."
  "an empty Option has no error for a Result")
foundation_neg_compile_test(neg_core_try_void_function
  "return-statement with a value, in function returning .void."
  "in expansion of macro .CRUCIBLE_TRY.")
foundation_neg_compile_test(neg_core_try_constructor
  "returning a value from a constructor"
  "in expansion of macro .CRUCIBLE_TRY.")
foundation_neg_compile_test(neg_core_try_twice_on_one_line
  "conflicting declaration .auto&& crucible_try_hold_[0-9]+."
  "in expansion of macro .CRUCIBLE_TRY.")
foundation_neg_compile_test(neg_core_try_unbraced_if
  ".crucible_try_hold_[0-9]+. was not declared in this scope"
  "template argument 1 is invalid"
  "unused variable .crucible_try_hold_[0-9]+.")
foundation_neg_compile_test(neg_core_one_of_single
  "template constraint failure for .template<class \\.\\.\\. Ts>  requires  OneOfAlternatives<Ts \\.\\.\\.> class foundation::core::OneOf."
  "the expression .sizeof \\.\\.\\. \\(Ts \\.\\.\\.\\) >= 2 \\[with Ts = \\{int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_one_of_duplicate
  "template constraint failure for .template<class \\.\\.\\. Ts>  requires  OneOfAlternatives<Ts \\.\\.\\.> class foundation::core::OneOf."
  "the expression .\\(OccursOnce<Ts, Ts \\.\\.\\.> && \\.\\.\\.\\) \\[with Ts = \\{int, long int, int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_one_of_reference
  "template constraint failure for .template<class \\.\\.\\. Ts>  requires  OneOfAlternatives<Ts \\.\\.\\.> class foundation::core::OneOf."
  "the expression .\\(ChoicePayload<Ts> && \\.\\.\\.\\) \\[with Ts = \\{int&, long int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_one_of_non_member_value
  "no matching function for call to .foundation::core::OneOf<int, long int>::OneOf\\(<brace-enclosed initializer list>\\)."
  "the expression .\\(is_same_v<T, Ts> \\|\\| \\.\\.\\.\\) \\[with T = unsigned int. Ts = \\{int, long int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_one_of_no_default
  "no matching function for call to .foundation::core::OneOf<int, long int>::OneOf\\(<brace-enclosed initializer list>\\)."
  "candidate expects 1 argument, 0 provided")
foundation_neg_compile_test(neg_core_one_of_match_missing_arm
  "no matching function for call to .foundation::core::OneOf<int, Code, long int>::match\\(main\\(\\)::<lambda\\(int\\)>, main\\(\\)::<lambda\\(Code\\)>\\)."
  "the expression .sizeof \\.\\.\\. \\(Arms \\.\\.\\.\\) == sizeof \\.\\.\\. \\(Ts \\.\\.\\.\\) \\[with Arms = \\{main::._anon_[0-9]+, main::._anon_[0-9]+\\}. Ts = \\{int, Code, long int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_one_of_match_arm_types
  "no matching function for call to .foundation::core::OneOf<int, Code>::match\\(main\\(\\)::<lambda\\(int\\)>, main\\(\\)::<lambda\\(Code\\)>\\)."
  "the expression .\\(same_as<typename std::invoke_result<Arms, Ts&&>::type, typename std::invoke_result<Arms \\.\\.\\.\\[0\\],")
foundation_neg_compile_test(neg_core_one_of_match_returns_reference
  "no matching function for call to .foundation::core::OneOf<int, Code>::match\\(main\\(\\)::<lambda\\(int&&\\)>, main\\(\\)::<lambda\\(Code&&\\)>\\)."
  "the expression .!\\(is_reference_v<typename std::invoke_result<Arms \\.\\.\\.\\[0\\],")
foundation_neg_compile_test(neg_core_one_of_is_non_member
  "no matching function for call to .foundation::core::OneOf<int, long int>::is<short int>\\(\\) const."
  "required for the satisfaction of .AlternativeOf<T, Ts \\.\\.\\.>. \\[with T = short int. Ts = \\{int, long int\\}\\]")
foundation_neg_compile_test(neg_core_one_of_expect_non_member
  "no matching function for call to .foundation::core::OneOf<int, long int>::expect<short int>\\(const char \\[8\\]\\)."
  "required for the satisfaction of .AlternativeOf<T, Ts \\.\\.\\.>. \\[with T = short int. Ts = \\{int, long int\\}\\]")
foundation_neg_compile_test(neg_core_one_of_owning_copied
  "use of deleted function .foundation::core::OneOf<Owner, int>::OneOf\\(const foundation::core::OneOf<Owner, int>&\\)."
  "an owning sum is move-only: a copy would duplicate what an alternative owns")
foundation_neg_compile_test(neg_core_one_of_temporary_borrow_match
  "use of deleted function .void foundation::core::OneOf<Ts>::match\\(Arms&& \\.\\.\\.\\) const && \\[with Arms = \\{main\\(\\)::<lambda\\(const int&\\)>, main\\(\\)::<lambda\\(const Code&\\)>\\}. Ts = \\{int, Code\\}\\]."
  "a borrow match of a temporary OneOf gives a reference into an object that the full expression ends")
foundation_neg_compile_test(neg_core_option_temporary_borrow_match
  "use of deleted function .void foundation::core::Option<T>::match\\(OnSome&&, OnNone&&\\) const && \\[with OnSome = main\\(\\)::<lambda\\(const int&\\)>. OnNone = main\\(\\)::<lambda\\(\\)>. T = int\\]."
  "a borrow match of a temporary Option gives a reference into an object that the full expression ends")
foundation_neg_compile_test(neg_core_result_temporary_borrow_match
  "use of deleted function .void foundation::core::Result<T, E>::match\\(OnOk&&, OnErr&&\\) const && \\[with OnOk = main\\(\\)::<lambda\\(const int&\\)>. OnErr = main\\(\\)::<lambda\\(const Code&\\)>. T = int. E = Code\\]."
  "a borrow match of a temporary Result gives a reference into an object that the full expression ends")
# A move assignment drops what the target holds, so a sum with a linear
# part has none.
foundation_neg_compile_test(neg_core_option_linear_assign
  "use of deleted function .foundation::core::Option<Token>& foundation::core::Option<Token>::operator=\\(foundation::core::Option<Token>&&\\)."
  "an assignment drops the payload that the Option holds, and a linear payload must not be dropped")
foundation_neg_compile_test(neg_core_result_linear_assign
  "use of deleted function .foundation::core::Result<int, Token>& foundation::core::Result<int, Token>::operator=\\(foundation::core::Result<int, Token>&&\\)."
  "use of deleted function .foundation::core::detail::ResultSlot<int, Token>& foundation::core::detail::ResultSlot<int, Token>::operator=\\(foundation::core::detail::ResultSlot<int, Token>&&\\)."
  "an assignment drops the alternative that the sum holds, and a linear alternative must not be dropped")
# A sum compares only with a sum of its own type, and a floating-point part
# has no compare.
foundation_neg_compile_test(neg_core_option_compare_none
  "use of deleted function .bool foundation::core::operator==\\(const Option<T>&, const Other&\\) \\[with Other = NoValue. T = int\\]."
  "an Option compares only with an Option of its own payload type")
foundation_neg_compile_test(neg_core_option_compare_other_type
  "use of deleted function .bool foundation::core::operator==\\(const Option<T>&, const Other&\\) \\[with Other = Option<long int>. T = int\\]."
  "an Option compares only with an Option of its own payload type")
foundation_neg_compile_test(neg_core_option_compare_float
  "no match for .operator==. \\(operand types are .const foundation::core::Option<double>. and .const foundation::core::Option<double>.\\)"
  "the expression .!\\(is_floating_point_v<T>\\) \\[with T = double\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_result_compare_value
  "use of deleted function .bool foundation::core::operator==\\(const Result<T, E>&, const Other&\\) \\[with Other = int. T = int. E = Code\\]."
  "a Result compares only with a Result of its own value and error types")
foundation_neg_compile_test(neg_core_one_of_compare_value
  "use of deleted function .bool foundation::core::operator==\\(const OneOf<Ts>&, const Other&\\) \\[with Other = int. Ts = \\{int, long int\\}\\]."
  "a OneOf compares only with a OneOf of its own alternatives")
# The borrow match of each sum carries the annotation borrow_match, and the
# rule borrow-match of the plugin refuses an arm that writes the sum.
foundation_neg_compile_test(neg_core_option_borrow_match_arm_assigns
  "neg_core_option_borrow_match_arm_assigns\\.cpp:11:[0-9]+: error: this arm of a borrow match writes the sum that the match reads \\(assignment held\\)"
  "an assignment, a move or a mutating call of the sum ends or changes the payload that the reference names")
foundation_neg_compile_test(neg_core_result_borrow_match_arm_moves
  "neg_core_result_borrow_match_arm_moves\\.cpp:15:[0-9]+: error: this arm of a borrow match writes the sum that the match reads \\(move result\\)"
  "an assignment, a move or a mutating call of the sum ends or changes the payload that the reference names")
foundation_neg_compile_test(neg_core_one_of_borrow_match_arm_assigns
  "neg_core_one_of_borrow_match_arm_assigns\\.cpp:13:[0-9]+: error: this arm of a borrow match writes the sum that the match reads \\(assignment held\\)"
  "an assignment, a move or a mutating call of the sum ends or changes the payload that the reference names")
foundation_neg_compile_test(neg_core_surface_open_name
  "static assertion failed: the public member constexpr bool Probe::has_value\\(\\) const of Probe is no shared verb, no language protocol and no name of the family list"
  "in expansion of macro .CRUCIBLE_CLOSED_SURFACE.")
foundation_neg_compile_test(neg_core_surface_open_base
  "static assertion failed: the public member Base::raw of Probe is no shared verb, no language protocol and no name of the family list"
  "in expansion of macro .CRUCIBLE_CLOSED_SURFACE.")
foundation_neg_compile_test(neg_core_surface_member_type
  "static assertion failed: the public member Probe<int>::value_type \\{aka int\\} of Probe<int> is no shared verb, no language protocol and no name of the family list"
  "in expansion of macro .CRUCIBLE_CLOSED_SURFACE.")
foundation_neg_compile_test(neg_core_surface_ungranted_dereference
  "static assertion failed: the public member constexpr int Probe::operator\\*\\(\\) const of Probe is no shared verb, no language protocol and no name of the family list"
  "in expansion of macro .CRUCIBLE_CLOSED_SURFACE.")
foundation_neg_compile_test(neg_core_surface_unknown_grant
  "no matching function for call to .closed_surface<Probe, CallProtocol>\\(const foundation::core::TextView&\\)."
  "the expression .\\(\\.\\.\\. && LanguageProtocolGrant<Grants>\\) \\[with Grants = \\{CallProtocol\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_surface_not_class
  "no matching function for call to .closed_surface<int>\\(foundation::core::TextView\\)."
  "the expression .is_class_v<T> \\[with T = int\\]. evaluated to .false.")

# ── The Record family ─────────────────────────────────────────────────
#
# A Record is an aggregate of parts that are objects, not arrays and not
# cv-qualified, with a move that does not throw.  It has no converting
# constructor, and its list initialization refuses a narrowing conversion.
# A structured binding names each part, and at() takes a constant index below
# the count of parts on a named Record only.  == and <=> compare two Records of
# one type only, and only when each part has the operator.  The Pack
# operations take a Pack only, an index below the count, and a type that is
# in the Pack one time.
foundation_test(test_core_record)
foundation_test(test_core_record_oracle)

foundation_neg_compile_test(neg_core_record_reference_part
  "template constraint failure for .template<class ... Ts>  requires \\(... && RecordElement<Ts>\\) struct foundation::core::Record."
  "the expression .\\(... && RecordElement<Ts>\\) \\[with Ts = \\{int&, int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_const_part
  "template constraint failure for .template<class ... Ts>  requires \\(... && RecordElement<Ts>\\) struct foundation::core::Record."
  "the expression .\\(... && RecordElement<Ts>\\) \\[with Ts = \\{const int, int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_array_part
  "template constraint failure for .template<class ... Ts>  requires \\(... && RecordElement<Ts>\\) struct foundation::core::Record."
  "the expression .\\(... && RecordElement<Ts>\\) \\[with Ts = \\{int.4.\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_throwing_move_part
  "template constraint failure for .template<class ... Ts>  requires \\(... && RecordElement<Ts>\\) struct foundation::core::Record."
  "the expression .\\(... && RecordElement<Ts>\\) \\[with Ts = \\{ThrowingMove\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_narrowing_init
  "narrowing conversion of .300. from .int. to .unsigned char."
  "neg_core_record_narrowing_init.cpp:10")
foundation_neg_compile_test(neg_core_record_from_other_parts
  "conversion from .Record<unsigned char, unsigned char>. to non-scalar type .Record<int, int>. requested"
  "neg_core_record_from_other_parts.cpp:10")
foundation_neg_compile_test(neg_core_record_binding_count
  "only 2 names provided for structured binding"
  "neg_core_record_binding_count.cpp:8")
foundation_neg_compile_test(neg_core_record_equal_needs_equal
  "no match for .operator==. \\(operand types are .const foundation::core::Record<int, NoEquality>. and .const foundation::core::Record<int, NoEquality>.\\)"
  "the expression .\\(RecordEqualPart<Ts> && ...\\) \\[with Ts = \\{int, NoEquality\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_equal_floating_part
  "no match for .operator==. \\(operand types are .const foundation::core::Record<int, double>. and .const foundation::core::Record<int, double>.\\)"
  "the expression .\\(RecordEqualPart<Ts> && ...\\) \\[with Ts = \\{int, double\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_equal_other_types
  "no match for .operator==. \\(operand types are .const foundation::core::Record<int, int>. and .const foundation::core::Record<long int, long int>.\\)"
  "neg_core_record_equal_other_types.cpp:9")
foundation_neg_compile_test(neg_core_record_order_other_category
  "no match for .operator<=>. \\(operand types are .const foundation::core::Record<int, SignedOrder>. and .const foundation::core::Record<int, SignedOrder>.\\)"
  "the expression .\\(RecordOrderedPart<Ts> && ...\\) \\[with Ts = \\{int, SignedOrder\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_record_at_of_rvalue
  "use of deleted function .void foundation::core::Record<Ts>::at\\(foundation::core::StaticIndex<I>\\) && \\[with long unsigned int I = 0. Ts = \\{int, long int\\}\\].: write auto \\[first, second\\] = std::move\\(record\\), because a move of one part drops the others"
  "neg_core_record_at_of_rvalue.cpp:11")
foundation_neg_compile_test(neg_core_record_at_out_of_range
  "no matching function for call to .foundation::core::Record<int, long int>::at\\(foundation::core::StaticIndex<2>\\)."
  "the expression .I < sizeof ... \\(Ts ...\\) \\[with I = 2. Ts = \\{int, long int\\}\\]. evaluated to .false.")
# The hook get<I> of a binding carries the annotation binding_hook, and the
# rule binding-hook of the plugin refuses a direct call of it.
foundation_neg_compile_test(neg_core_record_hook_lvalue_call
  "neg_core_record_hook_lvalue_call\\.cpp:9:[0-9]+: error: this call names the hook .foundation::core::get. of a structured binding, and only a structured binding calls the hook"
  "a direct call is a second form of the access to a part, and a call on an rvalue moves one part out")
foundation_neg_compile_test(neg_core_record_hook_rvalue_call
  "neg_core_record_hook_rvalue_call\\.cpp:11:[0-9]+: error: this call names the hook .foundation::core::get. of a structured binding, and only a structured binding calls the hook"
  "a direct call is a second form of the access to a part, and a call on an rvalue moves one part out")
foundation_neg_compile_test(neg_core_pack_size_not_a_pack
  "use of invalid variable template .pack_size_v<foundation::core::Record<int, long int> >."
  "required for the satisfaction of .PackType<P>. \\[with P = foundation::core::Record<int, long int>\\]")
foundation_neg_compile_test(neg_core_pack_concat_not_a_pack
  "template constraint failure for .template<class ... Ps>  requires \\(... && PackType<Ps>\\) using foundation::core::pack_concat_t"
  "the expression .\\(... && PackType<Ps>\\) \\[with Ps = \\{foundation::core::Pack<int>, long int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_pack_at_out_of_range
  "template constraint failure for .template<class ... Ts> template<long unsigned int I>  requires  I < sizeof ... \\(Ts ...\\) using foundation::core::detail::PackParts<foundation::core::Pack<Ts ...> >::at = typename foundation::core::detail::PackAt<I, Ts ...>::type."
  "the expression .I < sizeof ... \\(Ts ...\\) \\[with I = 2; Ts = \\{int, long int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_pack_index_of_absent
  "no matching function for call to .foundation::core::detail::PackParts<foundation::core::Pack<int, long int> >::index_of<char>\\(\\)."
  "the expression .\\(count<T>\\) == 1 \\[with Ts = \\{int, long int\\}; T = char\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_pack_index_of_twice
  "no matching function for call to .foundation::core::detail::PackParts<foundation::core::Pack<int, long int, int> >::index_of<int>\\(\\)."
  "the expression .\\(count<T>\\) == 1 \\[with Ts = \\{int, long int, int\\}; T = int\\]. evaluated to .false.")

# ── The Ref family: the allocation door and Box ───────────────────────
#
# The allocation door refuses a request of zero bytes and an alignment that
# is not a power of two.  A Box owns one object on the heap and comes only
# from mint_box, which takes the allocation capability.  A copy and a borrow
# of a temporary are refused.
foundation_test(test_core_heap)
foundation_test(test_core_ref)
foundation_test(test_core_identity)
foundation_neg_compile_test(neg_core_identity_ordered
  "no match for .operator<."
  "operand types are .const foundation::core::Identity. and .const foundation::core::Identity.")
foundation_neg_compile_test(neg_core_identity_from_integer
  "constexpr foundation::core::Identity::Identity\\([a-z_:0-9]+\\). is private within this context"
  "declared private here")
foundation_test(test_core_box)
# The test counts the calls of the allocation door: each reference of the
# link to the allocator of the C library goes to a wrapper of the test.
foundation_test(test_core_hole)
target_link_options(test_core_hole PRIVATE -Wl,--wrap=malloc,--wrap=aligned_alloc,--wrap=calloc,--wrap=free)

foundation_neg_compile_test(neg_core_ref_of_temporary
  "use of deleted function .void foundation::core::to\\(T&&\\) \\[with Refused = \\{\\}; T = int\\]."
  "a Ref of a temporary dangles at the end of the full expression")
foundation_neg_compile_test(neg_core_ref_drops_const
  "no matching function for call to .foundation::core::Ref<int, foundation::core::<lambda\\(\\)> >::Ref\\(<brace-enclosed initializer list>\\)."
  "required for the satisfaction of .RefWidens<U, UBrand, T, Brand>. \\[with U = const int; UBrand = [^;]+; T = int; ")
foundation_neg_compile_test(neg_core_ref_changes_brand
  "no matching function for call to .foundation::core::Ref<int, foundation::core::<lambda\\(\\)> >::Ref\\(<brace-enclosed initializer list>\\)."
  "required for the satisfaction of .RefWidens<U, UBrand, T, Brand>. \\[with U = int; UBrand = [^;]+; T = int; ")
foundation_neg_compile_test(neg_core_ref_names_brand
  "no matching function for call to .to<int, ArenaBrand>\\(int&\\)."
  "the expression .sizeof \\.\\.\\. \\(Refused \\.\\.\\.\\) == 0 \\[with Refused = \\{int, ArenaBrand\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_ref_array_target
  "template constraint failure for .template<class T, class Brand>  requires \\(RefTarget<T>\\) && \\(IsBrand<Brand>\\) class foundation::core::Ref."
  "the expression .!\\(is_array_v<T>\\) \\[with T = int\\[2\\]\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_ref_volatile_target
  "template constraint failure for .template<class T, class Brand>  requires \\(RefTarget<T>\\) && \\(IsBrand<Brand>\\) class foundation::core::Ref."
  "the expression .!\\(is_volatile_v<T>\\) \\[with T = volatile int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_address_alignment_not_power_of_two
  "no matching function for call to .foundation::core::Address::is_aligned<24>\\(\\)."
  "the expression .\\(Alignment & Alignment - 1\\) == 0 \\[with Alignment = 24\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_address_ordered
  "no match for .operator<."
  "operand types are .const foundation::core::Address. and .const foundation::core::Address.")
foundation_neg_compile_test(neg_core_address_of_pointer
  "no matching function for call to .address_of\\(int\\*\\)."
  "mismatched types .foundation::core::Ref<[A-Za-z]+, [A-Za-z]+>. and .int\\*.")
foundation_neg_compile_test(neg_core_ref_compared
  "no match for .operator==."
  "operand types are .const foundation::core::Ref<int, foundation::core::<lambda\\(\\)> >. and .const foundation::core::Ref<int, foundation::core::<lambda\\(\\)> >.")

foundation_neg_compile_test(neg_core_box_without_allocation_marker
  "no matching function for call to .mint_box<int>\\(foundation::effects::cap::IO, int\\)."
  "required for the satisfaction of .AllocationCapability<Marker>. \\[with Marker = foundation::effects::cap::IO\\]")
foundation_neg_compile_test(neg_core_box_homemade_marker
  "no matching function for call to .mint_box<int>\\(HomemadeMarker, int\\)."
  "required for the satisfaction of .AllocationCapability<Marker>. \\[with Marker = HomemadeMarker\\]")
foundation_neg_compile_test(neg_core_box_not_constructible
  "no matching function for call to .mint_box<int>\\(foundation::core::Alloc, const char \\[6\\]\\)."
  ".int. is not constructible from .const char \\(&\\)\\[6\\].")
foundation_neg_compile_test(neg_core_box_array_payload
  "template constraint failure for .template<class T>  requires  BoxPayload<T> class foundation::core::Box."
  "the expression .!\\(is_array_v<T>\\) \\[with T = int\\[4\\]\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_box_copied
  "use of deleted function .foundation::core::Box<T>::Box\\(const foundation::core::Box<T>&\\) \\[with T = int\\]."
  "a Box owns its object alone: a copy would free the object two times")
foundation_neg_compile_test(neg_core_box_borrow_of_temporary
  "use of deleted function .void foundation::core::Box<T>::view\\(\\) && \\[with T = int\\]."
  "a borrow of a temporary Box dangles at the end of the full expression")
foundation_neg_compile_test(neg_core_box_consume_lvalue
  "passing .foundation::core::Box<int>. as .this. argument discards qualifiers"
  "in call to .T foundation::core::Box<T>::consume\\(\\) && requires  is_nothrow_move_constructible_v<T> \\[with T = int\\].")
foundation_neg_compile_test(neg_core_box_consume_not_movable
  "no matching function for call to .foundation::core::Box<Fixed>::consume\\(\\)."
  "the expression .is_nothrow_move_constructible_v<T> \\[with T = Fixed\\]. evaluated to .false.")

# A Hole holds the storage of one object with no object in it.  Its mint
# from the allocation door takes the allocation tag and a shape of a power
# of two.  Its mint from a Box takes a Box that the caller gives up.  A Box
# builds in a Hole of its exact size and alignment that the caller gives up.
foundation_neg_compile_test(neg_core_hole_without_allocation_tag
  "no matching function for call to .mint_hole<8, 8>\\(foundation::effects::cap::IO\\)."
  "required for the satisfaction of .CanMintHole<Size, Align, Marker>. \\[with Size = 8; Align = 8; Marker = foundation::effects::cap::IO\\]")
foundation_neg_compile_test(neg_core_hole_alignment_not_power
  "no matching function for call to .mint_hole<24, 12>\\(foundation::core::Alloc\\)."
  "the expression .\\(Align & Align - 1\\) == 0 \\[with Align = 12\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_hole_of_lvalue_box
  "no matching function for call to .mint_hole\\(foundation::core::Box<long unsigned int>&\\)."
  "cannot bind rvalue reference of type .foundation::core::Box<long unsigned int>&&. to lvalue of type .foundation::core::Box<long unsigned int>.")
foundation_neg_compile_test(neg_core_hole_of_const_box
  "no matching function for call to .mint_hole\\(std::remove_reference<const foundation::core::Box<long unsigned int>&>::type\\)."
  "have incompatible cv-qualifiers")
foundation_neg_compile_test(neg_core_box_in_hole_of_other_size
  "no matching function for call to .mint_box<uint64_t>\\(std::remove_reference<foundation::core::Hole<16, 8>&>::type, uint64_t\\)."
  "the expression .Size == sizeof \\(T\\) \\[with Size = 16; T = long unsigned int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_box_in_hole_of_other_alignment
  "no matching function for call to .mint_box<TwoWords>\\(std::remove_reference<foundation::core::Hole<8, 8>&>::type\\)."
  "the expression .Align == alignof \\(T\\) \\[with Align = 8; T = TwoWords\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_box_in_lvalue_hole
  "no matching function for call to .mint_box<uint64_t>\\(foundation::core::Hole<8, 8>&, uint64_t\\)."
  "cannot bind rvalue reference of type .foundation::core::Hole<8, 8>&&. to lvalue of type .foundation::core::Hole<8, 8>.")
foundation_neg_compile_test(neg_core_hole_forged_box
  "redefinition of .class foundation::core::Box<T>."
  "previous definition of .class foundation::core::Box<T>.")
foundation_neg_compile_test(neg_core_hole_copied
  "use of deleted function .foundation::core::Hole<Size, Align>::Hole\\(const foundation::core::Hole<Size, Align>&\\) \\[with long unsigned int Size = 8; long unsigned int Align = 8\\]."
  "a Hole owns its storage alone: a copy would free the storage two times")

foundation_neg_compile_test(neg_core_box_mint_forged_body
  "no matching function for call to .foundation::core::Box<int>::Box\\(<brace-enclosed initializer list>\\)."
  "cannot convert .& stolen. \\(type .int\\*.\\) to type .const foundation::core::detail::BoxKey&.")
foundation_neg_compile_test(neg_core_address_of_forged_body
  "no matching function for call to .foundation::core::Address::Address\\(<brace-enclosed initializer list>\\)."
  "candidate expects 2 arguments, 1 provided")
foundation_neg_compile_test(neg_core_box_view_brand_by_address
  "no matches converting function .view. to type .class foundation::core::Ref<int, HeldBrand> \\(class foundation::core::Box<int>::\\*\\)\\(\\) & noexcept."
  "foundation::core::Ref<T, typename foundation::brand::undeducible_brand<Fresh>::type> foundation::core::Box<T>::view\\(\\) &")
foundation_neg_compile_test(neg_core_box_const_view_brand_by_address
  "no matches converting function .view. to type .class foundation::core::Ref<const int, HeldBrand> \\(class foundation::core::Box<int>::\\*\\)\\(\\) const & noexcept."
  "foundation::core::Ref<const T, typename foundation::brand::undeducible_brand<Fresh>::type> foundation::core::Box<T>::view\\(\\) const &")

# ── The Atomic family ─────────────────────────────────────────────────
#
# An Atomic holds a lock-free value with one bit pattern for each value, or
# the word of a value that declares one, and each operation names its memory
# order.  The runtime test runs four threads, and foundation starts no thread,
# so test/fixy holds it.
#
# Each gate of a value is a disjunction of the three roads of a value.  At
# the default depth, GCC names the gate that refuses the value, and it gives
# the part that the value misses only at depth 2.  So each fixture of the
# value gate compiles at depth 2.

set(CRUCIBLE_ATOMIC_GATE_FIXTURES
  neg_core_atomic_padded_payload neg_core_atomic_float_payload neg_core_atomic_not_lock_free
  neg_core_atomic_word_without_key neg_core_atomic_word_incomplete neg_core_atomic_word_without_empty_word)
foundation_neg_compile_test(neg_core_atomic_padded_payload
  "required for the satisfaction of .OtherValueHasOwnBits<T>. \\[with T = Padded\\]"
  "the expression .has_unique_object_representations_v<T> \\[with T = Padded\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_atomic_float_payload
  "required for the satisfaction of .OtherValueHasOwnBits<T>. \\[with T = float\\]"
  "the expression .has_unique_object_representations_v<T> \\[with T = float\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_atomic_not_lock_free
  "required for the satisfaction of .OtherValueHasOwnBits<T>. \\[with T = WidePhase\\]"
  "the expression .__atomic_always_lock_free\\(long unsigned int, const volatile void\\*\\)\\(sizeof \\(T\\), 0\\) \\[with T = WidePhase\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_atomic_word_without_key
  "required for the satisfaction of .DeclaredWordIsComplete<T>. \\[with T = .*Seat\\]"
  "the required expression .foundation::core::atomic_word<T>::from_word\\(key, word\\). is invalid")
foundation_neg_compile_test(neg_core_atomic_word_incomplete
  "required for the satisfaction of .DeclaredWordIsComplete<T>. \\[with T = .*Seat\\]"
  "the required expression .foundation::core::atomic_word<T>::to_word\\(value\\). is invalid")
foundation_neg_compile_test(neg_core_atomic_word_without_empty_word
  "required for the satisfaction of .DeclaredWordIsComplete<T>. \\[with T = .*Seat\\]"
  ".empty_word. is not a member of .foundation::core::atomic_word<.*Seat>.")
foreach(_fixture IN LISTS CRUCIBLE_ATOMIC_GATE_FIXTURES)
  set_property(TEST ${_fixture} APPEND PROPERTY ENVIRONMENT "CRUCIBLE_NEG_EXTRA_FLAGS=-fconcepts-diagnostics-depth=2")
endforeach()
foundation_neg_compile_test(neg_core_atomic_fetch_add_enum
  "no matching function for call to .foundation::core::Atomic<Phase>::fetch_add_acq_rel\\(Phase\\)."
  "the expression .is_integral_v<_Tp> \\[with _Tp = Phase\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_atomic_fetch_or_signed
  "no matching function for call to .foundation::core::Atomic<int>::fetch_or_acq_rel\\(int\\)."
  "required for the satisfaction of .AtomicBits<T>. \\[with T = int\\]")
foundation_neg_compile_test(neg_core_atomic_copied
  "use of deleted function .foundation::core::Atomic<int>::Atomic\\(const foundation::core::Atomic<int>&\\)."
  "Pinned<T>: stable address")
foundation_neg_compile_test(neg_core_cache_line_borrow_of_temporary
  "use of deleted function .T& foundation::core::CacheLine<T>::operator\\*\\(\\) && \\[with T = int\\]."
  "a borrow of a temporary CacheLine dangles at the end of the full expression")
foundation_neg_compile_test(neg_core_cache_line_arrow_of_temporary
  "use of deleted function .T\\* foundation::core::CacheLine<T>::operator->\\(\\) && \\[with T = foundation::core::Tally\\]."
  "a borrow of a temporary CacheLine dangles at the end of the full expression")
foundation_neg_compile_test(neg_core_cache_line_get
  "has no member named .get."
  ".class foundation::core::CacheLine<int>.")
foundation_neg_compile_test(neg_core_sole_writer_mint_role_not_class
  "no matching function for call to .mint_sole_writer<int>\\(\\)."
  "the expression .is_class_v<Role> \\[with Role = int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_sole_writer_mint_named_brand
  "no matching function for call to .mint_sole_writer<.*HeadRole, HeldBrand>\\(\\)."
  "the expression .sizeof ... \\(Arguments ...\\) == 0 \\[with Arguments = .*\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_sole_writer_bound_to_held_brand
  "invalid initialization of non-const reference of type .foundation::core::SoleWriter<\\{anonymous\\}::HeadRole, foundation::core::<lambda\\(\\)> > \\(&\\)\\(\\) noexcept. from an rvalue of type .<unresolved overloaded function type>."
  "neg_core_sole_writer_bound_to_held_brand\\.cpp:18:[0-9]+: error:")
foundation_neg_compile_test(neg_core_sole_writer_forged_specialization
  "template-id .mint_sole_writer<\\{anonymous\\}::OtherRole>. for .constexpr foundation::core::SoleWriter<\\{anonymous\\}::OtherRole, foundation::core::<lambda\\(\\)> > foundation::core::mint_sole_writer\\(\\). does not match any template declaration"
  "candidate is: .template<class Role, class ... Arguments, class Brand>  requires  SoleWriterMint<Role, Arguments ...> foundation::core::SoleWriter<Role, typename foundation::brand::undeducible_brand<Brand>::type> foundation::core::mint_sole_writer\\(\\).")
foundation_neg_compile_test(neg_core_sole_writer_copied
  "use of deleted function .foundation::core::SoleWriter<Role, Brand>::SoleWriter\\(const foundation::core::SoleWriter<Role, Brand>&\\)"
  "SoleWriter: a copy gives the cell a second writer")
foundation_neg_compile_test(neg_core_sole_writer_forged_key
  ".constexpr foundation::core::sole_writer_key::sole_writer_key\\(\\). is private within this context"
  "declared private here")
foundation_neg_compile_test(neg_core_sole_writer_store_without_proof
  "no matching function for call to .foundation::core::Atomic<long unsigned int, foundation::core::SoleWriter<.*HeadRole, .*> >::store_release\\(int\\)."
  "candidate expects 2 arguments, 1 provided")
foundation_neg_compile_test(neg_core_sole_writer_other_role
  "cannot convert .foundation::core::SoleWriter<.*TailRole, .*>. to .const foundation::core::SoleWriter<.*HeadRole, .*>&."
  "initializing argument 1 of .void foundation::core::Atomic<T, foundation::core::SoleWriter<Role, Identity> >::store_release")
foundation_neg_compile_test(neg_core_atomic_load_owned_shared
  ".class foundation::core::Atomic<long unsigned int>. has no member named .load_owned."
  "has no member named .load_owned.")
foundation_neg_compile_test(neg_core_sole_writer_default_brand
  "the brand is the identity of one writer.  DefaultBrand would give each writer of the role one type"
  "static assertion failed")
foundation_neg_compile_test(neg_core_sole_writer_exchange
  ".class foundation::core::Atomic<long unsigned int, foundation::core::SoleWriter<.*HeadRole, .*> >. has no member named .exchange_acq_rel."
  "has no member named .exchange_acq_rel.")
foundation_neg_compile_test(neg_core_atomic_writer_not_sole_writer
  "the writer is void, for a cell that each thread can write, or SoleWriter<Role, Brand>, for a cell of one writer"
  "static assertion failed")

# ── The Scalar family: decode and enum_from ───────────────────────────
#
# decode gives a value only when each bool byte is 0 or 1 and each
# enumeration value names an enumerator.  The two types have one size, the
# source has no padding, and a part whose bytes are not its whole value is
# refused: an address, a union, a bit-field, an empty class and a sealed
# class.  enum_from refuses an enumeration with no enumerator.
foundation_test(test_core_scalar)
# Each operation of the family gives the answer of its std form for each
# input that the std form defines.
foundation_test(test_core_scalar_oracle)

foundation_neg_compile_test(neg_core_decode_pointer_member
  "no matching function for call to .decode<HoldsAddress>\\(uint64_t\\)."
  "the expression .is_plain_at\\(.*\\(\\^\\^ To, 0\\) \\[with To = HoldsAddress\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_decode_union
  "no matching function for call to .decode<PhaseOrByte>\\(uint8_t\\)."
  "the expression .is_plain_at\\(.*\\(\\^\\^ To, 0\\) \\[with To = PhaseOrByte\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_decode_size_mismatch
  "no matching function for call to .decode<uint32_t>\\(uint16_t\\)."
  "the expression .sizeof \\(To\\) == sizeof \\(From\\) \\[with To = unsigned int; From = short unsigned int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_decode_padded_source
  "no matching function for call to .decode<uint64_t>\\(Padded\\)."
  "the expression .has_no_padding_bit_at\\(.*\\(\\^\\^ From\\) \\[with From = Padded\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_bit_cast_bool
  "no matching function for call to .bit_cast<bool>\\(uint8_t\\)."
  "the expression .!has_checked_leaf_at\\(.*\\(\\^\\^ T\\) \\[with T = bool\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_bit_cast_enumeration
  "no matching function for call to .bit_cast<Phase>\\(uint8_t\\)."
  "the expression .!has_checked_leaf_at\\(.*\\(\\^\\^ T\\) \\[with T = Phase\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_bit_cast_niche
  "no matching function for call to .bit_cast<NicheWord>\\(uint32_t\\)."
  "the expression .!has_checked_leaf_at\\(.*\\(\\^\\^ T\\) \\[with T = NicheWord\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_enum_from_no_enumerator
  "no matching function for call to .enum_from<Strong>\\(int\\)."
  "the expression .has_enumerators\\(.*\\(\\^\\^ E\\) \\[with E = Strong\\]. evaluated to .false.")

# The gates of a number and the arithmetic.  bool, a character type and
# long double are no number of the family.  The two operands of an
# operation have one type, and widening_mul takes unsigned operands.
foundation_test(test_core_scalar_arith)

foundation_neg_compile_test(neg_core_scalar_max_of_bool
  "use of invalid variable template .max_of<bool>."
  "required for the satisfaction of .Arithmetic<T>. \\[with T = bool\\]")
foundation_neg_compile_test(neg_core_scalar_max_of_char
  "use of invalid variable template .max_of<char>."
  "required for the satisfaction of .Arithmetic<T>. \\[with T = char\\]")
foundation_neg_compile_test(neg_core_scalar_epsilon_of_long_double
  "use of invalid variable template .epsilon_of<long double>."
  "required for the satisfaction of .Float<T>. \\[with T = long double\\]")
foundation_neg_compile_test(neg_core_scalar_epsilon_of_integer
  "use of invalid variable template .epsilon_of<int>."
  "required for the satisfaction of .Float<T>. \\[with T = int\\]")
foundation_neg_compile_test(neg_core_scalar_checked_add_char
  "no matching function for call to .checked_add\\(char, char\\)."
  "the expression .!\\(same_as<T, char>\\) \\[with T = char\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_checked_add_mixed
  "no matching function for call to .checked_add\\(int, unsigned int\\)."
  "deduced conflicting types for parameter .T. \\(.int. and .unsigned int.\\)")
foundation_neg_compile_test(neg_core_scalar_gcd_float
  "no matching function for call to .gcd\\(double, double\\)."
  "required for the satisfaction of .Integer<T>. \\[with T = double\\]")
foundation_neg_compile_test(neg_core_scalar_widening_mul_signed
  "no matching function for call to .widening_mul\\(int, int\\)."
  "the expression .is_unsigned_v<T> \\[with T = int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_wide_product_signed
  "template constraint failure for .template<class T>  requires  UnsignedInteger<T> struct foundation::core::WideProduct."
  "the expression .is_unsigned_v<T> \\[with T = int\\]. evaluated to .false.")

# Compare, select and convert.  compare takes two integers, a Bounds comes
# only from its two doors, widen takes only a pair whose values all fit, and
# narrow gives an integer.
foundation_test(test_core_scalar_convert)

foundation_neg_compile_test(neg_core_scalar_compare_float
  "no matching function for call to .compare\\(double, double\\)."
  "required for the satisfaction of .Integer<Left>. \\[with Left = double\\]")
foundation_neg_compile_test(neg_core_scalar_compare_bool
  "no matching function for call to .compare\\(bool, int\\)."
  "the expression .!\\(same_as<T, bool>\\) \\[with T = bool\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_bounds_swapped
  "no matching function for call to .bounds<10, 0>\\(\\)."
  "the expression .Lo <= Hi \\[with Lo = 10; Hi = 0\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_bounds_nan
  "no matching function for call to .bounds<foundation::core::quiet_nan_of<double>, 1.0e\\+0>\\(\\)."
  "the expression .Lo <= Hi \\[with Lo = \\+QNaN; Hi = 1.0e\\+0\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_bounds_constructor
  ".constexpr foundation::core::Bounds<T>::Bounds\\(T, T\\) \\[with T = int\\]. is private within this context"
  "neg_core_scalar_bounds_constructor.cpp:8:")
foundation_neg_compile_test(neg_core_scalar_bounds_decode
  "no matching function for call to .decode<foundation::core::Bounds<int> >\\(const uint64_t&\\)."
  "the expression .is_plain_at\\(.*\\(\\^\\^ To, 0\\) \\[with To = foundation::core::Bounds<int>\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_widen_signed_to_unsigned
  "no matching function for call to .widen<unsigned int>\\(int\\)."
  "the expression .fits_each_value_in\\(.*\\)\\(\\{Float<From>, is_signed_v<From>, sizeof \\(From\\)\\}, \\{Float<To>, is_signed_v<To>, sizeof \\(To\\)\\}\\) \\[with From = int; To = unsigned int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_widen_to_narrower
  "no matching function for call to .widen<uint8_t>\\(unsigned int\\)."
  "the expression .fits_each_value_in\\(.*\\)\\(\\{Float<From>, is_signed_v<From>, sizeof \\(From\\)\\}, \\{Float<To>, is_signed_v<To>, sizeof \\(To\\)\\}\\) \\[with From = unsigned int; To = unsigned char\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_narrow_to_float
  "no matching function for call to .narrow<double>\\(int\\)."
  "required for the satisfaction of .Integer<To>. \\[with To = double\\]")
foundation_neg_compile_test(neg_core_scalar_unsigned_abs_unsigned
  "no matching function for call to .unsigned_abs\\(unsigned int\\)."
  "the expression .is_signed_v<T> \\[with T = unsigned int\\]. evaluated to .false.")

# The bit operations.  Each operation other than byteswap takes an unsigned
# value, and the Integer gate refuses bool.
foundation_test(test_core_scalar_bits)

foundation_neg_compile_test(neg_core_scalar_popcount_signed
  "no matching function for call to .popcount\\(int\\)."
  "the expression .is_unsigned_v<T> \\[with T = int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_rotl_bool
  "no matching function for call to .rotl\\(bool, int\\)."
  "the expression .!\\(same_as<T, bool>\\) \\[with T = bool\\]. evaluated to .false.")

# The operations of a floating value.  Each one takes float or double, so
# long double and an integer do not compile.
foundation_test(test_core_scalar_float)

foundation_neg_compile_test(neg_core_scalar_fp_long_double
  "no matching function for call to .is_nan\\(long double\\)."
  "required for the satisfaction of .Float<T>. \\[with T = long double\\]")
foundation_neg_compile_test(neg_core_scalar_fp_sqrt_integer
  "no matching function for call to .sqrt\\(int\\)."
  "required for the satisfaction of .Float<T>. \\[with T = int\\]")

# exchange and swap move a value with no throw.  A Pinned type does not
# move, and a const object cannot take a value.
foundation_test(test_core_scalar_values)

foundation_neg_compile_test(neg_core_scalar_exchange_pinned
  "no matching function for call to .exchange\\(Anchored&, Anchored\\)."
  "the expression .is_nothrow_move_constructible_v<T> \\[with T = Anchored\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_swap_const
  "no match for call to .\\(const foundation::core::detail::SwapValues\\) \\(const int&, const int&\\)."
  "required for the satisfaction of .Exchangeable<T>. \\[with T = const int\\]")

# An Id holds an unsigned word of a tag class, with no default constructor
# and no arithmetic.  Its highest word is the niche of its Option.
foundation_test(test_core_scalar_id)

foundation_neg_compile_test(neg_core_scalar_id_signed_word
  "template constraint failure for .template<class Tag, class Word>  requires \\(UnsignedInteger<Word>\\) && \\(is_class_v<Tag>\\) class foundation::core::Id."
  "required for the satisfaction of .UnsignedInteger<Word>. \\[with Word = int\\]")
foundation_neg_compile_test(neg_core_scalar_id_tag_not_a_class
  "template constraint failure for .template<class Tag, class Word>  requires \\(UnsignedInteger<Word>\\) && \\(is_class_v<Tag>\\) class foundation::core::Id."
  "the expression .is_class_v<Tag> \\[with Tag = int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_scalar_id_arithmetic
  "no match for .operator\\+. \\(operand types are .const SlotId. \\{aka .const foundation::core::Id<SlotTag, unsigned int>.\\} and .const SlotId."
  "neg_core_scalar_id_arithmetic.cpp:12:")
foundation_neg_compile_test(neg_core_scalar_id_default
  "no matching function for call to .foundation::core::Id<SlotTag, unsigned int>::Id\\(\\)."
  "neg_core_scalar_id_default.cpp:11:")

# A Quantity holds an Integer in one unit of namespace unit.  Two units do
# not meet in one operation, and a conversion keeps the dimension.
foundation_test(test_core_scalar_quantity)

foundation_neg_compile_test(neg_core_scalar_quantity_not_a_unit
  "template constraint failure for .template<class UnitTag, class Rep>  requires \\(QuantityUnit<UnitTag>\\) && \\(Integer<Rep>\\) class foundation::core::Quantity."
  "required for the satisfaction of .QuantityUnit<UnitTag>. \\[with UnitTag = Kibibyte\\]")
foundation_neg_compile_test(neg_core_scalar_quantity_float_rep
  "template constraint failure for .template<class UnitTag, class Rep>  requires \\(QuantityUnit<UnitTag>\\) && \\(Integer<Rep>\\) class foundation::core::Quantity."
  "required for the satisfaction of .Integer<Rep>. \\[with Rep = double\\]")
foundation_neg_compile_test(neg_core_scalar_quantity_two_units
  "no matching function for call to .checked_add\\(const foundation::core::Quantity<foundation::core::unit::Byte, long unsigned int>&, const foundation::core::Quantity<foundation::core::unit::Element, long unsigned int>&\\)."
  "deduced conflicting types for parameter .UnitTag. \\(.foundation::core::unit::Byte. and .foundation::core::unit::Element.\\)")
foundation_neg_compile_test(neg_core_scalar_quantity_two_dimensions
  "no matching function for call to .convert<foundation::core::unit::Byte>\\(const foundation::core::Quantity<foundation::core::unit::Second, long int>&\\)."
  "required for the satisfaction of .ConvertibleUnits<FromUnit, ToUnit>. \\[with FromUnit = foundation::core::unit::Second; ToUnit = foundation::core::unit::Byte\\]")

# ── The Region family: View, copy and fill ────────────────────────────
#
# A View borrows a run of objects with a fixed or a dynamic extent.  A
# const is never dropped, and copy and fill write only an element whose
# assignment is a byte copy.  test/fixy/test_core_region.cpp runs the
# operations over a mapping, because a mapping is the owner of a View.
foundation_neg_compile_test(neg_core_view_reference_element
  "template constraint failure for .template<class T, long unsigned int Extent>  requires \\(ViewElement<T>\\) && \\(ViewExtent<T, Extent>\\) class foundation::core::View."
  "the expression .is_object_v<T> \\[with T = int&\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_view_zero_extent
  "template constraint failure for .template<class T, long unsigned int Extent>  requires \\(ViewElement<T>\\) && \\(ViewExtent<T, Extent>\\) class foundation::core::View."
  "required for the satisfaction of .ViewExtent<T, Extent>. \\[with T = int; Extent = 0\\]")
foundation_neg_compile_test(neg_core_view_mutable_from_const
  "conversion from .View<const int>. to non-scalar type .View<int>. requested"
  "In function .void write_through\\(foundation::core::View<const int>\\).")
foundation_neg_compile_test(neg_core_copy_extent_mismatch
  "no matching function for call to .copy\\(foundation::core::View<int, 4>&, foundation::core::View<const int, 8>&\\)."
  "In function .void copy_four_from_eight\\(foundation::core::View<int, 4>, foundation::core::View<const int, 8>\\).")
foundation_neg_compile_test(neg_core_copy_into_const
  "no matching function for call to .copy\\(foundation::core::View<const int>&, foundation::core::View<const int>&\\)."
  "required for the satisfaction of .RegionCopyElement<T>. .with T = const int.")
foundation_neg_compile_test(neg_core_copy_not_byte_copyable
  "no matching function for call to .copy\\(foundation::core::View<Counted>&, foundation::core::View<const Counted>&\\)."
  "the expression .is_trivially_copyable_v<T> \\[with T = Counted\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_copy_result_discarded
  "ignoring return value of .foundation::core::Result<foundation::core::Unit, foundation::core::LengthMismatch> foundation::core::copy"
  "declared with attribute .nodiscard.")
foundation_neg_compile_test(neg_core_fill_const
  "no matching function for call to .fill\\(foundation::core::View<const int>&, int\\)."
  "required for the satisfaction of .RegionCopyElement<T>. .with T = const int.")

# An Index<N> proves a number below N: only its check, a constant index
# below N and an index of a smaller count make one.  A FixedArray fills each
# element, reads only at a proven index, and refuses a view of a temporary.
# The reads of a TextView live in Region.h, and the free operations refuse
# a predicate that can throw, a key with no total order and the bytes of a
# padded or a temporary object.
foundation_test(test_core_array)
foundation_test(test_core_algorithms)
foundation_test(test_core_iteration)

foundation_neg_compile_test(neg_core_view_windows_zero_width
  "no matching function for call to .foundation::core::View<int>::windows<0>\\(\\)."
  "required for the satisfaction of .ViewExtent<T, Width>. \\[with T = int; Width = 0\\]")
foundation_neg_compile_test(neg_core_view_windows_past_extent
  "no matching function for call to .foundation::core::View<int, 4>::windows<5>\\(\\)."
  "constexpr auto foundation::core::View<T, Extent>::windows\\(\\) const \\[with long unsigned int Width = Width; T = int; long unsigned int Extent = 4\\].")
foundation_neg_compile_test(neg_core_fixed_array_reverse_of_temporary
  "use of deleted function .void foundation::core::FixedArray<T, N>::reverse\\(\\) const && \\[with T = int; long unsigned int N = 4\\]."
  "a loop over a temporary FixedArray points into an object that the full expression ends")
foundation_neg_compile_test(neg_core_fixed_array_chunks_of_temporary
  "use of deleted function .void foundation::core::FixedArray<T, N>::chunks\\(\\) const && \\[with long unsigned int Width = 2; T = int; long unsigned int N = 4\\]."
  "a loop over a temporary FixedArray points into an object that the full expression ends")

foundation_neg_compile_test(neg_core_index_zero_count
  "template constraint failure for .template<long unsigned int Count>  requires  IndexCount<Count> class foundation::core::Index."
  "the expression .Count > 0 \\[with Count = 0\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_index_narrowing
  "could not convert .wide. from .Index<8>. to .Index<4>."
  "In function .foundation::core::Index<4> narrow\\(foundation::core::Index<8>\\).")
foundation_neg_compile_test(neg_core_index_static_past_count
  "could not convert .foundation::core::index<4>\\(\\). from .foundation::core::StaticIndex<4>. to .foundation::core::Index<4>."
  "In function .foundation::core::Index<4> past_the_end\\(\\).")
foundation_neg_compile_test(neg_core_fixed_array_partial_list
  "no matching function for call to .foundation::core::FixedArray<int, 3>::FixedArray\\(<brace-enclosed initializer list>\\)."
  "ArrayStorage\\(foundation::core::detail::ArrayParameter<T, Positions>\\.\\.\\.\\) requires  is_nothrow_move_constructible_v<T> \\[with T = int; long unsigned int N = 3; long unsigned int \\.\\.\\.Positions = \\{0, 1, 2\\}\\]."
  "candidate expects 3 arguments, 2 provided")
foundation_neg_compile_test(neg_core_fixed_array_view_of_temporary
  "use of deleted function .void foundation::core::FixedArray<T, N>::view\\(\\) && \\[with T = int; long unsigned int N = 4\\]."
  "a view of a temporary FixedArray points into an object that the full expression ends")
foundation_neg_compile_test(neg_core_fixed_array_subscript
  "no match for .operator\\[\\]. \\(operand types are .const foundation::core::FixedArray<int, 4>. and .std::size_t."
  "In function .int unchecked_read\\(const foundation::core::FixedArray<int, 4>&, std::size_t\\).")
foundation_neg_compile_test(neg_core_fixed_array_at_wider_index
  "no matching function for call to .foundation::core::FixedArray<int, 4>::at\\(foundation::core::Index<8>&\\) const."
  "In function .int read_past\\(const foundation::core::FixedArray<int, 4>&, foundation::core::Index<8>\\).")
foundation_neg_compile_test(neg_core_text_window_without_region
  "use of .constexpr auto foundation::core::TextView::window\\(std::size_t, std::size_t\\) const. before deduction of .auto."
  "In function .bool has_window\\(foundation::core::TextView\\).")
foundation_neg_compile_test(neg_core_bytes_of_padded
  "no matching function for call to .bytes\\(const Padded&\\)."
  "the expression .has_unique_object_representations_v<T> \\[with T = Padded\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_bytes_of_temporary
  "use of deleted function .void foundation::core::bytes\\(const T&&\\) \\[with T = long unsigned int\\]."
  "the bytes of a temporary object dangle at the end of the full expression")
foundation_neg_compile_test(neg_core_find_if_throwing_predicate
  "no matching function for call to .find_if\\(foundation::core::View<const int, 4>, has_large\\(const foundation::core::FixedArray<int, 4>&\\)::<lambda\\(int\\)>\\)."
  "the expression .is_nothrow_invocable_v<Predicate&, const T&> \\[with Predicate = has_large::._anon_[0-9]+; T = int\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_lower_bound_float_key
  "no matching function for call to .lower_bound_by_key\\(foundation::core::View<const float, 4>, bound_of\\(const foundation::core::FixedArray<float, 4>&\\)::<lambda\\(float\\)>, float\\)."
  "required for the satisfaction of .KeyOf<Key, typename std::remove_const<_Tp>::type>. \\[with Key = bound_of::._anon_[0-9]+; T = const float\\]")

# get of a View and of an owner gives an Option of a Ref: Array.h defines
# it, so Region.h alone cannot call it, and get of a temporary owner is
# refused.  A HeapArray comes only from mint_heap_array, which takes the
# allocation capability, an element that copies and an alignment that is a
# power of two.  It does not copy, and a view of a temporary is refused.
foundation_test(test_core_heap_array)

# A FixedList holds at most N elements, and a push into a full list gives
# Full, which holds the value of a linear element.  Each element ends one
# time, and the model of a FixedArray and a count agrees with each step.
foundation_test(test_core_list)

# The branded scope gives proven indexes of a View of a dynamic extent: each
# index reads the element of the model, and a cursor never leaves its loop.
foundation_test(test_core_branded)

# The mint refuses a body whose result holds the brand and an explicit
# template argument, and an index of one scope is no index of another.
foundation_neg_compile_test(neg_core_branded_scope_gives_index
  "no matching function for call to .mint_branded_scope\\(foundation::core::View<int, 4>, main\\(\\)::<lambda\\(auto:7\\)>\\)."
  "template argument deduction/substitution failed")
foundation_neg_compile_test(neg_core_branded_scope_names_brand
  "no matching function for call to .mint_branded_scope<int>\\(foundation::core::View<int, 4>, main\\(\\)::<lambda\\(auto:7\\)>\\)."
  "the expression .sizeof \\.\\.\\. \\(Refused \\.\\.\\.\\) == 0 \\[with Refused = \\{int\\}\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_branded_index_of_other_scope
  "cannot convert .BrandedIndex<foundation::core::<lambda\\(\\)>>. to .BrandedIndex<foundation::core::<lambda\\(\\)>>."
  "initializing argument 1 of .constexpr T& foundation::core::BrandedView<T, Brand>::at\\(foundation::core::BrandedIndex<Brand>\\) const \\[with T = int; Brand = foundation::core::<lambda\\(\\)>\\].")

# The brand of a branded scope is a scope brand, so no value of it outlives a
# call of the mint: the rule brand-escape refuses a static local of the body
# that keeps an index for the next call, and a result that holds an index in a
# capture of a closure, which the gate of the mint does not see.
foundation_neg_compile_test(neg_branded_scope_static_local
  "an object of static or thread storage duration holds a value of a scope brand, so the value outlives the call of its mint"
  "KeepsLastIndex::operator\\(\\): kept")
foundation_neg_compile_test(neg_branded_scope_returns_closure
  "a scope mint gives a result that holds its own scope brand"
  "foundation::core::mint_branded_scope: the result .anonymous.::ReturnsClosure::operator\\(\\)")

# A push into or a view of a temporary list is refused, and a list of a
# linear element has no clear().
foundation_neg_compile_test(neg_core_fixed_list_push_of_temporary
  "use of deleted function .void foundation::core::FixedList<T, N>::push\\(T\\) && \\[with T = int; long unsigned int N = 4\\]."
  "a push into a temporary FixedList gives a Ref that dangles at the end of the full expression")
foundation_neg_compile_test(neg_core_fixed_list_clear_of_linear
  "no matching function for call to .foundation::core::FixedList<Token, 2>::clear\\(\\)."
  "required for the satisfaction of .DroppablePayload<T>. \\[with T = Token\\]")
foundation_neg_compile_test(neg_core_fixed_list_view_of_temporary
  "use of deleted function .void foundation::core::FixedList<T, N>::view\\(\\) && \\[with T = int; long unsigned int N = 4\\]."
  "a view of a temporary FixedList points into an object that the full expression ends")

# Region.h defines the body of get of a View, so a unit that includes only
# Region.h cannot give it a body of its own.
foundation_neg_compile_test(neg_core_view_get_forged_body
  "redefinition of .template<class Brand, class T>  requires  IsBrand<Brand> constexpr auto foundation::core::detail::element_at_\\(T\\*, std::size_t, std::size_t\\)."
  "previously declared here")
foundation_neg_compile_test(neg_core_fixed_array_get_of_temporary
  "use of deleted function .void foundation::core::FixedArray<T, N>::get\\(std::size_t\\) && \\[with T = int; long unsigned int N = 4; std::size_t = long unsigned int\\]."
  "an element of a temporary FixedArray dangles at the end of the full expression")
foundation_neg_compile_test(neg_core_heap_array_without_allocation_marker
  "no matching function for call to .mint_heap_array\\(foundation::effects::cap::IO, int, int\\)."
  "required for the satisfaction of .AllocationCapability<Marker>. \\[with Marker = foundation::effects::cap::IO\\]")
foundation_neg_compile_test(neg_core_heap_array_fill_not_copyable
  "no matching function for call to .mint_heap_array\\(foundation::core::Alloc, int, const MoveOnly&\\)."
  "the expression .is_nothrow_copy_constructible_v<T> \\[with T = MoveOnly\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_heap_array_alignment_not_power
  "no matching function for call to .mint_heap_array<int, 12>\\(foundation::core::Alloc, int, int\\)."
  "the expression .\\(Align & Align - 1\\) == 0 \\[with Align = 12\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_heap_array_copied
  "use of deleted function .foundation::core::HeapArray<T, Align>::HeapArray\\(const foundation::core::HeapArray<T, Align>&\\) \\[with T = int; long unsigned int Align = 4\\]."
  "a HeapArray owns its storage alone: a copy would free the storage two times")
foundation_neg_compile_test(neg_core_heap_array_view_of_temporary
  "use of deleted function .void foundation::core::HeapArray<T, Align>::view\\(\\) && \\[with T = int; long unsigned int Align = 4\\]."
  "a view of a temporary HeapArray points into storage that the full expression frees")
foundation_neg_compile_test(neg_core_heap_array_mint_forged_body
  "no matching function for call to .foundation::core::HeapArray<int>::HeapArray\\(<brace-enclosed initializer list>\\)."
  "HeapArray\\(const foundation::core::detail::HeapArrayKey&, std::size_t, const T&\\) requires  is_nothrow_copy_constructible_v<T>")

# The address of a function that gives a Ref with a target type, and an
# explicit specialization with a declared type, set no brand.  The result
# type holds the brand in undeducible_brand_t, so the fresh default applies,
# and a type that names a held brand matches no function.
foundation_neg_compile_test(neg_core_fixed_array_get_brand_from_target_type
  "no matches converting function .get. to type .using Pick = class foundation::core::Option<foundation::core::Ref<int, \\{anonymous\\}::Held> > \\(class foundation::core::FixedArray<int, 4>::\\*\\)\\(std::size_t\\) & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type> > foundation::core::FixedArray<T, N>::get\\(std::size_t\\) &")
foundation_neg_compile_test(neg_core_fixed_array_get_const_brand_from_target_type
  "no matches converting function .get. to type .using Pick = class foundation::core::Option<foundation::core::Ref<const int, \\{anonymous\\}::Held> > \\(class foundation::core::FixedArray<int, 4>::\\*\\)\\(std::size_t\\) const & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type> > foundation::core::FixedArray<T, N>::get\\(std::size_t\\) const &")
foundation_neg_compile_test(neg_core_heap_array_get_brand_from_target_type
  "no matches converting function .get. to type .using Pick = class foundation::core::Option<foundation::core::Ref<int, \\{anonymous\\}::Held> > \\(class foundation::core::HeapArray<int, 4>::\\*\\)\\(std::size_t\\) & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type> > foundation::core::HeapArray<T, Align>::get\\(std::size_t\\) &")
foundation_neg_compile_test(neg_core_heap_array_get_const_brand_from_target_type
  "no matches converting function .get. to type .using Pick = class foundation::core::Option<foundation::core::Ref<const int, \\{anonymous\\}::Held> > \\(class foundation::core::HeapArray<int, 4>::\\*\\)\\(std::size_t\\) const & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type> > foundation::core::HeapArray<T, Align>::get\\(std::size_t\\) const &")
foundation_neg_compile_test(neg_core_fixed_list_push_brand_from_target_type
  "no matches converting function .push. to type .using Pick = class foundation::core::Result<foundation::core::Ref<int, \\{anonymous\\}::Held>, foundation::core::Full<int> > \\(class foundation::core::FixedList<int, 4>::\\*\\)\\(int\\) & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type>, foundation::core::Full<T> > foundation::core::FixedList<T, N>::push\\(T\\) &")
foundation_neg_compile_test(neg_core_fixed_list_get_brand_from_target_type
  "no matches converting function .get. to type .using Pick = class foundation::core::Option<foundation::core::Ref<int, \\{anonymous\\}::Held> > \\(class foundation::core::FixedList<int, 4>::\\*\\)\\(std::size_t\\) & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type> > foundation::core::FixedList<T, N>::get\\(std::size_t\\) &")
foundation_neg_compile_test(neg_core_fixed_list_get_const_brand_from_target_type
  "no matches converting function .get. to type .using Pick = class foundation::core::Option<foundation::core::Ref<const int, \\{anonymous\\}::Held> > \\(class foundation::core::FixedList<int, 4>::\\*\\)\\(std::size_t\\) const & noexcept."
  "typename foundation::brand::undeducible_brand<Fresh>::type> > foundation::core::FixedList<T, N>::get\\(std::size_t\\) const &")
foundation_neg_compile_test(neg_core_zip_fixed_brand_from_target_type
  "invalid .static_cast. from type .<unresolved overloaded function type>. to type .Pick."
  "foundation::core::detail::ZipRange<int, int, \\{anonymous\\}::Held> \\(\\*\\)\\(foundation::core::View<int, 4>, foundation::core::View<int, 4>\\) noexcept")
foundation_neg_compile_test(neg_core_zip_dynamic_brand_from_target_type
  "invalid .static_cast. from type .<unresolved overloaded function type>. to type .Pick."
  "foundation::core::Result<foundation::core::detail::ZipRange<int, int, \\{anonymous\\}::Held>, foundation::core::LengthMismatch> \\(\\*\\)\\(foundation::core::View<int>, foundation::core::View<int, 4>\\) noexcept")
foundation_neg_compile_test(neg_core_zip_fixed_brand_from_specialization
  "template-id .zip<>. for .constexpr foundation::core::detail::ZipRange<int, int, \\{anonymous\\}::Held> foundation::core::zip\\(View<int, 4>, View<int, 4>\\). does not match any template declaration"
  "foundation::core::detail::ZipRange<First, Second, typename foundation::brand::undeducible_brand<Fresh>::type> foundation::core::zip")
foundation_neg_compile_test(neg_core_zip_dynamic_brand_from_specialization
  "template-id .zip<>. for .constexpr foundation::core::Result<foundation::core::detail::ZipRange<int, int, \\{anonymous\\}::Held>, foundation::core::LengthMismatch> foundation::core::zip\\(View<int, 18446744073709551615>, View<int, 4>\\). does not match any template declaration"
  "foundation::core::Result<foundation::core::detail::ZipRange<First, Second, typename foundation::brand::undeducible_brand<Fresh>::type>, foundation::core::LengthMismatch>")

# enumerate() needs the proven indexes of indices(), so a View of a dynamic
# extent has none.  zip of two Views of different fixed extents is refused
# at compile time, and the loop of a temporary array is refused.
foundation_neg_compile_test(neg_core_view_enumerate_dynamic_extent
  "no matching function for call to .foundation::core::View<const int>::enumerate\\(\\) const."
  "the expression .Extent != foundation::core::dynamic_extent \\[with Extent = 18446744073709551615\\]. evaluated to .false.")
foundation_neg_compile_test(neg_core_zip_different_fixed_extents
  "use of deleted function .void foundation::core::zip\\("
  "\\[with First = const int; long unsigned int FirstExtent = 4; Second = const int; long unsigned int SecondExtent = 5\\]"
  "two Views of different fixed extents have different counts, so the longer View has elements with no pair")
foundation_neg_compile_test(neg_core_fixed_array_enumerate_of_temporary
  "use of deleted function .void foundation::core::FixedArray<T, N>::enumerate\\(\\) const && \\[with T = int; long unsigned int N = 4\\]."
  "a loop over a temporary FixedArray points into an object that the full expression ends")

# ── The door directory of the base floor: foundation/os ───────────────
#
# The file door reads a file of /proc into a FixedText, gives the error of
# the open and of the read, and closes each descriptor.  The directory door
# lists /proc/self, stops when the visitor asks, and closes each descriptor.
# A PathText refuses an append that does not fit and a zero byte.  The
# kernel floor judges each kind of release text.
foundation_test(test_os_floor)

# The CText of a temporary PathText would point into an object that the
# full expression ends.
foundation_neg_compile_test(neg_os_path_text_of_temporary
  "use of deleted function .*foundation::os::PathText<Capacity>::c_text\\(\\) const &&"
  "a CText of a temporary PathText points into an object that the full expression ends")

# ── The attack ledger of the primitive families ───────────────────────
#
# Each attack that type-checks and that a family cannot refuse in C++, with
# a pin that fails when a repair refuses it.
foundation_test(test_core_attack_ledger)

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
