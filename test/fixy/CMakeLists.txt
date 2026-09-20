# Tests for the fixy layer.
#
# A fixy test links `fixy` (and through it `foundation`) and nothing above.
# The sanitizer flags come from the enclosing test/CMakeLists.txt.

function(fixy_test name)
  if(ARGN)
    set(_sources ${ARGN})
  else()
    set(_sources ${name}.cpp)
  endif()
  add_executable(${name} ${_sources})
  target_link_libraries(${name} PRIVATE fixy)
  set_source_files_properties(${_sources} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-Wno-alloc-size-larger-than>")
  target_compile_options(${name} PRIVATE -UNDEBUG)
  if(CRUCIBLE_SAN_COMPILE_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_SAN_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${CRUCIBLE_SAN_LINK_FLAGS})
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "fixy")
  if(CRUCIBLE_TSAN)
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT
        "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/test/tsan-suppressions.txt:halt_on_error=1")
  endif()
endfunction()

# Negative-compile fixtures for the fixy layer.  Same contract as
# foundation_neg_compile_test in test/foundation/CMakeLists.txt: at least
# two regexes (reason + project token), every regex required, strict
# single-error mode on.  The label is fixy_neg_new because "fixy_neg" is
# the old corpus under test/fixy_neg/, which Stage D deletes.
function(fixy_neg_compile_test name)
  set(_diags ${ARGN})
  list(LENGTH _diags _count)
  if(_count LESS 2)
    message(FATAL_ERROR
      "fixy_neg_compile_test(${name}): two required-diagnostic "
      "regexes are the minimum (the reason and the project token); got ${_count}")
  endif()
  add_executable(${name} EXCLUDE_FROM_ALL neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE fixy)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/neg/${name}.cpp
            ${name}
            ${_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "fixy_neg_new"
    ENVIRONMENT "CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1")
endfunction()

# The fixture that proves the harness fires through the fixy target.
fixy_neg_compile_test(neg_fixy_harness_probe
  "constraints not satisfied"
  "satisfaction of .HalvesEvenly<T>.")

fixy_test(test_qtt)
fixy_test(test_mutation)
fixy_test(test_axis)

# The axis table's primary is defined, so defaulted_axes is what keeps
# it from handing a new axis a claim nobody gave it.  A value one past
# the enum stands in for that axis.
fixy_neg_compile_test(neg_axis_unclassified_axis
  "static assertion failed: an axis off the roster and off the specialization list is classified"
  "required by the constraints of .template<fixy::Axis A> concept fixy::AxisIsClassified."
  "the expression .\\(TakesDefaultTraits<A>\\) == \\(axis_takes_defaults<A>\\) \\[with A = \\(fixy::Axis\\)33\\]. evaluated to .false.")

# The copy constructor of Qtt is deleted with a reason; the compiler
# names the constructor and repeats the reason.
fixy_neg_compile_test(neg_qtt_copy_a_linear
  "use of deleted function"
  "fixy::Qtt<Grade, T>::Qtt\\(const fixy::Qtt<Grade, T>&\\)"
  "move-only")

# consume is rvalue-qualified and non-const; a const borrow cannot call it.
fixy_neg_compile_test(neg_qtt_consume_through_const_ref
  "discards qualifiers"
  "fixy::Qtt<Grade, T>::consume\\(\\) &&")

# A body CRUCIBLE_PRE reaches its consteval trap when a constant
# expression steps a Monotonic backward or sets a WriteOnce twice.
fixy_neg_compile_test(neg_mutation_monotonic_step_backward
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .counter\\.fixy::Monotonic<unsigned int, std::less<unsigned int> >::advance\\(5\\)."
  "fixy/Mutation\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_mutation_write_once_twice
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .slot\\.fixy::WriteOnce<int>::set\\(2\\)."
  "fixy/Mutation\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# ── Task #100 (A9): the atom catalog ─────────────────────────────────
fixy_test(test_atom)

# ── Task #108 (A11.1): the fn gate ───────────────────────────────────
# The resolver, the silent strict defaults and what the gate refuses.
# The negative fixtures for the tiers arrive with A11.5.
fixy_test(test_fn)

# ── Task #109 (A11.2): the collision rules ───────────────────────────
# Ten live rules, and twenty-two registered against the axis they wait
# on.  The per-rule negative fixtures arrive with A11.4.
fixy_test(test_collision)

# ── Task #110 (A11.3): the corpus, the roles, the insights, the contexts
# Six refused combinations wired into tier 5, seven roles over the
# atoms, one generated insight provider per axis plus six by hand, and
# the five production contexts.  The per-entry negative fixtures arrive
# with A11.4.
fixy_test(test_corpus)
fixy_test(test_role)
fixy_test(test_insights)
# test_ctx is foundation's sentinel for ExecCtx; this one is fixy's for
# the five named contexts.
fixy_test(test_contexts)

# ── Task #111 (A11.4): the hand corpus ───────────────────────────────
#
# One fixture per rule that can fire and one per corpus entry: eleven
# and six.  The other forty-three rule codes get none, and the reason is
# data rather than a claim — collision::rule_corpus records each as
# Pending (twenty-two, waiting on an axis with no atom, so no pack can
# trip them) or Absent (twenty-one, not implemented, each with the note
# saying what is missing).  test_collision.cpp pins that accounting, and
# the day a pending axis gains its first atom
# every_pending_axis_is_still_empty() fires and names the rules that
# then need a fixture here.
#
# Each fixture floors on three compiler-produced regexes: the tier-5
# reason, its own rule code or entry name — which fn's message carries
# because A11.3 made tier 5 report WHICH rule or entry refused the pack
# — and the GCC bullet naming the member that evaluated false.  None of
# the three appears in the fixture's own source, so the source echo
# cannot satisfy the gate.
#
# Three rules share two packs, and the fixtures say so in their own
# text: L007's premises (borrow x Row<Bg>) and R002's (coroutine x
# borrow) are each a special case of L002's (borrow x concurrent), so no
# pack isolates them.  The message lists every code a pack trips, which
# is what lets each of the three stand on its own.

set(_fixy_tier5_reason "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 5\\]: the combination is refused")
set(_fixy_tier5_member "::tier5_composition_ok_.+ evaluates to false")

fixy_neg_compile_test(neg_rule_l002_borrow_async
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) .*L002"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_l007_borrow_bg_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) .*L007"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_r002_coroutine_borrow
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) .*R002"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_r003_coroutine_bg_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) R003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_m012_monotonic_concurrent
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) M012:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_p010_ghost_observable_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) P010:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_p002_ghost_stdio
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) P002:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_t001_capability_unverified
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) T001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_l006_linear_longjmp
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) L006:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_g002_thread_local_atomic
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) G002:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_d002_recursion_unbounded_cost
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) D002:"
  "${_fixy_tier5_member}")

# The six corpus entries.  Each floors on its entry name and on a phrase
# of the citation the entry carries, so a fixture cannot pass against an
# entry that lost its literature reference.
fixy_neg_compile_test(neg_corpus_classified_io
  "${_fixy_tier5_reason}"
  "Corpus entry classified_io_without_declassify"
  "Sabelfeld-Myers 2003")
fixy_neg_compile_test(neg_corpus_classified_bg
  "${_fixy_tier5_reason}"
  "Corpus entry classified_bg_without_declassify"
  "Smith-Volpano 1998")
fixy_neg_compile_test(neg_corpus_staleness_secret
  "${_fixy_tier5_reason}"
  "Corpus entry staleness_secret_without_declassify"
  "Hunt-Sands 2008")
fixy_neg_compile_test(neg_corpus_ghost_runtime_observable
  "${_fixy_tier5_reason}"
  "Corpus entry ghost_runtime_observable"
  "Spirit of Ghost Code")
fixy_neg_compile_test(neg_corpus_internal_io
  "${_fixy_tier5_reason}"
  "Corpus entry internal_io_without_declassify"
  "Bell-LaPadula 1973")
fixy_neg_compile_test(neg_corpus_internal_bg
  "${_fixy_tier5_reason}"
  "Corpus entry internal_bg_without_declassify"
  "Bell-LaPadula 1973")

# The tier chain itself: one message per bad pack.  fn asks the tiers in
# order and stops at the first failure, because a later tier's question
# is a hard error on a pack an earlier one refused — the uniqueness walk
# and every rule read each atom's `axis` member.  Before that gating a
# malformed atom produced the tier-2 message and thirty-eight
# diagnostics from inside the walks, and an unholdable payload produced
# the tier-1 message and six from `const void&`.  These three fixtures
# are what hold each tier to exactly one error.
fixy_neg_compile_test(neg_fn_tier1_unholdable_payload
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 1\\]"
  "fixy::fn<void>::tier1_payload_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_fn_tier2_malformed_atom
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]"
  "fixy::fn<int, .anonymous.::not_an_atom>::tier2_atoms_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_fn_tier4_duplicate_axis
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 4\\]"
  "fixy::atom::copy, fixy::atom::affine>::tier4_unique_ok_.+ evaluates to false")

# A cv-qualified atom fails the same-as clause of IsAtom; the compiler
# names the clause and the two types it compared.
fixy_neg_compile_test(neg_atom_cv_qualified_rejected
  "constraints not satisfied"
  "required for the satisfaction of .IsAtom<G>. \\[with G = const fixy::atom::affine\\]"
  ".const struct fixy::atom::affine. is not the same as .struct fixy::atom::affine.")

# Each family roster is a hand list, and the walk beside it reads the
# family namespace to catch an atom the list omits.  The fixture plants
# one in two families, so the refusal is shown to be per-namespace.
fixy_neg_compile_test(neg_atom_family_namespace_unrostered
  "static assertion failed: fixy/atoms/Ctrl\\.h: an atom declared in fixy::atom::ctrl is missing from ctrl_atom_roster"
  "static assertion failed: fixy/atoms/Stack\\.h: an atom declared in fixy::atom::stack is missing from stack_atom_roster")

# A final type that derives atom_base but names no axis fails the
# requires-expression of IsAtom on `G::axis`.
fixy_neg_compile_test(neg_atom_without_axis_rejected
  "constraints not satisfied"
  "required for the satisfaction of .IsAtom<G>."
  "the required expression .G::axis. is invalid")

# ── Task #101 (A10.1): fixy/Refined.h ─────────────────────────────────

fixy_test(test_refined)

# The checked mint carries CRUCIBLE_PRE(Pred(v)); a refused value in a
# constant expression reaches the macro's consteval trap through the
# mint, and the expansion note names the macro inside Refined.h.
fixy_neg_compile_test(neg_refined_predicate_fails_at_consteval_mint
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::mint_refined<.*, int>\\(-1\\)."
  "fixy/Refined\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# non_negative ⇒ positive is not a member of admitted_implications, and
# the edge the fixture declares elsewhere is inert, so the constrained
# weakening fails its constraint and the note shows implies_v false.
fixy_neg_compile_test(neg_refined_implication_not_admitted
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = .*; Q = .*\\]. evaluated to .false.")

# No public constructor takes a bare value; the candidates the compiler
# lists are the copy, the move and the two private doors.
fixy_neg_compile_test(neg_refined_direct_construction
  "no matching function for call to .fixy::Refinement<.*>::Refinement\\(<brace-enclosed initializer list>\\)."
  "candidate .*: .constexpr fixy::Refinement<Pred, T, Sealed>::Refinement\\(checked_door_, T\\) requires  PredicateInvocableOn<Pred, T>."
  "candidate .*: .constexpr fixy::Refinement<Pred, T, Sealed>::Refinement\\(trusted_door_, T\\).")

# The checked mint's gate names the concept at the call site instead of
# failing somewhere inside the contract clause.
fixy_neg_compile_test(neg_refined_mint_predicate_not_invocable
  "constraints not satisfied"
  "required for the satisfaction of .PredicateInvocableOn<Pred, T>. \\[with Pred = .*; T = int\\]")

# The sealed checked mint reaches the same consteval trap through its
# own door.
fixy_neg_compile_test(neg_refined_sealed_mint_predicate_fails_at_consteval
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::mint_sealed_refined<.*, int>\\(0\\)."
  "fixy/Refined\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# into() is declared once and constrained to the unsealed half, so the
# sealed refinement refuses it on the constraint rather than on name
# lookup.  The name is unreachable either way.
fixy_neg_compile_test(neg_refined_sealed_into_unreachable
  "no matching function for call to .fixy::Refinement<.*, true>::into\\(\\)."
  "constraints not satisfied"
  "the expression .!Sealed \\[with Sealed = true\\]. evaluated to .false.")

# A rule family opts in by deriving rule_family<itself>, which replaced
# a marker variable that could be forgotten.  A class in the namespace
# that derives nothing is refused where the namespace is walked.
fixy_neg_compile_test(neg_refined_rule_family_without_base
  "static assertion failed: a class declared in admitted_implications is a rule family, which it says by deriving rule_family<itself>"
  ".fixy::refined::rule_family<fixy::refined::admitted_implications::not_a_rule_family>. is not a base of .fixy::refined::admitted_implications::not_a_rule_family.")

# DivisibleBy<0> and Bounded<10, 5, int> are refused where the type is
# named, before any value is minted.
fixy_neg_compile_test(neg_refined_divisible_by_zero
  "static assertion failed: DivisibleBy<0> is undefined \\(modulo by zero\\)"
  "In instantiation of .struct fixy::DivisibleBy<0>.")
fixy_neg_compile_test(neg_refined_bounded_inverted
  "static assertion failed: Bounded<Lo, Hi, T>: range is empty \\(Lo > Hi\\)"
  "In instantiation of .struct fixy::detail::bounded_alias<10, 5, int>.")

# ── Task #102 (A10.2): fixy/Tagged.h and fixy/Secret.h ────────────────

fixy_test(test_tagged)
fixy_test(test_secret)

# Verified -> Unverified has no edge in admitted_retags; the member
# retag fails its constraint and the note names RetagAllowed with the
# two tags.
fixy_neg_compile_test(neg_tagged_retag_not_admitted
  "constraints not satisfied"
  "satisfaction of .RetagAllowed<Tag, NewTag>."
  "Tag = fixy::tags::trust::Verified; NewTag = fixy::tags::trust::Unverified")

# The value constructor is private, so a Sanitized tag cannot be forged
# around a raw value; the compiler names the constructor.
fixy_neg_compile_test(neg_tagged_forge_by_constructor
  "is private within this context"
  "fixy::Tagged<T, Tag>::Tagged\\(T\\)"
  "Tag = fixy::tags::source::Sanitized")

# A policy derived from the marker base with no edge in
# admitted_policies fails AdmittedDeclassification, and the note shows
# the fail-closed admission evaluating to false.
fixy_neg_compile_test(neg_secret_declassify_unlisted_policy
  "constraints not satisfied"
  "satisfaction of .AdmittedDeclassification<Policy>."
  "the expression .\\(admits<Ns, From, To>\\)\\(\\).*evaluated to .false.")

# The copy constructor of Secret is deleted with a reason; the compiler
# names the constructor and repeats the reason.
fixy_neg_compile_test(neg_secret_copy
  "use of deleted function"
  "fixy::Secret<T>::Secret\\(const fixy::Secret<T>&\\)"
  "cannot be silently duplicated")

# ── Task #104 (A10.4): Stale, OwnedRegion, Borrowed, ScopedView, Machine ──
#
# The region test keeps the file name of the test it is ported from and
# carries a suffix, because test/CMakeLists.txt still registers
# test_owned_region and a CMake target name is global.
fixy_test(test_stale)
fixy_test(test_owned_region_fixy test_owned_region.cpp)
fixy_test(test_borrowed)
fixy_test(test_scoped_view)
fixy_test(test_machine)

# The escape audit reads private members: a carrier holding a view in a
# private field fails no_scoped_view_field_check with the audit's own
# message, and the note shows contains_scoped_view answering true.
fixy_neg_compile_test(neg_scoped_view_escapes_through_private_member
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "fixy/ScopedView\\.h:[0-9]+:[0-9]+: error"
  "fixy::contains_scoped_view<Holder>\\(\\). evaluates to false")

# A transition whose edge is not declared in the namespace the machine
# names fails the MachineTransition constraint on transition_to.
fixy_neg_compile_test(neg_machine_transition_not_admitted
  "constraints not satisfied"
  "satisfaction of .MachineTransition<OldState, NewState, Edges>."
  "machine_transition_v<From, To, Edges>.*evaluated to .false.")

# A Borrowed of a const element type built from a temporary vector
# selects the deleted rvalue-range twin of the span constructor.
fixy_neg_compile_test(neg_borrowed_from_a_temporary
  "use of deleted function"
  "fixy::Borrowed<T, Source>::Borrowed\\(R&&\\) \\[with R = std::vector<int>.*\\]"
  "Borrowed of a temporary range dangles")

# weaken towards a fresher grade in a constant expression reaches the
# CRUCIBLE_PRE trap inside Stale::weaken.
fixy_neg_compile_test(neg_stale_weakened_downwards
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .eight_behind\\.fixy::Stale<int>::weaken\\("
  "fixy/Stale\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# ── Task #106 (A10.6): the consumed bands and the F* aliases ─────────
fixy_test(test_bands)
fixy_test(test_aliases)

# relax moves down the chain only.  Both relax overloads are rejected
# because the leq gate in their requires clause evaluates to false for
# the pair (PhiloxRng, Pure).
fixy_neg_compile_test(neg_bands_relax_up_the_chain
  "no matching function for call to .relax<foundation::algebra::lattices::DetSafeTier::Pure>"
  "band_lattice_t<B>::leq\\)\\(WeakerTier, band_tier_v<B>\\).*evaluated to .false.")

# relax on a const lvalue copies, so a move-only payload fails the
# copy_constructible clause of the lvalue overload, and the rvalue
# overload is not viable for an lvalue.
fixy_neg_compile_test(neg_bands_relax_lvalue_move_only
  "no matching function for call to .relax<foundation::algebra::lattices::DetSafeTier::PhiloxRng>"
  "required for the satisfaction of .copy_constructible<fixy::band_value_t<B> >."
  "!\\(is_lvalue_reference_v<B>\\).*evaluated to .false.")

# satisfies_v takes a tier of the band's own chain; a tier from another
# enum fails the same_as clause on the required tier's type.
fixy_neg_compile_test(neg_bands_satisfies_foreign_tier
  "use of invalid variable template .satisfies_v<"
  "satisfaction of .same_as<decltype \\(Required\\), fixy::band_tier_t<B> >."
  ".enum class foundation::algebra::lattices::HotPathTier. is not the same as .enum class foundation::algebra::lattices::DetSafeTier.")

# The seven coverage assertions in Bands.h all pass, so none of them
# shows what the walk does when a tier has no alias.  This namespace
# mirrors fixy::hot_path and leaves one of the three tiers out.
fixy_neg_compile_test(neg_bands_tier_without_an_alias
  "static assertion failed: a namespace missing one tier alias covers its enum"
  "the comparison reduces to .\\(0 == 1\\).")

# ── Task #105 (A10.5): the eight value wrappers ──────────────────────
#
# The checked test keeps the file name of the test it is ported from and
# carries a suffix, because test/CMakeLists.txt still registers
# test_checked and a CMake target name is global.
fixy_test(test_owned_file)
fixy_test(test_owned_mmap)
fixy_test(test_constant_time)
fixy_test(test_cyclic)
fixy_test(test_saturated)
fixy_test(test_saturate_fixy test_saturate.cpp)

# The four refusals of fixy/Saturate.h, from the old safety_neg corpus,
# under the namespace's name because the old corpus still owns the
# neg_saturate_ targets.  An into helper takes a mutable reference; a
# from helper returns Saturated<T>, not T; a det helper returns the pure
# band, not the Saturated<T> inside it; and a det result reaches another
# tier only through relax.  Each fixture is matched on the qualified
# type or signature the compiler prints, which the fixture's source
# never spells in that form.
fixy_neg_compile_test(neg_sat_into_const_dest
  "no matching function for call to .add_sat_into\\(const unsigned int&, unsigned int\\)."
  "add_sat_into\\(const unsigned int&, unsigned int\\)")
fixy_neg_compile_test(neg_sat_from_raw_escape
  "cannot convert .fixy::Saturated<unsigned int>. to .unsigned int. in initialization"
  "fixy::Saturated<unsigned int>")
fixy_neg_compile_test(neg_sat_det_raw_result_escape
  "conversion from .fixy::sat::DetSatPure<unsigned int>. \\{aka .foundation::algebra::Graded<"
  "to non-scalar type .fixy::Saturated<unsigned int>. requested")
fixy_neg_compile_test(neg_sat_det_cross_tier_assignment
  "conversion from .Graded<\\[\\.\\.\\.\\],At<foundation::algebra::lattices::DetSafeTier::Pure>,\\[\\.\\.\\.\\]>."
  "to non-scalar type .Graded<\\[\\.\\.\\.\\],At<foundation::algebra::lattices::DetSafeTier::PhiloxRng>,\\[\\.\\.\\.\\]>. requested")
fixy_test(test_checked_fixy test_checked.cpp)
fixy_test(test_bits)
fixy_test(test_fixed_array)

# The leak witness is one reflection query over the atom catalog, so no
# translation unit can mint an authorization out of a type of its own.
fixy_neg_compile_test(neg_owned_mmap_release_without_leak_atom
  "no matching function for call to .fixy::OwnedMmap<.*>::release\\("
  "constraints not satisfied"
  "required for the satisfaction of .IsLeakAtom<LeakAtom>."
  "the expression .is_leak_atom_v<G> \\[with G = <unnamed>::NotALeakAtom\\]. evaluated to .false.")

# The compile-time accessor carries its bound in a requires clause.
fixy_neg_compile_test(neg_fixed_array_at_out_of_range_index
  "no matching function for call to .fixy::FixedArray<int, 4>::at<4>\\(\\)."
  "constraints not satisfied"
  "the expression .I < N \\[with I = 4; N = 4\\]. evaluated to .false.")

# The bit operators are hidden friends, so two Bits over different enums
# share none of them.
fixy_neg_compile_test(neg_bits_cross_enum_operand
  "no match for .operator\\|."
  "operand types are .const fixy::Bits<.*Perm>. and .const fixy::Bits<.*Colour>."
  "no known conversion for argument 2 from .const fixy::Bits<.*Colour>. to .*Perm.")

# The carrier admits a float; the three checked operations do not,
# because the clamp they report is the end of an integer range.
fixy_neg_compile_test(neg_saturated_checked_op_on_float
  "no matching function for call to .add_sat_checked\\(float, float\\)."
  "constraints not satisfied"
  "required for the satisfaction of .integral<T>. \\[with T = float\\]")

# The ninth header of A10.5.  The old spelling was a pair that included
# each other; here the sanitizer follows the declarations it reads.
fixy_test(test_path)

# CipherPath has no edge into Sanitized, so its lane never reaches the
# sanitizer.
fixy_neg_compile_test(neg_path_sanitize_unadmitted_source
  "no matching function for call to .sanitize_path_no_dotdot\\("
  "constraints not satisfied"
  "required for the satisfaction of .RetagAllowed<From, fixy::tags::source::Sanitized>. \\[with From = fixy::tags::source::CipherPath\\]"
  "no operand of the disjunction is satisfied")

# A Sanitized tag is the proof a predicate ran, so it cannot be minted
# around a raw path.
fixy_neg_compile_test(neg_path_forge_sanitized_tag
  "is private within this context"
  "fixy::Tagged<T, Tag>::Tagged\\(T\\)"
  "Tag = fixy::tags::source::Sanitized")

# ── Task #153 (FIXY-6): the OS tag namespaces, walked ────────────────
#
# The check reads the nine namespaces rather than a hand roster, so the
# witness that it works is a class it was never told about.  The
# fixture plants one before the header, and both halves of the check
# answer: the shape walk refuses it, and the count pin sees 46 where it
# recorded 45.  The namespace count went from nine to ten and the tag
# count from 35 to 45 when #116 moved fixy::mmap::advice here, so that one
# walk covers every tag namespace.
fixy_neg_compile_test(neg_os_tag_namespace_holds_a_non_tag
  "static assertion failed: fixy/atoms/Os\\.h: a class declared in one of the ten tag namespaces has state"
  "static assertion failed: fixy/atoms/Os\\.h: the ten tag namespaces hold a different number of tags"
  "the comparison reduces to .\\(46 == 45\\).")

# ── Task #117 (A14.1): the binary session core ───────────────────────
#
# Protocol.h is the type level: combinators, duality, composition,
# well-formedness.  Stepping.h adds the abandonment-check policy and
# the SteppingGraded contract that foundation/algebra/Modality.h
# reserved ModalityKind::Stepping for and never wrote down.
fixy_test(test_session_protocol)

# Handle.h is the runtime half.  The test forks a child to prove the
# abandonment policy's destructor really runs, so a passing run prints
# one abandonment banner on stderr from that child.
fixy_test(test_session_handle)

# A channel whose two endpoints are not duals has no deadlock-freedom
# guarantee.  Both operands here are individually well-formed, so the
# refusal can only come from the duality clause.
fixy_neg_compile_test(neg_sess_dual_mismatch
  "static assertion failed"
  "fixy::session::diagnostic \\[Dual_Mismatch\\]"
  "ensure_dual<P1, P2>")

# Branch-asymmetric composition on a spine that holds no choice has no
# branch to compose into.  Walking Send -> Recv -> End reaches the End
# arm, which is the clause that refuses.
fixy_neg_compile_test(neg_sess_compose_at_branch_no_choice
  "static assertion failed"
  "fixy::session::diagnostic \\[Branch_Compose_No_Choice\\]"
  "compose_at_branch_t<P, I, Q>")

# HS14 for mint_session_handle.  Its requires-clause is the whole
# admission gate, so each clause gets a fixture that fails through it
# and through no other: an ill-formed protocol, a well-formed protocol
# that is not runnable, and a resource whose address is not stable.
fixy_neg_compile_test(neg_sess_mint_ill_formed
  "no matching function for call to .mint_session_handle"
  "constraints not satisfied"
  "is_well_formed_v<Proto>")

fixy_neg_compile_test(neg_sess_mint_empty_choice
  "no matching function for call to .mint_session_handle"
  "constraints not satisfied"
  "is_empty_choice_v<Proto>")

fixy_neg_compile_test(neg_sess_mint_unpinned_reference
  "no matching function for call to .mint_session_handle"
  "constraints not satisfied"
  "satisfaction of .SessionResource<Resource>.")

# ── Task #118 (A14.2): the machine and Vigil-mode session bridges ────
fixy_test(test_session_bridges)

# HS14 for mint_session_from_machine: one fixture per clause of its
# gate — the protocol it will mint handles over, and the arguments it
# builds the state from.
fixy_neg_compile_test(neg_sess_machine_bridge_ill_formed
  "no matching function for call to .mint_session_from_machine"
  "constraints not satisfied"
  "is_well_formed_v<Proto>")

fixy_neg_compile_test(neg_sess_machine_bridge_state_args
  "no matching function for call to .mint_session_from_machine"
  "constraints not satisfied"
  "is_constructible_v<State")

# HS14 for mint_atomic_session: the cell's read shape, and the protocol.
fixy_neg_compile_test(neg_sess_atomic_not_a_cell
  "no matching function for call to .mint_atomic_session"
  "constraints not satisfied"
  "satisfaction of .AtomicMachineCell<Cell>.")

fixy_neg_compile_test(neg_sess_atomic_ill_formed
  "no matching function for call to .mint_atomic_session"
  "constraints not satisfied"
  "is_well_formed_v<Proto>")

# HS14 for mint_vigil_mode_bridge.  Its gate is one clause, so the two
# fixtures separate the two ways to fail it: an unrelated type, and a
# cell that satisfies the structural concept and is still refused —
# which is what says the gate is nominal rather than structural.
fixy_neg_compile_test(neg_sess_vigil_bridge_wrong_cell
  "no matching function for call to .mint_vigil_mode_bridge"
  "satisfaction of .CanMintVigilModeBridge<Cell>."
  "long unsigned int")

fixy_neg_compile_test(neg_sess_vigil_bridge_lookalike_cell
  "no matching function for call to .mint_vigil_mode_bridge"
  "satisfaction of .CanMintVigilModeBridge<Cell>."
  "LookalikeCell")

# The transition relation itself: DIVERGED has no edge in either
# direction, so forming the transition type is where the pair is
# refused.
fixy_neg_compile_test(neg_sess_vigil_illegal_transition
  "static assertion failed"
  "fixy::session::diagnostic \\[VigilModeBridge_IllegalTransition\\]"
  "admitted_mode_transitions")

# ── Task #119 (A15): the lock-free rings ─────────────────────────────
#
# The threaded half of this test is what checks the ordering claim, so
# it is the half that means anything under the tsan preset.
fixy_test(test_concurrent_rings)

# A capacity that is not a power of two makes the mask drop bits, and
# two live positions collide on one slot with nothing to detect it.
fixy_neg_compile_test(neg_ring_capacity_not_power_of_two
  "static assertion failed"
  "fixy::concurrent::SpscRing<T, Capacity>: Capacity must be a power"
  "index modulo Capacity")

# A cell is overwritten in place and never destroyed, so a value that
# owns anything leaks once per slot per lap.
fixy_neg_compile_test(neg_ring_value_owns_a_resource
  "template constraint failure"
  "constraints not satisfied"
  "satisfaction of .RingValue<T>.")

# ── Task #119 (A15): the permissioned channels ───────────────────────
#
# The split relation for each channel's tag triple is declared in the
# channel's own header, which is why include/fixy/concurrent/ is an
# authoring location in scripts/check-splits-into-orphan.sh.
fixy_test(test_concurrent_channels)

# A handle owns a linear token or a pool share; copying it would
# duplicate the one and double-count the other.
fixy_neg_compile_test(neg_channel_producer_handle_copied
  "use of deleted function"
  "ProducerHandle owns the Producer Permission"
  "duplicate the linear token")

# The role is in the handle type: a producer has no pop.
fixy_neg_compile_test(neg_channel_pop_from_producer_handle
  "has no member named .try_pop."
  "ProducerHandle")

# Two channels with the same value type and capacity still have
# distinct tags, so an endpoint token from one cannot open the other.
fixy_neg_compile_test(neg_channel_foreign_tag_permission
  "cannot convert"
  "spsc_tag::Producer<.*TraceTag"
  "spsc_tag::Producer<.*MetaTag")

# ── Task #119 (A15): the handle poles and the stage shape ─────────────
#
# Which pole of a channel a handle is, and what shape a pipeline-stage
# body has.  Both were in namespace crucible::safety::extract, which 57
# headers reopened; they arrive here beside the channels whose handles
# they read.
fixy_test(test_handle_traits)
fixy_test(test_stage_shape)

# Each pole's value alias is constrained on its own predicate.  Both
# probes hold the member the predicate wants, with exactly the right
# signature, and are refused for holding the opposite pole's member as
# well — so nothing decides which pole they are.  The two families
# exclude on different member pairs, try_push against try_pop and publish
# against load, so a dropped check in one leaves the other's fixture
# green.
fixy_neg_compile_test(neg_handle_traits_consumer_value_on_non_handle
  "template constraint failure"
  "the expression .is_consumer_handle_v<T> \\[with T = <unnamed>::both_poles\\]. evaluated to .false.")
fixy_neg_compile_test(neg_handle_traits_swmr_writer_value_on_non_writer
  "template constraint failure"
  "the expression .is_swmr_writer_v<T> \\[with T = <unnamed>::both_swmr_poles\\]. evaluated to .false.")

# The three stage-shape extractors are each constrained on PipelineStage.
# The input fixture probes a function of plain ints; the output fixture
# probes a function whose parameters ARE the right handles as non-const
# rvalue references, with only the data-flow order reversed, which is the
# sharper case; the value-preserving fixture probes a nullary, where an
# unconstrained comparison would answer true because void equals void.
fixy_neg_compile_test(neg_stage_shape_input_value_on_non_stage
  "template constraint failure"
  "required for the satisfaction of .PipelineStage<FnPtr>. \\[with FnPtr = &not_a_stage\\(int, int\\)\\]")
fixy_neg_compile_test(neg_stage_shape_output_value_on_non_stage
  "template constraint failure"
  "the expression .fixy::concurrent::StageArity<FnPtr>::ordered \\[with FnPtr = &producer_first\\(<unnamed>::fake_producer<int>&&, <unnamed>::fake_consumer<int>&&\\)\\]. evaluated to .false.")
fixy_neg_compile_test(neg_stage_shape_value_preserving_on_non_stage
  "use of invalid variable template"
  "the expression .\\(arity_v<FnPtr>\\) >= 2 \\[with FnPtr = &nullary_body\\(\\)\\]. evaluated to .false.")

# ── Task #189: the payload row is fail-closed ─────────────────────────
#
# The extractor is entirely compile-time, so this test is first the
# sentinel that compiles its static_assert wall: until Stage.h lands,
# nothing else includes the header.  It also pins the three roster
# cardinalities, which is what makes a new payload family a two-place
# edit a reviewer sees.
fixy_test(test_payload_row)
#
# The old extractor's primary template answered Row<> for a payload
# nobody classified, and an empty row means "needs no capability", so an
# unrecognised wrapper satisfied every context.  Its own header said so.
# The primary is a static_assert now, and these two fixtures are what
# stands on it.  The first probes a wrapper holding an int, where Row<>
# would have been right for the wrong reason; the second probes the same
# wrapper holding a computation engaged at Row<Bg>, where Row<> would
# have let a foreground context admit a background effect.  A check that
# read the wrapper's contents rather than its family would pass the
# first and fail the second, so both are required.  The type arrives in
# the substitution note, which the fixture source does not contain.
fixy_neg_compile_test(neg_payload_row_unclassified_wrapper
  "static assertion failed: payload_row<T>: T is a class template specialization whose family is on none of the "
  "with T = \\{anonymous\\}::UnclassifiedWrapper<int>")
fixy_neg_compile_test(neg_payload_row_unclassified_hides_effect
  "static assertion failed: payload_row<T>: T is a class template specialization whose family is on none of the "
  "with T = \\{anonymous\\}::UnclassifiedWrapper<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>, int> >")

# ── Task #119 (A15): the publication handles ─────────────────────────
#
# The double-publish abort is deliberately not a contract clause — a
# hot-path TU sets the contract semantic to ignore, where a clause
# elides and the second publish quietly no-ops.  Observing an abort
# needs a process that can die, so a passing run prints one
# PublishOnceDoublePublish banner from the child.
fixy_test(test_handle_publish)

# The standing witness for task #18: the guard that admitted
# PublishOnce<int*> and enforced nothing.  A guard that went
# tautological again would make this fixture stop failing.
fixy_neg_compile_test(neg_handle_publish_once_pointer_pointee
  "static assertion failed"
  "PublishOnce<T> hands off a T\\*, so T is the pointee"
  "write PublishOnce<Foo>")

# The slot is a channel identity; a copy gives two slots where the
# publisher believes there is one.
fixy_neg_compile_test(neg_handle_publish_once_copied
  "use of deleted function"
  "fixy::handle::PublishOnce<T>::PublishOnce\\(const fixy::handle::PublishOnce<T>&\\)"
  "copies would duplicate channel state")

# ── The fixy/os smoke tests, migrated out of the headers ─────────────
#
# Each of these used to be a `runtime_smoke_test` inside the header it
# exercises, compiled into every translation unit that included it.
# Removing them is also what let three entries leave
# scripts/ctx-testing-boundary-allowlist.txt: the headers no longer name
# foundation::effects::testing, and the guard fails on a stale entry.
fixy_test(test_os_time)

# The gate and the clamp safety/_Mutation.h's MonotonicClock carried,
# restored to fixy/os/Time.h (#183).  A clock read on the replay-bound
# foreground path makes replay diverge across machines, so the reader is
# minted only by a context owning Bg, Init or Test, and its constructor
# is private with the mint as its only friend.  The first fixture is a
# foreground context refused at the mint: the second regex names the
# ported gate and the third the three effects it demanded, as the
# compiler spells them, which the source does not.  The second fixture
# is direct construction through the ported name, refused as private;
# the constructor is matched with its parentheses, which a brace-spelled
# fixture never contains.
fixy_neg_compile_test(neg_os_clock_reader_foreground_ctx
  "no matching function for call to .mint_clock_reader<foundation::algebra::lattices::ClockSource::Monotonic>\\(const \\{anonymous\\}::ForegroundCtx&\\)"
  "required for the satisfaction of .CtxFitsMonotonicClock<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]"
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init, foundation::effects::Effect::Test\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_clock_reader_direct_ctor
  "is private within this context"
  "ClockReader\\(\\) \\[with foundation::algebra::lattices::ClockSource Source = foundation::algebra::lattices::ClockSource::Monotonic\\]")
fixy_test(test_os_sched)

# ── The pin proof has one door ───────────────────────────────────────
#
# CpuPinned asserts the thread was pinned to its mask.  Three public
# constructors and one free mint used to hand one out with no evidence,
# and a forged single-core pin satisfies IsSingletonCpuPin, which is the
# whole gate on TscReader — so a forged pin reads a per-core counter
# across cores and the replay silently stops reproducing.
#
# fixy::sched::mint_affinity is the sole friend of the sole constructor
# now.  One fixture per closed route, because closing three and leaving
# the fourth open is the failure this discipline keeps finding.  Each
# second regex is a string the diagnostic carries and the fixture source
# does not, so the source echo cannot satisfy its own gate.
fixy_neg_compile_test(neg_os_cpu_pinned_value_ctor
  "is private within this context"
  "CpuPinned\\(Unit\\)")

fixy_neg_compile_test(neg_os_cpu_pinned_default_ctor
  "use of deleted function"
  "a default-constructed CpuPinned would claim a pin nobody performed")

fixy_neg_compile_test(neg_os_cpu_pinned_in_place_ctor
  "no matching function for call"
  "CpuPinned\\(<brace-enclosed initializer list>\\)")

fixy_neg_compile_test(neg_os_cpu_pinned_free_mint
  "is not a member of"
  "mint_cpu_pinned. is not a member of .fixy")

fixy_neg_compile_test(neg_os_cpu_pinned_copied
  "use of deleted function"
  "a pin proof cannot be duplicated")

# ── The mapping and io_uring surfaces get their first positive test ───
#
# Thirteen negative-compile fixtures stand over the headers these were
# ported from, and not one positive case: the corpus says what the gates
# refuse and nothing about what the mints do.  These two make real
# mappings, set up a real ring, and move real bytes, so a mint that
# compiles and returns garbage is caught.
#
# Legs the kernel refuses are skipped rather than failed.  io_uring can
# be off entirely, and copy_file_range refuses some filesystem pairs.
fixy_test(test_os_mmap)
fixy_test(test_os_io)

# ── OwnedMmap has one door, and it is the one that maps ──────────────
#
# The constructor over an address was public, so a caller could claim a
# mapping it never made — and since the destructor unmaps whatever it
# holds, a stack address handed in was a stack address unmapped on scope
# exit.  The constructor is private now and OwnedMmap::map_region
# performs the ::mmap itself.
#
# Two routes, because the region is handed out inside Linear and the
# generic mint_linear forwarder sits between the mints and the object.
# The direct route fails on access; the forwarded route fails because
# there is no reachable constructor to forward to.
fixy_neg_compile_test(neg_owned_mmap_forged_address
  "is private within this context"
  "OwnedMmap\\(void\\*, std::size_t\\)")

fixy_neg_compile_test(neg_owned_mmap_forged_through_mint_linear
  "no matching function for call"
  "mint_linear<.*::Region>\\(void\\*, long unsigned int\\)")

# ── OwnedFile has two doors, and both are the ones that open ─────────
#
# The same defect as OwnedMmap, over a FILE*: the constructor was public
# and the destructor fcloses whatever it holds, so `OwnedFile{stdin}`
# closed standard input on scope exit.  The constructor is private now
# and OwnedFile::open_path / open_temporary perform the open themselves.
#
# Two routes, for the reason given above the mmap pair: the generic
# mint_linear forwarder sits between any mint and the object, so the
# forwarded route fails on the absence of a constructor to forward to.
# The second regex of each is the token GCC prints, taken from its
# output: the typedef name FILE, and an lvalue reference because stdin
# is an lvalue.
fixy_neg_compile_test(neg_owned_file_forged_stream
  "is private within this context"
  "OwnedFile\\(FILE\\*\\)")

fixy_neg_compile_test(neg_owned_file_forged_through_mint_linear
  "no matching function for call"
  "mint_linear<fixy::OwnedFile>\\(FILE\\*&\\)")

# ── The file surface and the three durable writers ───────────────────
#
# Neither header, nor the two they were ported from, ever had a
# positive test; sixteen negative fixtures said what the gates refuse
# and nothing about what the mints do — which is how the cold writer
# shipped pinned to a commit that returned ENOSYS on every call.  The
# test runs the whole sequence in an mkdtemp directory, and its second
# leg is that path: it now has to reach the target.
fixy_test(test_fs)

# The sixteen fixtures, ported from the old tree's v_224 / v_226 / v_228
# families and renamed for what each refuses.  Every second regex is a
# string the compiler emits and the fixture source does not: the
# anonymous-namespace prefix on the context type, the libstdc++ inline
# namespace on the path type, or the deduced call with its `> >`
# spacing.  So the source echo cannot satisfy its own gate.
fixy_neg_compile_test(neg_os_fs_mint_file_ctx_lacks_block
  "no matching function for call"
  "mint_file<.*>\\(\\{anonymous\\}::IoOnlyCtx&")

fixy_neg_compile_test(neg_os_fs_mint_file_duplicate_mode
  "no matching function for call"
  "mode<fixy::fs::open_mode::WriteTruncate> >\\(\\{anonymous\\}::IoBlockCtx&")

fixy_neg_compile_test(neg_os_fs_mint_file_empty_atoms
  "no matching function for call"
  "mint_file<>\\(\\{anonymous\\}::IoBlockCtx&")

fixy_neg_compile_test(neg_os_fs_mint_file_external_path
  "no matching function for call"
  "Tagged<std::filesystem::__cxx11::path, fixy::tags::source::External>\\)")

fixy_neg_compile_test(neg_os_fs_sync_none
  "no matching function for call"
  "sync<fixy::fs::sync_op::None>\\(\\{anonymous\\}::IoBlockCtx&, fixy::fs::OwnedFd&\\)")

fixy_neg_compile_test(neg_os_fs_commit_atomic_external_target
  "no matching function for call"
  "commit_atomic<fixy::fs::atomicity::Rename>\\(.*Tagged<std::filesystem::__cxx11::path, fixy::tags::source::External>\\)")

fixy_neg_compile_test(neg_os_fs_durable_append_ctx_lacks_block
  "no matching function for call"
  "mint_durable_append_file\\(\\{anonymous\\}::IoOnlyCtx&")

fixy_neg_compile_test(neg_os_fs_durable_append_external_path
  "no matching function for call"
  "mint_durable_append_file\\(.*Tagged<std::filesystem::__cxx11::path, fixy::tags::source::External>\\)")

fixy_neg_compile_test(neg_os_fs_durable_truncate_ctx_lacks_block
  "no matching function for call"
  "mint_durable_truncate_file\\(\\{anonymous\\}::IoOnlyCtx&")

fixy_neg_compile_test(neg_os_fs_durable_truncate_external_path
  "no matching function for call"
  "mint_durable_truncate_file\\(.*Tagged<std::filesystem::__cxx11::path, fixy::tags::source::External>\\)")

fixy_neg_compile_test(neg_os_cipher_durable_warm_writer_ctx_lacks_block
  "no matching function for call"
  "mint_warm_writer<>\\(\\{anonymous\\}::IoOnlyCtx&")

fixy_neg_compile_test(neg_os_cipher_durable_warm_writer_extras_engage_mode
  "no matching function for call"
  "mint_warm_writer<fixy::atom::fs::mode<fixy::fs::open_mode::WriteCreate> >\\(\\{anonymous\\}::IoBlockCtx&")

fixy_neg_compile_test(neg_os_cipher_durable_cold_writer_ctx_lacks_block
  "no matching function for call"
  "mint_cold_writer<>\\(\\{anonymous\\}::IoOnlyCtx&")

fixy_neg_compile_test(neg_os_cipher_durable_cold_writer_extras_engage_durable
  "no matching function for call"
  "mint_cold_writer<fixy::atom::fs::durable<fixy::fs::sync_op::Fdatasync> >\\(\\{anonymous\\}::IoBlockCtx&")

fixy_neg_compile_test(neg_os_cipher_durable_head_advancer_ctx_lacks_block
  "no matching function for call"
  "mint_head_advancer<>\\(\\{anonymous\\}::IoOnlyCtx&")

fixy_neg_compile_test(neg_os_cipher_durable_head_advancer_extras_engage_atomic_write
  "no matching function for call"
  "mint_head_advancer<fixy::atom::fs::atomic_write<fixy::fs::atomicity::Rename> >\\(\\{anonymous\\}::IoBlockCtx&")

# ── The witness roster: one fixture per closed line ──────────────────
#
# scripts/witness-roster.txt names every type that attests to a fact it
# cannot see, and scripts/check-witness-roster.sh --gen writes
# neg/neg_witness_<slug>_direct_construction.cpp for each closed line.
# This loop reads the same roster and registers each fixture with the
# two regexes the roster implies: the refusal reason it states, and the
# refused constructor as the compiler prints it — with parentheses,
# which a brace-spelled fixture never contains, so the source cannot
# satisfy its own gate.  The slug and the base name are derived here
# exactly as the generator derives them; the guard's self-test checks
# that ctest registers precisely the fixtures on disk.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/scripts/witness-roster.txt)
file(STRINGS ${CMAKE_SOURCE_DIR}/scripts/witness-roster.txt _witness_lines)
foreach(_witness_line IN LISTS _witness_lines)
  string(STRIP "${_witness_line}" _witness_line)
  if(_witness_line STREQUAL "" OR _witness_line MATCHES "^#")
    continue()
  endif()
  string(REPLACE "|" ";" _witness_fields "${_witness_line}")
  list(LENGTH _witness_fields _witness_field_count)
  if(NOT _witness_field_count EQUAL 5)
    message(FATAL_ERROR
      "scripts/witness-roster.txt: expected 5 fields separated by '|', "
      "got ${_witness_field_count}: ${_witness_line}")
  endif()
  list(GET _witness_fields 0 _witness_type)
  list(GET _witness_fields 3 _witness_reason)
  list(GET _witness_fields 4 _witness_status)
  string(STRIP "${_witness_type}" _witness_type)
  string(STRIP "${_witness_reason}" _witness_reason)
  string(STRIP "${_witness_status}" _witness_status)
  if(NOT _witness_status STREQUAL "closed")
    continue()
  endif()
  # The same derivation as the generator's qualify(): strip every
  # template argument list, innermost first, and drop the root
  # namespace.  fixy::Secret<int> -> Secret, and the compiler prints
  # `Secret(` when it refuses the constructor;
  # fixy::concurrent::PermissionedMpscChannel<int, 8>::ProducerHandle
  # -> concurrent_permissionedmpscchannel_producerhandle, which keeps
  # the two channels' handles apart.
  set(_witness_stripped "${_witness_type}")
  set(_witness_previous "")
  while(NOT _witness_stripped STREQUAL _witness_previous)
    set(_witness_previous "${_witness_stripped}")
    string(REGEX REPLACE "<[^<>]*>" "" _witness_stripped "${_witness_stripped}")
  endwhile()
  string(REGEX REPLACE "^(fixy|foundation)::" "" _witness_stripped "${_witness_stripped}")
  string(REGEX REPLACE "^.*::" "" _witness_base "${_witness_stripped}")
  string(TOLOWER "${_witness_stripped}" _witness_slug)
  string(REGEX REPLACE "[^a-z0-9]+" "_" _witness_slug "${_witness_slug}")
  string(REGEX REPLACE "^_+" "" _witness_slug "${_witness_slug}")
  string(REGEX REPLACE "_+$" "" _witness_slug "${_witness_slug}")
  if(_witness_reason STREQUAL "private")
    set(_witness_why "is private within this context")
  elseif(_witness_reason STREQUAL "deleted")
    set(_witness_why "use of deleted function")
  elseif(_witness_reason STREQUAL "no-match")
    set(_witness_why "no matching function for call")
  else()
    message(FATAL_ERROR
      "scripts/witness-roster.txt: a closed entry's reason must be "
      "private, deleted or no-match; got '${_witness_reason}' on: ${_witness_line}")
  endif()
  fixy_neg_compile_test(neg_witness_${_witness_slug}_direct_construction
    "${_witness_why}"
    "\\b${_witness_base}\\(")
endforeach()

# ── Task #107 (A10.7): the cross-wrapper harness and the cheat probe ──
#
# test_wrapper_verification is the one translation unit where every
# fixy wrapper meets: the diagnostic surface, the layout per storage
# regime, composition at every depth, and one smoke call each.
fixy_test(test_wrapper_verification)

# test_cheat_probe is the adversarial half: fifty-five lookalikes and
# injections, fifty-three asserted refused and two admitted by design
# (cheat 4 and cheat 18).  Each injection is paired with the proof that
# it took on the variable it named, so the refusal is the concept's.
# The old probe admitted twenty-seven trait injections, because a
# detection trait can be specialized from anywhere.  The seventeen with
# a target here are all refused: the new tree's detection is a concept
# over a reflection query and the opt-in is a member, so the
# specialization reaches no gate.
fixy_test(test_cheat_probe)

# The injection that no longer compiles at all: graded_modality's
# primary is constrained to Graded, so specializing it for a fake
# substrate is a constraint failure, not an inert declaration.
fixy_neg_compile_test(neg_cheat_graded_modality_injection
  "template constraint failure"
  "IsGraded<T>. \\[with T = FakeSubstrate\\]")

# ── Task #116 (A13.4): the spin gate, the throws walk, fp, and spawn ──
#
# fixy/Throws.h and fixy/fp/Canonicalize.h carry their whole claim as
# static assertions, so they have no runtime unit of their own; the two
# below are the parts that only a constructed sequence of calls can show.
#
# test_fp_polynomial re-checks at run time every value the header pins at
# compile time.  The two are different arithmetic engines, GCC's constant
# folding and the host FPU, and the claim is that they agree.
fixy_test(test_fp_polynomial)

# test_os_spinlock acquires and releases through every door the gate has.
# The cells that prove a door is CLOSED are the five fixtures below: a
# private-member access is a compile error, not a runtime result.
fixy_test(test_os_spinlock)

# test_os_spawn runs both mints for real: that every child body ran, that
# the parent Permission comes back, and that a recombined region still
# describes the storage it started with.
fixy_test(test_os_spawn)

# Every second regex is text the compiler emits and the fixture source
# does not: a signature with its permission_t parameter spelled out, a
# candidate-arity line, a constraint note naming the concept, or a
# sentence from the header's own static_assert.  So no fixture can satisfy
# its own gate by echoing itself.

# Deviation 1 of fixy/os/SpinLock.h: both doors are private, and lock_in
# is the way in.  The old header had them public, which is how eleven
# production sites bypassed the background-context clause.
fixy_neg_compile_test(neg_spin_lock_door_is_private
  "is private within this context"
  "fixy::spin::SpinLock<Tag>::lock\\(permission_t&\\)")

fixy_neg_compile_test(neg_spin_try_lock_door_is_private
  "is private within this context"
  "fixy::spin::SpinLock<Tag>::try_lock\\(permission_t&\\)")

# Deviation 3: release costs the same witness acquisition did.
fixy_neg_compile_test(neg_spin_unlock_without_witness
  "no matching function for call to .fixy::spin::SpinLock<\\{anonymous\\}::GateTag>::unlock\\(\\)."
  "candidate expects 1 argument, 0 provided")

# Deviation 2: a background context may also allocate, syscall or block.
fixy_neg_compile_test(neg_spin_guard_under_bg_ctx
  "constraints not satisfied"
  "required for the satisfaction of .CtxMayAcquireSpin<Ctx>.")

fixy_neg_compile_test(neg_spin_lock_without_permission
  "no matching function for call to .fixy::spin::SpinGuard<\\{anonymous\\}::GateTag>::SpinGuard\\("
  "candidate expects 3 arguments, 2 provided")

# Deviation 1 of fixy/os/Spawn.h, and the hole fixy/Throws.h closes: the
# callable here carries the atom at a NAMED exception family, which the
# old needle did not match because it compared against the default
# specialization alone.
fixy_neg_compile_test(neg_spawn_callable_carries_throws
  "static assertion failed: mint_spawn: a callable.s type carries fixy::atom::ctrl::throws"
  "tears through the join")

# A mechanism that never joins has to say why.
fixy_neg_compile_test(neg_spawn_detached_missing_rationale
  "static assertion failed: a Detached mechanism with no detach_with atom must be refused"
  "required by the constraints of .template<class Mechanism, class \\.\\.\\. Atoms> concept fixy::spawn::JoinPolicyGrantsCoherent.")

fixy_neg_compile_test(neg_spawn_empty_rationale
  "static assertion failed: detach_with<\"\"> rejected"
  "enumerates every legitimate detach across the codebase")

# The mechanism allowlist is by identity, so a lookalike that declares the
# same member is still refused.
fixy_neg_compile_test(neg_spawn_join_concept_rejects_non_tag
  "static assertion failed: a struct that merely declares a mechanism member must not pass for a declared tag"
  "required by the constraints of .template<class T> concept fixy::spawn::join::IsJoinMechanismTag.")

# canonicalize_for gates on the two axes a content hash depends on.
fixy_neg_compile_test(neg_canonicalize_for_non_bitexact_spec
  "static assertion failed: fixy::fp::canonicalize_for<Spec> requires"
  "cross-vendor drift exceeds 1 ULP")

fixy_neg_compile_test(neg_canonicalize_for_wrong_rounding
  "static assertion failed: fixy::fp::canonicalize_for<Spec> requires RoundingMode::RN"
  "a rounding mode no default kernel")

# The HS14 floor for the two new mints: two fixtures each.  mint_spawn is
# floored by the throws gate above and by the context gate here;
# mint_parallel_for by its context gate and by its body signature.
fixy_neg_compile_test(neg_spawn_mint_spawn_ctx_lacks_bg
  "no matching function for call to .mint_spawn<\\{anonymous\\}::Left, \\{anonymous\\}::Right>\\("
  "required for the satisfaction of .CtxFitsSpawn<Ctx, Parent, std::tuple<_Ts \\.\\.\\.>")

fixy_neg_compile_test(neg_spawn_mint_parallel_for_ctx_lacks_bg
  "constraints not satisfied"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Bg>.")

# Deviation 4: recombine needs every shard back, so a body that consumes
# its shard is refused.
fixy_neg_compile_test(neg_spawn_mint_parallel_for_body_consumes_shard
  "static assertion failed: a body that consumes its shard must be refused"
  "is_nothrow_invocable_v<Body&, fixy::OwnedRegion<T, fixy::Slice<Whole, 0> >&>")
