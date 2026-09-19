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

# A cv-qualified atom fails the same-as clause of IsAtom; the compiler
# names the clause and the two types it compared.
fixy_neg_compile_test(neg_atom_cv_qualified_rejected
  "constraints not satisfied"
  "required for the satisfaction of .IsAtom<G>. \\[with G = const fixy::atom::affine\\]"
  ".const struct fixy::atom::affine. is not the same as .struct fixy::atom::affine.")

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
  "no matching function for call to .fixy::Refined<.*>::Refined\\(<brace-enclosed initializer list>\\)."
  "candidate .*: .constexpr fixy::Refined<Pred, T>::Refined\\(checked_door_, T\\) requires  PredicateInvocableOn<Pred, T>.")

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

# SealedRefined has no into(): name lookup fails on the sealed class.
fixy_neg_compile_test(neg_refined_sealed_into_absent
  "has no member named .into."
  "class fixy::SealedRefined<.*>")

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
