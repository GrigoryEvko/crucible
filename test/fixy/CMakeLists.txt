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
# the old corpus under test/fixy_neg/.
function(fixy_neg_compile_test name)
  set(_diags ${ARGN})
  list(LENGTH _diags _count)
  if(_count LESS 2)
    message(FATAL_ERROR
      "fixy_neg_compile_test(${name}): two required-diagnostic "
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
      "fixy_neg_compile_test(${name}): ${_written} regexes were written and "
      "${_count} survived. An unbalanced `[` in one of them swallowed the list "
      "separator and merged it with the next. Balance the bracket, or match it "
      "with `.`")
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

# A precondition and a postcondition do their checks in a Release build.
# This test compiles with NDEBUG and the observe semantic, as a Release
# library does.  The two options go on the source, after the -UNDEBUG of
# the factory, and the compiler uses the last of each.
fixy_test(test_precondition_checks_in_release)
set_property(SOURCE test_precondition_checks_in_release.cpp APPEND PROPERTY
  COMPILE_OPTIONS -DNDEBUG -fcontract-evaluation-semantic=observe)

# The axis table's primary is defined, so defaulted_axes is what keeps
# it from handing a new axis a claim nobody gave it.  A value one past
# the enum stands in for that axis.
fixy_neg_compile_test(neg_axis_unclassified_axis
  "static assertion failed: an axis off the roster and off the specialization list is classified"
  "required by the constraints of .template<fixy::Axis A> concept fixy::AxisIsClassified."
  "the expression .\\(TakesDefaultTraits<A>\\) == \\(axis_takes_defaults<A>\\) \\[with A = \\(fixy::Axis\\)33\\]. evaluated to .false.")

# The strict pole of each axis is its weakest claim.  A Fact pole that
# states a fact, and a Right pole that claims nothing, are refused.
fixy_neg_compile_test(neg_axis_fact_pole_claims_a_fact
  "static assertion failed: a Fact pole that names an internal source is the weakest claim"
  "required by the constraints of .template<fixy::Claim C, class Pole> concept fixy::PoleFitsClaim."
  "\\[with C = fixy::Claim::Fact. Pole = fixy::tags::source::FromInternal\\]. evaluated to .false.")
fixy_neg_compile_test(neg_axis_right_pole_grants_every_right
  "static assertion failed: a Right pole that claims nothing is the weakest claim"
  "required by the constraints of .template<fixy::Claim C, class Pole> concept fixy::PoleFitsClaim."
  "\\[with C = fixy::Claim::Right. Pole = fixy::pole::Unconstrained<fixy::Axis::Effect>\\]. evaluated to .false.")

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

# A class that compares equal to the One grade is not a grade.  The
# value check of IsConsumeBound admits it, and the type check refuses it.
fixy_neg_compile_test(neg_qtt_grade_that_only_compares_equal
  "template constraint failure for .template<auto Grade, class T> +requires +IsConsumeBound<Grade> class fixy::Qtt."
  "required for the satisfaction of .same_as<decltype \\(Grade\\), foundation::algebra::lattices::QttGrade>. \\[with Grade = <unnamed>::LookalikeGrade\\{\\}\\]")

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

# No mutation wrapper is assignable.  An assignment moves a counter
# backward, empties a set slot, or drops every element of a log, so each
# wrapper deletes both assignments with a message that names what the
# assignment would undo.  One fixture per wrapper and direction.
fixy_neg_compile_test(neg_mutation_monotonic_move_assign
  "use of deleted function .fixy::Monotonic<T, Cmp>& fixy::Monotonic<T, Cmp>::operator=\\(fixy::Monotonic<T, Cmp>&&\\)"
  "Monotonic only advances\\. An assignment would move it backward\\.")
fixy_neg_compile_test(neg_mutation_monotonic_copy_assign
  "use of deleted function .fixy::Monotonic<T, Cmp>& fixy::Monotonic<T, Cmp>::operator=\\(const fixy::Monotonic<T, Cmp>&\\)"
  "Monotonic only advances\\. An assignment would move it backward\\.")
fixy_neg_compile_test(neg_mutation_bounded_monotonic_move_assign
  "use of deleted function .fixy::BoundedMonotonic<T, Max, Cmp>& fixy::BoundedMonotonic<T, Max, Cmp>::operator=\\(fixy::BoundedMonotonic<T, Max, Cmp>&&\\)"
  "BoundedMonotonic only advances\\. An assignment would move it backward\\.")
fixy_neg_compile_test(neg_mutation_bounded_monotonic_copy_assign
  "use of deleted function .fixy::BoundedMonotonic<T, Max, Cmp>& fixy::BoundedMonotonic<T, Max, Cmp>::operator=\\(const fixy::BoundedMonotonic<T, Max, Cmp>&\\)"
  "BoundedMonotonic only advances\\. An assignment would move it backward\\.")
fixy_neg_compile_test(neg_mutation_write_once_move_assign
  "use of deleted function .fixy::WriteOnce<T>& fixy::WriteOnce<T>::operator=\\(fixy::WriteOnce<T>&&\\)"
  "WriteOnce is set at most once\\. An assignment would empty or overwrite a set slot\\.")
fixy_neg_compile_test(neg_mutation_write_once_copy_assign
  "use of deleted function .fixy::WriteOnce<T>& fixy::WriteOnce<T>::operator=\\(const fixy::WriteOnce<T>&\\)"
  "WriteOnce is set at most once\\. An assignment would empty or overwrite a set slot\\.")
fixy_neg_compile_test(neg_mutation_write_once_non_null_move_assign
  "use of deleted function .fixy::WriteOnceNonNull<T\\*>& fixy::WriteOnceNonNull<T\\*>::operator=\\(fixy::WriteOnceNonNull<T\\*>&&\\)"
  "WriteOnceNonNull is set at most once\\. An assignment would empty or overwrite a set slot\\.")
fixy_neg_compile_test(neg_mutation_write_once_non_null_copy_assign
  "use of deleted function .fixy::WriteOnceNonNull<T\\*>& fixy::WriteOnceNonNull<T\\*>::operator=\\(const fixy::WriteOnceNonNull<T\\*>&\\)"
  "WriteOnceNonNull is set at most once\\. An assignment would empty or overwrite a set slot\\.")
fixy_neg_compile_test(neg_mutation_append_only_move_assign
  "use of deleted function .fixy::AppendOnly<T, Storage>& fixy::AppendOnly<T, Storage>::operator=\\(fixy::AppendOnly<T, Storage>&&\\)"
  "AppendOnly never removes an element\\. An assignment would drop every element it holds\\.")
fixy_neg_compile_test(neg_mutation_append_only_copy_assign
  "use of deleted function .fixy::AppendOnly<T, Storage>& fixy::AppendOnly<T, Storage>::operator=\\(const fixy::AppendOnly<T, Storage>&\\)"
  "AppendOnly never removes an element\\. An assignment would drop every element it holds\\.")
fixy_neg_compile_test(neg_mutation_ordered_append_only_move_assign
  "use of deleted function .fixy::OrderedAppendOnly<T, KeyFn, Cmp, Storage>& fixy::OrderedAppendOnly<T, KeyFn, Cmp, Storage>::operator=\\(fixy::OrderedAppendOnly<T, KeyFn, Cmp, Storage>&&\\)"
  "OrderedAppendOnly never removes an element\\. An assignment would drop every element it holds\\.")
fixy_neg_compile_test(neg_mutation_ordered_append_only_copy_assign
  "use of deleted function .fixy::OrderedAppendOnly<T, KeyFn, Cmp, Storage>& fixy::OrderedAppendOnly<T, KeyFn, Cmp, Storage>::operator=\\(const fixy::OrderedAppendOnly<T, KeyFn, Cmp, Storage>&\\)"
  "OrderedAppendOnly never removes an element\\. An assignment would drop every element it holds\\.")

# The bound of a BoundedMonotonic is an integer the carrier holds
# exactly, so no conversion changes it.  GCC separates template
# arguments with a semicolon, which CMake reads as a list separator, so
# the regexes match it with a dot.
fixy_neg_compile_test(neg_mutation_bounded_monotonic_bound_truncates
  "required for the satisfaction of .BoundedMonotoneCarrier<T, Max, Cmp>. \\[with T = unsigned char. Max = 256. Cmp = std::less<unsigned char>\\]"
  "the expression .\\(in_range<T>\\)\\(Max\\) \\[with T = unsigned char. Max = 256\\]. evaluated to .false.")
fixy_neg_compile_test(neg_mutation_bounded_monotonic_negative_bound
  "required for the satisfaction of .BoundedMonotoneCarrier<T, Max, Cmp>. \\[with T = unsigned int. Max = -1. Cmp = std::less<unsigned int>\\]"
  "the expression .\\(in_range<T>\\)\\(Max\\) \\[with T = unsigned int. Max = -1\\]. evaluated to .false.")

# At max(), the precondition of bump() refuses the step.  Under constant
# evaluation, the refusal stops the evaluation.
fixy_neg_compile_test(neg_mutation_bounded_monotonic_bump_past_max
  "in .constexpr. expansion of .counter\\.fixy::BoundedMonotonic<unsigned int, 2, std::less<unsigned int> >::bump\\(\\)."
  ".__builtin_trap\\(\\). is not a constant expression")

# The door of each mutation wrapper refuses a type argument its concept
# does not admit.
fixy_neg_compile_test(neg_mutation_append_only_storage_without_index
  "In substitution of .template<class T, template<class ...> class Storage>  requires  AppendOnlyStorage<T, Storage> constexpr fixy::AppendOnly<T, Storage> fixy::mint_append_only\\(\\) \\[with T = int. Storage = std::__cxx11::list\\]"
  "the required expression .storage\\[index\\]. is invalid")
fixy_neg_compile_test(neg_mutation_append_only_storage_without_append
  "In substitution of .template<class T, template<class ...> class Storage>  requires  AppendOnlyStorage<T, Storage> constexpr fixy::AppendOnly<T, Storage> fixy::mint_append_only\\(\\) \\[with T = int. Storage = std::set\\]"
  "the required expression .mutable_storage\\.emplace_back\\(std::move\\(item\\)\\). is invalid")
fixy_neg_compile_test(neg_mutation_ordered_append_only_storage_without_index
  "fixy::mint_ordered_append_only\\(\\) \\[with T = int. KeyFn = std::identity. Cmp = std::less<void>. Storage = std::__cxx11::list\\]"
  "the required expression .storage\\[index\\]. is invalid")
fixy_neg_compile_test(neg_mutation_ordered_append_only_storage_without_append
  "fixy::mint_ordered_append_only\\(\\) \\[with T = int. KeyFn = std::identity. Cmp = std::less<void>. Storage = std::set\\]"
  "the required expression .mutable_storage\\.emplace_back\\(std::move\\(item\\)\\). is invalid")
fixy_neg_compile_test(neg_mutation_atomic_monotonic_string
  "fixy::mint_atomic_monotonic\\(T\\) .with T = std::__cxx11::basic_string<char>."
  ".std::__cxx11::basic_string<char>. is not trivially copyable")
fixy_neg_compile_test(neg_mutation_atomic_monotonic_user_copy
  "fixy::mint_atomic_monotonic\\(T\\) .with T = .[a-z]+.::Stamp."
  "..[a-z]+.::Stamp. is not trivially copyable")
fixy_neg_compile_test(neg_mutation_write_once_non_null_not_pointer
  "required for the satisfaction of .SlotPointer<Ptr>. \\[with Ptr = int\\]"
  "the expression .is_pointer_v<Ptr> \\[with Ptr = int\\]. evaluated to .false.")
fixy_neg_compile_test(neg_mutation_write_once_non_null_member_pointer
  "required for the satisfaction of .SlotPointer<Ptr>. \\[with Ptr = int .[a-z]+.::Holder::\\*\\]"
  "the expression .is_pointer_v<Ptr> \\[with Ptr = int .[a-z]+.::Holder::\\*\\]. evaluated to .false.")
fixy_neg_compile_test(neg_mutation_monotonic_comparator_not_ordering
  "required for the satisfaction of .MonotoneCarrier<T, Cmp>. \\[with T = unsigned int. Cmp = int\\]"
  ".int. is not invocable by .const unsigned int&, const unsigned int&.")
fixy_neg_compile_test(neg_mutation_write_once_not_movable
  "fixy::mint_write_once\\(\\) \\[with T = .[a-z]+.::Pinned\\]"
  "required for the satisfaction of .move_constructible<T>. \\[with T = .[a-z]+.::Pinned\\]")

# ── The atom catalog ──────────────────────────────────────────────────
fixy_test(test_atom)

# The row check of the hardware-axis blocks (test/fixy/hw_axis_pins.h).
# Each fixture plants a block and a row that disagree, and the report
# names exactly the axis that disagrees.
fixy_neg_compile_test(neg_hw_pins_listed_axis_without_atom
  "static assertion failed: the hardware-axis block planted::site_hw holds no atom alias of the listed axis SimdIsa\\.  Restore the alias"
  "hw_axis_pins::pinned_row<\\^\\^planted::site_hw, fixy::Axis::SimdIsa, fixy::Axis::HwInstruction>::report")
fixy_neg_compile_test(neg_hw_pins_atom_on_unlisted_axis
  "static assertion failed: the hardware-axis block planted::site_hw holds an atom alias of the unlisted axis BarrierStrength\\.  Add the axis to its row\\."
  "hw_axis_pins::pinned_row<\\^\\^planted::site_hw, fixy::Axis::HwInstruction>::report")
fixy_neg_compile_test(neg_hw_pins_row_without_axis
  "static assertion failed: the row of the hardware-axis block planted::site_hw lists no axis, so it states nothing\\."
  "hw_axis_pins::pinned_row<\\^\\^planted::site_hw>::report")

# register_bits_v names a width only for an ISA with a fixed register
# width.  SVE is scalable and Portable names no register, so the
# constraint refuses both.
fixy_neg_compile_test(neg_simd_register_bits_of_a_scalable_isa
  "use of invalid variable template .register_bits_v<fixy::atom::simd::SimdIsa::Sve>."
  "the expression .has_fixed_register_width\\(fixy::atom::simd::SimdIsa\\)\\(Isa\\) \\[with Isa = fixy::atom::simd::SimdIsa::Sve\\]. evaluated to .false.")
fixy_neg_compile_test(neg_simd_register_bits_of_portable
  "use of invalid variable template .register_bits_v<fixy::atom::simd::SimdIsa::Portable>."
  "the expression .has_fixed_register_width\\(fixy::atom::simd::SimdIsa\\)\\(Isa\\) \\[with Isa = fixy::atom::simd::SimdIsa::Portable\\]. evaluated to .false.")

# ── The fn gate ───────────────────────────────────────────────────────
# The resolver, the silent strict defaults and what the gate refuses.
# The negative fixtures for the tiers are in the gate-fixture section.
fixy_test(test_fn)

# ── The collision rules ───────────────────────────────────────────────
# Ten live rules, and twenty-two registered against the axis they wait
# on.  The per-rule negative fixtures are in the hand corpus below.
fixy_test(test_collision)

# ── The corpus, the roles, the insights, the contexts ─────────────────
# Six refused combinations wired into tier 5, seven roles over the
# atoms, one generated insight provider per axis plus six by hand, and
# the five production contexts.  The per-entry negative fixtures are
# in the hand corpus below.
fixy_test(test_corpus)
fixy_test(test_role)
fixy_test(test_insights)
# test_ctx is foundation's sentinel for ExecCtx; this one is fixy's for
# the five named contexts.
fixy_test(test_contexts)

# ── The hand corpus ───────────────────────────────────────────────────
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
# because tier 5 reports WHICH rule or entry refused the pack
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
fixy_neg_compile_test(neg_rule_p002_ghost_vdso_call
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) P002:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_t001_capability_unverified
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) T001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_t001_capability_strict_trust_pole
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) T001:"
  "fixy::fn<int, fixy::atom::capability_usage>::tier5_composition_ok_.+ evaluates to false")
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

# ── The regime family ─────────────────────────────────────────────────
#
# Six rules that could not fire until fixy/atoms/Regime.h gave Axis::Regime
# its first atoms.  Each fixture but one is isolated to its own code, which
# takes work rather than luck: H001's and H002's premises are both
# ABSENCES (the Complexity and Refinement strict poles), so a bare
# `regime::hot` pack trips both, and every other fixture in this group
# carries a cost grade and a refinement witness specifically to silence
# them.  Without those atoms each fixture would floor on a code it does
# not claim.
#
# H003 reads Alloc and IO in the row of the binding, and not the cost, so
# its three fixtures state a constant cost.  One states Alloc, one states
# IO, and one states getpid, a system call that lifts IO and cannot block.
#
# S001 is the one fixture that is not isolated.  A stdio write lifts IO
# and Block, so H003 and W001 refuse a hot write too, and no pack can
# avoid them.  The regex names the three codes, so a fourth code or a
# missing one fails the fixture.
fixy_neg_compile_test(neg_rule_h001_hot_unstated_cost
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_h002_hot_no_witness
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H002:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_h003_hot_alloc_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_h003_hot_io_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_h003_hot_io_syscall
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_h010_hot_bg_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H010:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_r001_coroutine_hot
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) R001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_s001_stdio_hot
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) H003, S001, W001:"
  "${_fixy_tier5_member}")

# ── The wait family ───────────────────────────────────────────────────
#
# Two rules that need fixy/atoms/Sync.h.  W001 reads a tier and the row
# that the whole pack lifts to, so three kinds of atom put Block there: a
# wait strategy that enters the kernel, a stated with<Block>, and a
# system call that can park the caller.  Each has its own fixture.
#
# W001's packs carry a cost grade and a refinement witness, like the
# regime fixtures above, so they floor on W001 rather than on H001 or H002.
# W002's does not need them: its premises are a Bg row and a spin, and
# neither H rule reads a tier the pack never names.
fixy_neg_compile_test(neg_rule_w001_hot_kernel_wait
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_w001_hot_block_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_w001_hot_blocking_syscall
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W001:"
  "${_fixy_tier5_member}")
# A wait on a descriptor lifts Block through the ThreadSync family, as a
# futex call does, so W001 refuses it with no edit to the rule.
fixy_neg_compile_test(neg_rule_w001_hot_epoll_wait
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_w002_bg_row_core_burning_spin
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W002:"
  "${_fixy_tier5_member}")

# ── The observability family ──────────────────────────────────────────
#
# Two rules on one axis, and they are two theorems rather than one read
# two ways.  B002 is the containment — an observability row must be a
# Subrow of the binding's effect row, because Observability names which
# PART of the declared row is observation and is not a second row.  B001
# is the back-pressure trap the catalog already recorded for this axis,
# kept rather than overwritten because a rule code here is stable API.
#
# Each fixture's pack satisfies the other rule, which is what shows the
# two are independent: B002's pack has no Bg row, and B001's declares Bg
# on the Effect axis so its surface is contained.
fixy_neg_compile_test(neg_rule_b002_surface_outside_effect_row
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) B002:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_b001_bg_surface_unbounded
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) B001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_b001_bg_surface_unstated_space
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) B001:"
  "fixy::fn<int, fixy::atom::with<foundation::effects::Effect::Bg>, fixy::atom::observe::surface<foundation::effects::Effect::Bg>, fixy::atom::cost_linear<8>, fixy::atom::as_public>::tier5_composition_ok_.+ evaluates to false")

# ── The hardware-instruction family ───────────────────────────────────
#
# Three rules over one chain, each read at one boundary.  V201 is the hot
# path against a tier at or above the timestamp read; V202 is the
# privileged tier against an effect row with no Init; V203 is a
# replay-deterministic payload against the timestamp tier, and it is the
# first fixture that refuses on the payload rather than the pack.  Each
# pack trips exactly one of the three, which the cells in
# test_collision.cpp pin the other way around.
fixy_neg_compile_test(neg_rule_v201_hot_tsc
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V201:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_v202_privileged_msr_no_init
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V202:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_v203_replay_tsc
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V203:"
  "${_fixy_tier5_member}")

# ── The barrier-strength family ───────────────────────────────────────
#
# One rule at one boundary of the chain: the hot path against a fence at
# or above seq_cst.  The pack carries a cost grade and a refinement
# witness so H001 and H002 stand down, and names no wait strategy and no
# hardware tier, so W001 and V201 do too.
fixy_neg_compile_test(neg_rule_v301_hot_seq_cst
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V301:"
  "${_fixy_tier5_member}")

# ── The memory-scope family ───────────────────────────────────────────
#
# One rule that reads two of the new axes together: a scope at or above
# Cluster against a fence below AcqRel.  The pack names no regime, so
# V301 stands down on the same strength axis.
fixy_neg_compile_test(neg_rule_v401_wide_scope_weak_fence
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V401:"
  "${_fixy_tier5_member}")

# ── The SIMD-ISA family ───────────────────────────────────────────────
#
# Two rules.  V101 refuses on the payload, as V203 does: a replay-
# deterministic band against an ISA pinned to one vendor trunk.  V402
# reads two axes of the pack, a trunk-pinned scope against a trunk-pinned
# ISA, and it is the rule the old catalog carried as a marker trait no
# atom ever set.  Neither fixture is hot, so the regime rules stand down.
fixy_neg_compile_test(neg_rule_v101_replay_pinned_isa
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V101:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_v101_replay_under_alloc_class
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V101:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_v402_scope_across_trunk
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) V402:"
  "${_fixy_tier5_member}")

# ── The floating-point-mode family ────────────────────────────────────
#
# Both rules refuse on the payload's
# replay claim against one setting of the product atom: F101 on
# reassociation, F102 on contraction across statements.  Each fixture's
# mode names one setting and leaves the other at its strict pole, which
# is what makes each trip exactly one of the two.
fixy_neg_compile_test(neg_rule_f101_replay_reassociate
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F101:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f101_replay_under_hot_path
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F101:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f102_replay_contract_fast
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F102:"
  "${_fixy_tier5_member}")

# ── Three rules the old tree carried as marker traits ─────────────────
#
# Each premise is a grade in the new tree, so each rule fires
# structurally.  Two fixtures per rule, one per mismatch class the rule
# reads, and each pack trips its rule alone.  S011's packs state a trust
# grade so T001 stands down; L003's name no suspension and no Bg row so
# L002 stands down.
fixy_neg_compile_test(neg_rule_s011_capability_replay_pure
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) S011:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_s011_capability_replay_philox
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) S011:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_d001_indirect_call_free_function_throws
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) D001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_d001_indirect_call_member_function_throws
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) D001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_d001_indirect_call_opaque_family
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) D001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_d001_indirect_call_ref_qualified_member
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) D001:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_l003_borrow_detached_spawn
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) L003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_l003_borrow_raw_clone
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) L003:"
  "${_fixy_tier5_member}")

# The constant-time family and the failure family.  The constant-time
# claim is the Security grade atom::constant_time, and a failure is the
# payload std::expected<T, E> or the ControlFlow grade ctrl::throws<E>.
# Two fixtures per rule, one per mismatch class the rule reads, and each
# pack trips its rule alone.  The S010 packs also match the corpus entry
# staleness_secret_without_declassify, because a constant-time binding is
# classified; S010 is still the only collision rule each one trips.
fixy_neg_compile_test(neg_rule_f103_ct_rewrite_unrestricted
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F103:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f103_ct_rewrite_with_fast_contraction
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F103:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f104_ct_denormals_unnamed
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F104:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f104_ct_denormals_honoured
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F104:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f105_ct_subnormals_unnamed
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F105:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_f105_ct_subnormals_preserved
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) F105:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_e044_ct_coroutine
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) E044:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_e044_ct_control_flow_suspension
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) E044:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_s010_ct_stale_narrow
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) S010:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_s010_ct_stale_wide
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) S010:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_i004_classified_coroutine_session
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) I004:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_i004_secret_control_flow_session
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) I004:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_i003_ct_returned_failure
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) I003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_i003_ct_thrown_failure
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) I003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_i002_returned_plain_error
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) I002:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_i002_thrown_unseen_family
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) I002:"
  "${_fixy_tier5_member}")

# R004: a suspension, a live session handle and a frame that is not
# linear.  One fixture states the handle with the session atom and
# suspends on ControlFlow.  The other has a session handle as its payload
# and suspends on Reentrancy, so the rule reads the handle from the type.
fixy_neg_compile_test(neg_rule_r004_affine_frame_holds_live_handle
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) R004:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_r004_copy_frame_suspends_handle_payload
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) R004:"
  "${_fixy_tier5_member}")

# W003: the hot regime, a live session handle and no stated wait.  One
# fixture states the handle as an atom, and one carries it as the payload.
fixy_neg_compile_test(neg_rule_w003_hot_live_handle_unstated_wait
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W003:"
  "${_fixy_tier5_member}")
fixy_neg_compile_test(neg_rule_w003_hot_session_payload_unstated_wait
  "${_fixy_tier5_reason}"
  "Collision rule\\(s\\) W003:"
  "${_fixy_tier5_member}")

# The Security grade is read through one closed relation, and its primary
# has no definition.  An atom on the axis that the relation does not name
# stops the build with one message, rather than reading as public.
fixy_neg_compile_test(neg_security_grade_without_class
  "the Security grade of this binding has no entry in fixy::atom::detail::security_class_of_"
  "security_class_or_refuse_.+unlisted_level")

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

# A declassification licenses the channels its policy names and no
# other.  The replay policy leaves an IO row classified, and an export
# policy leaves a Bg crossing classified.
fixy_neg_compile_test(neg_corpus_classified_io_replay_policy
  "${_fixy_tier5_reason}"
  "Corpus entry classified_io_without_declassify"
  "fixy::fn<int, fixy::atom::with<foundation::effects::Effect::IO>, fixy::atom::declassify<fixy::tags::secret_policy::AuthorizedReplay> >::tier5_composition_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_corpus_classified_bg_export_policy
  "${_fixy_tier5_reason}"
  "Corpus entry classified_bg_without_declassify"
  "fixy::fn<int, fixy::atom::with<foundation::effects::Effect::Bg>, fixy::atom::declassify<fixy::tags::secret_policy::AuditedLogging> >::tier5_composition_ok_.+ evaluates to false")

# An entry declared in fixy::corpus and left out of Entries refuses
# nothing, so the walk over the declarations names it.
fixy_neg_compile_test(neg_corpus_entry_declared_not_joined
  "static assertion failed: fixy/Corpus.h: these corpus entries are declared in fixy::corpus and missing from Entries, so they refuse nothing: forgotten_entry\\."
  "Name each one in the Entries tuple")

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
  "The axis graded twice here is Usage"
  "fixy::atom::copy, fixy::atom::affine>::tier4_unique_ok_.+ evaluates to false")

# ── The gate fixtures and the generated TU ────────────────────────────
#
# The hand corpus stands a fixture on each rule and each corpus entry,
# which is the tier-5 half.  This is the rest of the gate: the doors a reader walks into on
# the way to tier 5, and the combinatorial claims that are universal over
# the atom catalog rather than one file per case.
#
# The duplicate-axis fixture above gained a third regex in the same
# commit, because tier 4's message now SPELLS the axis the pack graded
# twice — "The axis graded twice here is Usage" — instead of naming the
# template that names it.  first_duplicated_axis_ always returned an axis
# rather than a pack position; tier4_message_ in fixy/Reject.h is what
# spends that, the way tier5_message_ spends the rule codes.
#
# The eight hand fixtures are the cases NOT already covered elsewhere in
# this tree; the ones that were are listed in the commit message with the
# fixture that covers each, so this block does not duplicate them.  Two
# deserves naming here because the reason it is here is a gap rather
# than a choice:
#
#   neg_secret_transform_returns_reference is the Secret escape that
#   does NOT go through declassify.  Secret has no other public reader,
#   so transform is the only route, and a mapping function returning a
#   reference is the only way to walk around the policy tag.
fixy_neg_compile_test(neg_fn_ctad_attempt
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 0\\]"
  "fn_ctad_blocked_use_mint_fn>::tier0_not_ctad_sentinel_.+ evaluates to false")

# Four payload shapes against one tier.  The second regex is what makes
# them four fixtures rather than four copies: each floors on the OFFENDING
# TYPE as the compiler spells it, so a fixture cannot pass by tripping a
# sibling's case.
fixy_neg_compile_test(neg_fn_payload_array
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 1\\]"
  "fixy::fn<int \\[4\\]>::tier1_payload_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_fn_payload_reference
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 1\\]"
  "fixy::fn<int&>::tier1_payload_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_fn_payload_const
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 1\\]"
  "fixy::fn<const int>::tier1_payload_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_fn_payload_function
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 1\\]"
  "fixy::fn<int\\(\\)>::tier1_payload_ok_.+ evaluates to false")

# A role is an alias for a whole binding, so writing one inside a pack
# asks for a grade that is itself a binding.  The second regex floors on
# the expansion, which is how a reader sees what they actually wrote.
fixy_neg_compile_test(neg_fn_role_alias_as_atom
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]"
  "fixy::fn<int, fixy::fn<int, fixy::atom::copy> >::tier2_atoms_ok_.+ evaluates to false")

# The context gate, refused at the call site rather than inside the body.
# Both regexes name the row and the context in the compiler's own fully
# qualified spelling, which the fixture writes as `fe::` throughout, so
# neither can be satisfied by the quoted source.
fixy_neg_compile_test(neg_role_io_row_under_foreground_ctx
  "required for the satisfaction of .CtxAdmitsBinding<Ctx, Bound>."
  "required for the satisfaction of .CtxAdmits<Ctx, fixy::binding_row_t<F> >."
  "R1 = foundation::effects::Row<foundation::effects::Effect::IO>. R2 = foundation::effects::Row<>")

# The same gate reads the lifts of the atoms.  Each pack states no Effect
# atom, so its Effect grade is the empty row, and the atom alone puts the
# effect in the row the binding requires.  The second regex names that
# row and the row of the context, so each fixture fails for the one
# effect the context lacks: Block for the futex call, IO for the read of
# process state.
fixy_neg_compile_test(neg_fn_futex_row_under_compile_ctx
  "required for the satisfaction of .CtxAdmits<Ctx, fixy::binding_row_t<F> >."
  "R1 = foundation::effects::Row<foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>")
fixy_neg_compile_test(neg_fn_io_syscall_row_under_drain_ctx
  "required for the satisfaction of .CtxAdmits<Ctx, fixy::binding_row_t<F> >."
  "R1 = foundation::effects::Row<foundation::effects::Effect::IO>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>")
# A stdio write lifts IO and Block.  The context holds Block and not IO,
# so the fixture fails for IO alone.
fixy_neg_compile_test(neg_fn_stdio_row_under_block_ctx
  "required for the satisfaction of .CtxAdmits<Ctx, fixy::binding_row_t<F> >."
  "R1 = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Block>")

# The same lift, pointing the other way.  An atom::with in an os mint
# pack widens the row the context has to admit, and this is the fixture
# for that direction: before with<Es...> lifted, the pack failed one
# conjunct earlier because the atom could not be folded at all.  The
# second regex is the folded row itself, IO and Block from the mode atom
# and Bg from the with atom, which is the evidence that the fold reached
# it.
fixy_neg_compile_test(neg_os_fs_mint_file_with_atom_widens_required_row
  "required for the satisfaction of .CtxAdmitsAtomRow<Ctx, Atoms ...>."
  "R1 = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block, foundation::effects::Effect::Bg>")
#   Both refusals are constraints, so the diagnostic names the
#   concept that failed and the atomic expression that decided it, and
#   Secret<int&> is never named because deduction stops before the
#   trailing return type.
fixy_neg_compile_test(neg_secret_transform_returns_reference
  "no matching function for call to .fixy::Secret<int>::transform\\("
  "required for the satisfaction of .TransformReturnsByValue<F, T>."
  "the expression .!\\(is_reference_v<typename std::invoke_result<F, T&&>::type>\\) \\[with F = .*T = int\\]. evaluated to .false.")
fixy_neg_compile_test(neg_secret_transform_returns_void
  "no matching function for call to .fixy::Secret<int>::transform\\("
  "required for the satisfaction of .TransformReturnsNonVoid<F, T>."
  "the expression .!\\(is_void_v<typename std::invoke_result<F, T&&>::type>\\) \\[with F = .*T = int\\]. evaluated to .false.")

# The combinatorial half, as one walk over the atom catalog instead of
# the 566 files the old corpus spent on it.  Registered as a POSITIVE
# test: the claims are static_asserts, so the build failing IS the
# failure, and the run prints the shape of the input set — how many atoms
# were walked, how many are refused alone and which they are, how many
# axes contributed a same-axis pair, how many atoms carry a lift — so a
# reader can judge whether the walk covered anything.
fixy_test(rejections_generated)

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

# The catalog of atoms is closed.  A type with the whole shape of an atom
# is refused when it is declared outside fixy::atom and its families,
# and when it is declared in a file other than the one that seals its
# namespace.  Two fixtures refuse the namespace and two refuse the file.
fixy_neg_compile_test(neg_atom_forged_in_user_namespace
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]: every entry in the pack must be an atom of the closed catalog"
  "fixy::fn<int, user_code::forged_usage>::tier2_atoms_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_atom_forged_in_spoof_namespace
  "static assertion failed"
  ".atom_refusal_v<G>. == fixy::atom::detail::atom_refusal::none \\[with G = user_code::fixy::atom::forged_effect\\]. evaluated to .false.")
fixy_neg_compile_test(neg_atom_planted_in_family
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]: every entry in the pack must be an atom of the closed catalog"
  "fixy::fn<int, fixy::atom::sync::planted_wait>::tier2_atoms_ok_.+ evaluates to false")
fixy_neg_compile_test(neg_atom_specialized_outside_its_header
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]: every entry in the pack must be an atom of the closed catalog"
  "fixy::fn<int, fixy::atom::stdio::write<user_code::private_stream> >::tier2_atoms_ok_.+ evaluates to false")

# The name of an atom is its key in the row hash, so IsAtom refuses an
# atom whose argument has no stable identity: a closure type, and a class
# with internal linkage.  The strict Refinement pole is no witness, so
# refined_with refuses it as its predicate.
fixy_neg_compile_test(neg_atom_refined_with_closure_predicate
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]: every entry in the pack must be an atom of the closed catalog"
  "The type fixy::atom::refined_with<<lambda\\(int\\)> > appears in a fixy::fn pack but is not an atom"
  "refused because an argument names a closure type, an unnamed class or an entity with internal linkage")
fixy_neg_compile_test(neg_atom_from_source_internal_linkage
  "static assertion failed: fixy::fn<Type, Atoms...> \\[tier 2\\]: every entry in the pack must be an atom of the closed catalog"
  "The type fixy::atom::from_source<.anonymous.::private_source> appears in a fixy::fn pack but is not an atom"
  "refused because an argument names a closure type, an unnamed class or an entity with internal linkage")
fixy_neg_compile_test(neg_atom_refined_with_vacuous_predicate
  "template constraint failure for .template<class Pred> +requires +IsRefinementPredicate<Pred> struct fixy::atom::refined_with."
  "the expression .!\\(is_same_v<Pred, fixy::pole::pred::True>\\) \\[with Pred = fixy::pole::pred::True\\]. evaluated to .false.")

# The declassification policies are closed the same way: the namespace
# and the file of the declaration, read by IsDeclassificationPolicy.
fixy_neg_compile_test(neg_declassify_forged_policy
  "template constraint failure for .template<class Policy>  requires  IsDeclassificationPolicy<Policy> struct fixy::atom::declassify."
  "is_catalog_policy_.+\\[with Policy = user_code::forged_release\\]. evaluated to .false.")
fixy_neg_compile_test(neg_declassify_policy_planted_in_catalog
  "template constraint failure for .template<class Policy>  requires  IsDeclassificationPolicy<Policy> struct fixy::atom::declassify."
  "is_catalog_policy_.+\\[with Policy = fixy::tags::secret_policy::PlantedRelease\\]. evaluated to .false.")

# The syscall atoms are closed over the catalog of fixy/atoms/Syscall.h.
# A call id or a family that a cast makes from a number outside the
# catalog is refused at the constraint of its atom.
fixy_neg_compile_test(neg_syscall_per_outside_the_catalog
  "template constraint failure for .template<fixy::atom::syscall::SyscallId Id>  requires  is_catalogued_call_\\(fixy::atom::syscall::SyscallId\\)\\(Id\\) struct fixy::atom::syscall::per."
  "is_catalogued_call_\\(fixy::atom::syscall::SyscallId\\)\\(Id\\) \\[with Id = \\(fixy::atom::syscall::SyscallId\\)99\\]. evaluated to .false.")
fixy_neg_compile_test(neg_syscall_family_outside_the_chain
  "template constraint failure for .template<fixy::atom::syscall::SyscallFamily F>  requires  is_catalogued_family_\\(fixy::atom::syscall::SyscallFamily\\)\\(F\\) struct fixy::atom::syscall::family."
  "is_catalogued_family_\\(fixy::atom::syscall::SyscallFamily\\)\\(F\\) \\[with F = \\(fixy::atom::syscall::SyscallFamily\\)9\\]. evaluated to .false.")

# ── Fixy/Refined.h ────────────────────────────────────────────────────

fixy_test(test_refined)
fixy_test(test_refined_chain_attack)

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

# The relation holds a seal that counts every member at the foot of the
# header.  An edge or a rule family added after it stops the build at the
# first implication query that no earlier line asked.
fixy_neg_compile_test(neg_refined_edge_after_the_header
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .fixy::refined::implies_types<fixy::InRange<0, 7>, fixy::IsPositive>\\(\\).")
fixy_neg_compile_test(neg_refined_family_after_the_header
  "call to non-.constexpr. function .void foundation::fail_closed::detail::a_member_stands_outside_the_seal_of_its_relation\\(\\)."
  "in .constexpr. expansion of .fixy::refined::implies_types<fixy::BoundedBelow<2>, fixy::IsNonZero>\\(\\).")

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

# The fallible door carries the same predicate gate as the checked mint,
# and it takes its refusal as a scoped enum only.
fixy_neg_compile_test(neg_refined_admit_predicate_not_invocable
  "no matching function for call to .admit_refined<fixy::non_null>\\(int, Refusal\\)."
  "required for the satisfaction of .PredicateInvocableOn<Pred, T>. \\[with Pred = fixy::IsNonNull\\{\\}; T = int\\]")
fixy_neg_compile_test(neg_refined_admit_refusal_not_scoped_enum
  "no matching function for call to .admit_refined<fixy::positive>\\(int, int\\)."
  "the expression .is_scoped_enum_v<Error> \\[with Error = int\\]. evaluated to .false.")

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

# The implication relation is transitive, and a chain uses admitted
# steps only.  An edge declared outside admitted_implications does not
# extend a chain, and a chain runs one way.
fixy_neg_compile_test(neg_refined_chain_through_unadmitted_edge
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = fixy::InRange<5, 9>\\{\\}; Q = .*\\]. evaluated to .false.")
fixy_neg_compile_test(neg_refined_chain_wrong_direction
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = fixy::BoundedAbove<20>\\{\\}; Q = fixy::InRange<5, 9>\\{\\}\\]. evaluated to .false.")

# Two distinct predicates that imply each other through steps that do
# not narrow are two names for one predicate.  The walk at the foot of
# the header runs the closure from each edge, and the search stops on
# the cycle, of two steps or of three.
fixy_neg_compile_test(neg_refined_implication_cycle_edges
  "call to non-.constexpr. function .void fixy::refined::implication_cycle_between_two_names_for_one_predicate\\(\\)."
  "in .constexpr. expansion of .fixy::refined::implies_types<planted::<lambda\\(auto:[0-9]+\\)>, fixy::detail::refined_self_test::unreachable_conclusion>\\(\\).")
fixy_neg_compile_test(neg_refined_implication_cycle_ring
  "call to non-.constexpr. function .void fixy::refined::implication_cycle_between_two_names_for_one_predicate\\(\\)."
  "in .constexpr. expansion of .fixy::refined::implies_types<planted::<lambda\\(auto:[0-9]+\\)>, fixy::detail::refined_self_test::unreachable_conclusion>\\(\\).")

# A predicate with a base class borrows none of its base's places in
# the relation, as a premise or as a conclusion.
fixy_neg_compile_test(neg_refined_derived_predicate_premise
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = <unnamed>::AcceptsEverything\\{\\}; Q = fixy::BoundedAbove<20>\\{\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_refined_derived_predicate_conclusion
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = fixy::InRange<5, 9>\\{\\}; Q = <unnamed>::AcceptsNothing\\{\\}\\]. evaluated to .false.")

# Bounds of different signedness are compared by value, so -1 never
# becomes the largest unsigned value inside a step.
fixy_neg_compile_test(neg_refined_mixed_sign_ceiling
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = fixy::BoundedAbove<9>\\{\\}; Q = fixy::BoundedAbove<-1>\\{\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_refined_mixed_sign_floor
  "constraints not satisfied"
  "the expression .implies_v<P, Q> \\[with P = fixy::BoundedBelow<-1>\\{\\}; Q = fixy::BoundedBelow<9>\\{\\}\\]. evaluated to .false.")

# A bounded predicate compares a value with its bound by value, and it is
# defined only on a value type that holds the bound.  The checked mint
# refuses a bound that the value type would change on conversion.
fixy_neg_compile_test(neg_refined_bound_outside_value_type
  "required for the satisfaction of .PredicateInvocableOn<Pred, T>. \\[with Pred = fixy::BoundedBelow<256>\\{\\}; T = unsigned char\\]"
  "the required expression .Pred\\(v\\). is invalid")
fixy_neg_compile_test(neg_refined_negative_bound_on_unsigned
  "required for the satisfaction of .PredicateInvocableOn<Pred, T>. \\[with Pred = fixy::BoundedAbove<-1>\\{\\}; T = unsigned int\\]"
  "the required expression .Pred\\(v\\). is invalid")
fixy_neg_compile_test(neg_refined_divisor_outside_value_type
  "required for the satisfaction of .PredicateInvocableOn<Pred, T>. \\[with Pred = fixy::DivisibleBy<256>\\{\\}; T = unsigned char\\]"
  "the required expression .Pred\\(v\\). is invalid")

# The session payload order weakens a refinement only where the value
# type evaluates both predicates, so a trusted refinement whose bound the
# value type does not hold weakens to nothing.
fixy_neg_compile_test(neg_refined_weaken_bound_outside_value_type
  "the expression .is_payload_subsort_v<Sub, Super> \\[with Sub = fixy::Refinement<fixy::BoundedBelow<256>\\{\\}, unsigned char, false>; Super = fixy::Refinement<fixy::IsPositive\\{\\}, unsigned char, false>\\]. evaluated to .false."
  "no matching function for call to .accept_subsort<fixy::Refined<fixy::BoundedBelow<256>\\(\\), unsigned char>, fixy::Positive<unsigned char> >\\(\\).")
fixy_neg_compile_test(neg_refined_weaken_negative_bound_on_unsigned
  "the expression .is_payload_subsort_v<Sub, Super> \\[with Sub = fixy::Refinement<fixy::BoundedAbove<-1>\\{\\}, unsigned int, false>; Super = fixy::Refinement<fixy::BoundedAbove<0>\\{\\}, unsigned int, false>\\]. evaluated to .false."
  "no matching function for call to .accept_subsort<fixy::Refined<fixy::BoundedAbove<-1>\\(\\), unsigned int>, fixy::Refined<fixy::BoundedAbove<0>\\(\\), unsigned int> >\\(\\).")

# ── Fixy/Tagged.h and fixy/Secret.h ───────────────────────────────────

fixy_test(test_tagged)
fixy_test(test_secret)
fixy_test(test_siphash)
fixy_test(test_federation)

# The admission of a federation peer needs a context whose row holds the
# IO row of the local cipher and of the peer token.  A foreground
# context holds no effect.
fixy_neg_compile_test(neg_federation_admission_foreground_ctx
  "no matching function for call to .foundation::permissions::FederationAdmission<NegFederationOrg>::mint_federation_admission<fixy::federation::policy::admit_orgs<NegFederationOrg> >\\(const \\{anonymous\\}::ForegroundCtx&"
  "required for the satisfaction of .CtxFitsFederationAdmission<Ctx, Org, Policy>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "required for the satisfaction of .CtxAdmitsPermission<fixy::federation::LocalCipherTag, Ctx>.")

# A policy that does not name the organization refuses its admission.
fixy_neg_compile_test(neg_federation_admission_policy_refuses_org
  "no matching function for call to .foundation::permissions::FederationAdmission<NegStrangerOrg>::mint_federation_admission<fixy::federation::policy::admit_orgs<NegAdmittedOrg> >"
  "required for the satisfaction of .CtxFitsFederationAdmission<Ctx, Org, Policy>."
  "the expression .Policy::admits<Org> \\[with Policy = fixy::federation::policy::admit_orgs<NegAdmittedOrg>. Org = NegStrangerOrg\\]. evaluated to .false.")

# A caller cannot name the brand of an admitted peer token.
fixy_neg_compile_test(neg_federation_admittance_names_brand
  "no matching function for call to .foundation::permissions::FederationAdmission<NegBrandOrg>::mint_federation_admittance<\\{anonymous\\}::HeldBrand>"
  "required for the satisfaction of .AdmittanceNamesNoBrand<Explicit \\.\\.\\.>. \\[with Explicit = \\{<unnamed>::HeldBrand\\}\\]")

# An admittance writes the replay window, so a const admission admits
# nobody.
fixy_neg_compile_test(neg_federation_admittance_const_admission
  "passing .const foundation::permissions::FederationAdmission<NegConstOrg>. as .this. argument discards qualifiers"
  "in call to .constexpr std::pair<foundation::permissions::Permission<fixy::federation::LocalCipherTag, LocalBrand>, std::expected<foundation::permissions::Permission<foundation::permissions::tag::FederatedPeer<Org>, Brand>, fixy::federation::AdmittanceError> > foundation::permissions::FederationAdmission<Org>::mint_federation_admittance")

# Verified -> Unverified has no edge in admitted_retags; the member
# retag fails its constraint and the note names RetagAllowed with the
# two tags.
fixy_neg_compile_test(neg_tagged_retag_not_admitted
  "constraints not satisfied"
  "satisfaction of .RetagAllowed<Tag, NewTag>."
  "Tag = fixy::tags::trust::Verified. NewTag = fixy::tags::trust::Unverified")

# An earned tag (a target of a discharge edge) is reached only by retag.
# The factory, the empty-slot door and std::bit_cast each refuse it.
fixy_neg_compile_test(neg_tagged_mint_earned_tag
  "no matching function for call to .mint_tagged<fixy::tags::source::Sanitized>"
  "satisfaction of .MintableTag<Tag>.")

fixy_neg_compile_test(neg_tagged_default_earned_tag
  "no matching function for call to .fixy::Tagged<int, fixy::tags::trust::Verified>::Tagged\\(<brace-enclosed initializer list>\\)."
  "satisfaction of .MintableTag<Tag>.")

fixy_neg_compile_test(neg_tagged_bit_cast_earned_tag
  "no matching function for call to .bit_cast<fixy::Tagged<int, fixy::tags::source::Sanitized> >"
  "is_trivially_copyable_v<_To>")

fixy_neg_compile_test(neg_tagged_start_as_array_earned_tag
  "no matching function for call to .start_as_array<Sanitized>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = fixy::Tagged<int, fixy::tags::source::Sanitized>\\]")

# declassify<Policy>() is the one way out of a Secret: std::bit_cast
# cannot read the value out, because a Secret is not trivially copyable.
fixy_neg_compile_test(neg_secret_bit_cast_out
  "no matching function for call to .bit_cast<int>.fixy::Secret<int>&."
  "is_trivially_copyable_v<_From>")

# A refined value is not trivially copyable, so no byte route builds one,
# and its predicate is a stateless class, so the predicate's type keys the
# row hash of exactly one claim.
fixy_neg_compile_test(neg_refined_bit_cast
  "no matching function for call to .bit_cast<fixy::Refined<"
  "is_trivially_copyable_v<_To>")

fixy_neg_compile_test(neg_refined_start_as_array
  "no matching function for call to .start_as_array<Positive>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = fixy::Refinement<fixy::IsPositive\\{\\}, int, false>\\]")

fixy_neg_compile_test(neg_refined_function_pointer_predicate
  "static assertion failed"
  "the predicate must be a stateless class")

fixy_neg_compile_test(neg_refined_stateful_predicate
  "static assertion failed"
  "A function pointer or a class with state gives two predicates one type")

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

# A classified value cannot steer a branch, an address or an order.  Each
# flow has two fixtures.  The first regex names the deleted operator that
# the compiler selects, and the second is the reason it gives.
fixy_neg_compile_test(neg_secret_if_condition
  "use of deleted function .fixy::Secret<T>::operator bool\\(\\) const .with T = bool"
  "Secret_BranchCondition. a classified value cannot be a branch condition")
fixy_neg_compile_test(neg_secret_logical_not
  "use of deleted function .fixy::Secret<T>::operator bool\\(\\) const .with T = int"
  "Secret_BranchCondition. a classified value cannot be a branch condition")
fixy_neg_compile_test(neg_secret_array_index
  "use of deleted function .fixy::Secret<T>::operator I\\(\\) const .with I = long int. T = long unsigned int"
  "Secret_IndexOrArithmetic. a classified value cannot become an integer, an index or a pointer offset")
fixy_neg_compile_test(neg_secret_pointer_offset
  "use of deleted function .fixy::Secret<T>::operator I\\(\\) const .with I = long int. T = int"
  "Secret_IndexOrArithmetic. a classified value cannot become an integer, an index or a pointer offset")
fixy_neg_compile_test(neg_secret_container_index
  "use of deleted function .fixy::Secret<T>::operator I\\(\\) const .with I = long unsigned int"
  "Secret_IndexOrArithmetic. a classified value cannot become an integer, an index or a pointer offset")
fixy_neg_compile_test(neg_secret_equal_secret
  "use of deleted function .constexpr bool fixy::operator==\\(const Secret<T>&, const U&\\) .with U = Secret<int>"
  "Secret_Compare. a classified value cannot be compared with ==")
fixy_neg_compile_test(neg_secret_not_equal_value
  "use of deleted function .constexpr bool fixy::operator==\\(const Secret<T>&, const U&\\) .with U = int"
  "Secret_Compare. a classified value cannot be compared with ==")
fixy_neg_compile_test(neg_secret_less_than
  "use of deleted function .constexpr std::strong_ordering fixy::operator<=>\\(const Secret<T>&, const U&\\) .with U = Secret<unsigned int>"
  "Secret_Order. a classified value cannot be ordered with <, <=, >, >= or <=>")
fixy_neg_compile_test(neg_secret_three_way_value
  "use of deleted function .constexpr std::strong_ordering fixy::operator<=>\\(const Secret<T>&, const U&\\) .with U = int"
  "Secret_Order. a classified value cannot be ordered with <, <=, >, >= or <=>")

# ── Stale, OwnedRegion, Borrowed, ScopedView, Machine ─────────────────
#
# The region test keeps the file name of the test it is ported from and
# carries a suffix, because test/CMakeLists.txt still registers
# test_owned_region and a CMake target name is global.
fixy_test(test_stale)
fixy_test(test_versioned_budgeted)
fixy_test(test_versioned_budgeted_attacks)
# Legal C++ that tries to make a payload claim a version no source vouched
# for.  An attack that still succeeds is a row of the shrink-only ledger.
fixy_test(test_version_attacks)
# Legal C++ that writes the private state of a proof through a splice.
# An attack that still succeeds is a row of the shrink-only ledger.
fixy_test(test_splice_access_attacks)
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
  "fixy::Borrowed<T, Source, Brand>::Borrowed\\(R&&\\) \\[with R = std::vector<int>.*\\]"
  "Borrowed of a temporary range dangles")

# A braced list reaches the span constructor through span's own
# initializer_list constructor, and deduces no range for the twin, so
# the deleted initializer_list constructor is what refuses it.
fixy_neg_compile_test(neg_borrowed_of_a_braced_list
  "use of deleted function .fixy::Borrowed<T, Source, Brand>::Borrowed\\(std::initializer_list<"
  "Borrowed of a braced list dangles")

# A brand has no data member.  The IsBrand check in each class body
# refuses one that does.
fixy_neg_compile_test(neg_borrowed_ref_brand_not_empty
  "static assertion failed"
  "BorrowedRef<T, Brand>: Brand must be an empty class type")
fixy_neg_compile_test(neg_borrowed_brand_not_empty
  "static assertion failed"
  "Borrowed<T, Source, Brand>: Brand must be an empty class type")

# The doors that take an address are erased, because an address names no
# identity a brand could claim, and a branded borrow never rebrands.
fixy_neg_compile_test(neg_borrowed_ref_branded_from_an_address
  "no matching function for call to .fixy::BorrowedRef<int, .anonymous.::ClaimedBrand>::from_raw_nonnull\\(int\\*\\)."
  "the expression .is_same_v<Brand, foundation::brand::DefaultBrand> \\[with Brand = <unnamed>::ClaimedBrand\\]. evaluated to .false.")
fixy_neg_compile_test(neg_borrowed_branded_from_pointer_and_count
  "no matching function for call to .fixy::Borrowed<int, .anonymous.::Owner, .anonymous.::ClaimedBrand>::Borrowed"
  "required by the constraints of .template<class T, class Source, class Brand> constexpr fixy::Borrowed<T, Source, Brand>::Borrowed\\(T\\*, std::size_t\\) requires +is_same_v<Brand, foundation::brand::DefaultBrand>.")
fixy_neg_compile_test(neg_borrowed_rebrand
  "no matching function for call to .fixy::Borrowed<int, .anonymous.::Owner, fixy::<lambda\\(\\)> >::Borrowed"
  "the expression .is_same_v<Brand, foundation::brand::DefaultBrand> \\[with Brand = fixy::")

# A WeakRef over a reference type has no pointer to form.
fixy_neg_compile_test(neg_weak_ref_of_a_reference
  "template constraint failure for .template<class T> +requires +is_object_v<T> class fixy::WeakRef."
  "the expression .is_object_v<T> \\[with T = int&\\]. evaluated to .false.")

# The four extractors read a member of one wrapper, and each refuses a
# type that has the member but is not that wrapper.
fixy_neg_compile_test(neg_borrowed_value_of_a_span
  "template constraint failure for .template<class T> +requires +IsBorrowed<T> using fixy::borrowed_value_t"
  "required for the satisfaction of .IsBorrowed<T>. \\[with T = std::span<int")
fixy_neg_compile_test(neg_borrowed_source_of_a_look_alike
  "template constraint failure for .template<class T> +requires +IsBorrowed<T> using fixy::borrowed_source_t"
  "required for the satisfaction of .IsBorrowed<T>. \\[with T = <unnamed>::LookalikeBorrow\\]")
fixy_neg_compile_test(neg_borrowed_ref_value_of_a_span
  "template constraint failure for .template<class T> +requires +IsBorrowedRef<T> using fixy::borrowed_ref_value_t"
  "required for the satisfaction of .IsBorrowedRef<T>. \\[with T = std::span<int")
fixy_neg_compile_test(neg_weak_ref_value_of_a_span
  "template constraint failure for .template<class T> +requires +IsWeakRef<T> using fixy::weak_ref_value_t"
  "required for the satisfaction of .IsWeakRef<T>. \\[with T = std::span<int")

# weaken towards a fresher grade in a constant expression reaches the
# CRUCIBLE_PRE trap inside Stale::weaken.
fixy_neg_compile_test(neg_stale_weakened_downwards
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .eight_behind\\.fixy::Stale<int>::weaken\\("
  "fixy/Stale\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# EpochVersioned has no door that raises a version without new evidence.
# A default version, a combine that joins two versions into one payload,
# and a payload write under the current version each make a stale payload
# read as fresh.
fixy_neg_compile_test(neg_epoch_versioned_default_claims_no_version
  "use of deleted function .fixy::EpochVersioned<T>::EpochVersioned\\(\\) .with T = int.."
  "a value that was never produced at a version has no version to report")
fixy_neg_compile_test(neg_epoch_versioned_combine_marks_stale_fresh
  "has no member named .combine_max."
  ".const class fixy::EpochVersioned<int>.")
fixy_neg_compile_test(neg_epoch_versioned_payload_replaced_under_version
  "has no member named .peek_mut."
  ".class fixy::EpochVersioned<int>.")

# Budgeted has no door that claims less use than was measured.  A factory
# that grants the zero grade, a swapped pair of grades, and a payload
# write under the current grade each let a value pass a gate it must fail.
fixy_neg_compile_test(neg_budgeted_free_claims_zero
  ".free. is not a member of"
  ".fixy::Budgeted<int>.")
fixy_neg_compile_test(neg_budgeted_axes_swapped
  "cannot convert .bound_type<foundation::algebra::lattices::counter_tags::peak_bytes>. to .bound_type<foundation::algebra::lattices::counter_tags::bits_budget>."
  "initializing argument 1 of .constexpr fixy::BudgetStamp fixy::BudgetAuthority::grant\\(foundation::algebra::lattices::BitsBudgetBound, foundation::algebra::lattices::PeakBytesBound\\).")
fixy_neg_compile_test(neg_budgeted_payload_replaced_under_grade
  "has no member named .peek_mut."
  ".class fixy::Budgeted<int>.")

# The payload side and the substrate side.  A payload that reaches state
# outside itself, or that a const reference can write, could change under
# the claim with no cast, so each wrapper refuses it and the diagnostic
# names the way out.  The substrate under each wrapper weakens only toward
# the weaker claim, which is the older version and the larger use.
fixy_neg_compile_test(neg_epoch_versioned_reference_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::EpochVersioned."
  ".int&. is not an object type")
fixy_neg_compile_test(neg_epoch_versioned_mutable_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::EpochVersioned."
  "is_same_v<WayOut, fixy::detail::self_contained::nothing_reaches_out> .with WayOut = CachedValue.. evaluated to .false.")
fixy_neg_compile_test(neg_epoch_versioned_pointer_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::EpochVersioned."
  "is_same_v<WayOut, fixy::detail::self_contained::nothing_reaches_out> .with WayOut = int\\*.. evaluated to .false.")
fixy_neg_compile_test(neg_epoch_versioned_view_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::EpochVersioned."
  "is_same_v<WayOut, fixy::detail::self_contained::nothing_reaches_out> .with WayOut = const char\\*.. evaluated to .false.")
fixy_neg_compile_test(neg_budgeted_span_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::Budgeted."
  "is_same_v<WayOut, fixy::detail::self_contained::nothing_reaches_out> .with WayOut = const int\\*.. evaluated to .false.")
fixy_neg_compile_test(neg_budgeted_mutable_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::Budgeted."
  "is_same_v<WayOut, fixy::detail::self_contained::nothing_reaches_out> .with WayOut = Scratchpad.. evaluated to .false.")
fixy_neg_compile_test(neg_epoch_versioned_substrate_weaken_to_newer
  "non-constant condition for static assertion"
  "::weaken<const foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::epoch>.*\\(\\{anonymous\\}::stale, "
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_budgeted_reference_payload
  "template constraint failure for .template<class T>  requires  SelfContained<T> class fixy::Budgeted."
  ".int&. is not an object type")
fixy_neg_compile_test(neg_budgeted_substrate_weaken_below
  "non-constant condition for static assertion"
  "::weaken<const foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::StrongCounterLattice<foundation::algebra::lattices::counter_tags::bits_budget>.*\\(\\{anonymous\\}::measured, "
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# Two values at one version are one event only when their payloads are
# the same, and the equality derived from the members decides that.  A
# payload whose operator== is hand-written is refused, in the payload or in
# a member of it, and the diagnostic names the part.  The moving form fails
# the same constraint in both candidates.
fixy_neg_compile_test(neg_epoch_versioned_select_hand_written_equality
  "no matching function for call to .select_fresher\\(const fixy::EpochVersioned<Reading>&, const fixy::EpochVersioned<Reading>&\\)."
  "is_same_v<Part, fixy::detail::self_contained::every_part_compares> .with Part = Reading.. evaluated to .false.")
fixy_neg_compile_test(neg_epoch_versioned_select_rvalue_hand_written_equality
  "no matching function for call to .select_fresher\\(std::remove_reference<fixy::EpochVersioned<Reading>&>::type, std::remove_reference<fixy::EpochVersioned<Reading>&>::type\\)."
  "(?s)is_same_v<Part, fixy::detail::self_contained::every_part_compares> .with Part = Celsius.. evaluated to .false..*is_same_v<Part, fixy::detail::self_contained::every_part_compares> .with Part = Celsius.. evaluated to .false.")

# No producer states a claim.  A version comes only from a stamp of a
# source, and a budget only from a grant of an authority.  A stamp has a
# private constructor and no trivial one, so it is built neither by a
# caller nor from bytes, and only a context that owns Init mints a source
# or an authority.
fixy_neg_compile_test(neg_epoch_versioned_from_counts
  "no matching function for call to .fixy::EpochVersioned<int>::EpochVersioned\\(<brace-enclosed initializer list>\\)."
  "candidate [0-9]+: .constexpr fixy::EpochVersioned<T>::EpochVersioned\\(T, const fixy::VersionStamp&\\) .with T = int.")
fixy_neg_compile_test(neg_version_stamp_forged
  ".constexpr fixy::VersionStamp::VersionStamp\\(foundation::algebra::lattices::Epoch, foundation::algebra::lattices::Generation\\). is private within this context"
  "fixy::VersionStamp::VersionStamp")
fixy_neg_compile_test(neg_version_stamp_bit_cast
  "no matching function for call to .bit_cast<fixy::VersionStamp>"
  "the expression .is_trivially_copyable_v<_To> .with _To = fixy::VersionStamp.. evaluated to .false.")
fixy_neg_compile_test(neg_version_stamp_start_as_array
  "no matching function for call to .start_as_array<fixy::VersionStamp>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = fixy::VersionStamp.")
fixy_neg_compile_test(neg_version_source_without_init
  "no matching function for call to .mint_version_source"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
fixy_neg_compile_test(neg_version_source_in_foreground
  "no matching function for call to .mint_version_source"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
fixy_neg_compile_test(neg_budget_stamp_forged
  ".constexpr fixy::BudgetStamp::BudgetStamp\\(foundation::algebra::lattices::BitsBudget, foundation::algebra::lattices::PeakBytes\\). is private within this context"
  "fixy::BudgetStamp::BudgetStamp")
# An allowance is spent one time: a stamp does not copy, and a value takes
# it by rvalue only.
fixy_neg_compile_test(neg_budget_stamp_copied_onto_two_values
  "use of deleted function .fixy::BudgetStamp::BudgetStamp\\(const fixy::BudgetStamp&\\)."
  "an allowance is spent one time. a copy would put one grant on two payloads")
fixy_neg_compile_test(neg_budget_stamp_lvalue_grades_a_value
  "no matching function for call to .fixy::Budgeted<int>::Budgeted"
  "cannot bind rvalue reference of type .fixy::BudgetStamp&&. to lvalue of type .fixy::BudgetStamp.")
fixy_neg_compile_test(neg_budget_stamp_bit_cast
  "no matching function for call to .bit_cast<fixy::BudgetStamp>"
  "the expression .is_trivially_copyable_v<_To> .with _To = fixy::BudgetStamp.. evaluated to .false.")
fixy_neg_compile_test(neg_budget_stamp_start_as_array
  "no matching function for call to .start_as_array<fixy::BudgetStamp>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = fixy::BudgetStamp.")
fixy_neg_compile_test(neg_budget_authority_without_init
  "no matching function for call to .mint_budget_authority"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test,")
fixy_neg_compile_test(neg_budget_authority_in_background
  "no matching function for call to .mint_budget_authority"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")

# A source or an authority has no copy and no move, so no byte route
# builds a second one.
fixy_neg_compile_test(neg_version_source_bit_cast
  "no matching function for call to .bit_cast<fixy::VersionSource>"
  "the expression .is_trivially_copyable_v<_To> .with _To = fixy::VersionSource.. evaluated to .false.")
fixy_neg_compile_test(neg_version_source_start_as_array
  "no matching function for call to .start_as_array<fixy::VersionSource>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = fixy::VersionSource.")
fixy_neg_compile_test(neg_budget_authority_bit_cast
  "no matching function for call to .bit_cast<fixy::BudgetAuthority>"
  "the expression .is_trivially_copyable_v<_To> .with _To = fixy::BudgetAuthority.. evaluated to .false.")
fixy_neg_compile_test(neg_budget_authority_start_as_array
  "no matching function for call to .start_as_array<fixy::BudgetAuthority>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = fixy::BudgetAuthority.")

# ── The consumed bands and the F* aliases ─────────────────────────────
fixy_test(test_bands)
fixy_test(test_aliases)

# ── The canonical nesting order ───────────────────────────────────────
fixy_test(test_canonical_order_fixy test_canonical_order.cpp)

# A stack must nest its canonical wrappers at strictly increasing
# positions.  Linear outside HotPath runs the positions downward, and a
# HotPath inside a HotPath repeats one position.
fixy_neg_compile_test(neg_fixy_canonical_order_inverted_two_layer
  "no matching function for call to .accept_canonical_stack<"
  "satisfaction of .CanonicallyOrdered<Stack>. .with Stack = fixy::Qtt<foundation::algebra::lattices::QttGrade::One, foundation::algebra::Graded<")
fixy_neg_compile_test(neg_fixy_canonical_order_duplicate_layer
  "no matching function for call to .accept_canonical_stack<"
  "satisfaction of .CanonicallyOrdered<Stack>. .with Stack = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::HotPathLattice::At<foundation::algebra::lattices::HotPathTier::Hot>, foundation::algebra::Graded<")

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

# The coverage assertions in Bands.h all pass, so none of them
# shows what the walk does when a tier has no alias.  This namespace
# mirrors fixy::hot_path and leaves one of the three tiers out.
fixy_neg_compile_test(neg_bands_tier_without_an_alias
  "static assertion failed: a namespace missing one tier alias covers its enum"
  "the comparison reduces to .\\(0 == 1\\).")

# ScopedFence is a band over a partial order rather than a chain, so it
# can be asked to move to a tier that sits neither above nor below its
# own.  Cta is on the accelerator trunk and
# Inner on the ARM trunk, the two meet only at Thread and System, and the
# same leq gate that refuses a move up refuses this one.  The chain
# fixture above cannot produce the case.
fixy_neg_compile_test(neg_bands_relax_cross_trunk
  "no matching function for call to .relax<foundation::algebra::lattices::MemoryScope::Inner>"
  "band_lattice_t<B>::leq\\)\\(WeakerTier, band_tier_v<B>\\).*evaluated to .false."
  "MemoryScopeLattice::At<foundation::algebra::lattices::MemoryScope::Cta>")

# The admission direction across the same two trunks.  An inner-shareable
# provider is refused at a block-scope requirement, because leq is false
# both ways between the trunks and a gate that admitted it would offer
# the value to observers the fence never reached.
fixy_neg_compile_test(neg_bands_satisfies_cross_trunk
  "no matching function for call to .consume"
  "required for the satisfaction of .covers_cta_requirement<"
  "satisfies_v<Provider, foundation::algebra::lattices::MemoryScope::Cta>.*evaluated to .false.")

# Vendor is the second band over a partial order.  Portable is its top,
# so relax from a named backend up to Portable is a move up, and the leq
# gate refuses it: the value would claim to run on every backend.
fixy_neg_compile_test(neg_bands_vendor_relax_to_portable
  "no matching function for call to .relax<foundation::algebra::lattices::VendorBackend::Portable>"
  "band_lattice_t<B>::leq\\)\\(WeakerTier, band_tier_v<B>\\).*evaluated to .false."
  "VendorLattice::At<foundation::algebra::lattices::VendorBackend::NV>")

# The admission direction between two named backends.  NV and AMD are
# incomparable, so an AMD provider is refused at an NV requirement.
fixy_neg_compile_test(neg_bands_vendor_satisfies_other_backend
  "no matching function for call to .consume"
  "required for the satisfaction of .covers_nv_requirement<"
  "satisfies_v<Provider, foundation::algebra::lattices::VendorBackend::NV>.*evaluated to .false.")

# The residency chain refuses a move up: a Warm value must not claim that
# its working set sits in L1.
fixy_neg_compile_test(neg_bands_residency_heat_relax_up
  "no matching function for call to .relax<foundation::algebra::lattices::ResidencyHeatTag::Hot>"
  "band_lattice_t<B>::leq\\)\\(WeakerTier, band_tier_v<B>\\).*evaluated to .false."
  "ResidencyHeatLattice::At<foundation::algebra::lattices::ResidencyHeatTag::Warm>")

# The admission direction on the residency chain.  A Cold provider is
# refused at a Hot requirement, because the consumer did not budget for
# the cache miss.
fixy_neg_compile_test(neg_bands_residency_heat_cold_at_hot
  "no matching function for call to .consume"
  "required for the satisfaction of .covers_hot_requirement<"
  "satisfies_v<Provider, foundation::algebra::lattices::ResidencyHeatTag::Hot>.*evaluated to .false.")

# A band takes its tier at mint_band and nowhere else.  Braces that pair a
# value with the tier meet no constructor of the substrate, and the
# keyless write in place does not exist, because the tier is a claim about
# the bytes.  The door itself refuses a cv spelling of the band and a
# carrier that is not a band.  The second regex of each names the refused
# candidate or the clause that refused it.
fixy_neg_compile_test(neg_bands_braces_without_door
  "no matching function for call to .foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::DetSafeLattice::At<foundation::algebra::lattices::DetSafeTier::Pure>, int>::Graded\\(<brace-enclosed initializer list>\\)."
  "candidate 1: .template<class Authority> constexpr foundation::algebra::Graded<M, L, T>::Graded\\(const foundation::algebra::grade_key<Authority>&, T, grade_type\\)")
fixy_neg_compile_test(neg_bands_peek_mut_rewrites_tier
  "no matching function for call to .foundation::algebra::Graded<.*DetSafeTier::Pure>, int>::peek_mut\\(\\)."
  "the expression .\\(claim_subject_v<L>\\) == foundation::algebra::ClaimSubject::slot \\[with L = foundation::algebra::lattices::DetSafeLattice::At<foundation::algebra::lattices::DetSafeTier::Pure>\\]. evaluated to .false.")
fixy_neg_compile_test(neg_bands_mint_band_cv_band
  "required for the satisfaction of .ExactBand<B>. .with B = const foundation::algebra::Graded<"
  "required for the satisfaction of .same_as<B, typename std::remove_cvref<_Tp>::type>. .with B = const foundation::algebra::Graded<")
fixy_neg_compile_test(neg_bands_mint_band_not_a_band
  "required for the satisfaction of .ExactBand<B>. .with B = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::ProductLattice<"
  "required for the satisfaction of .IsBand<B>. .with B = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::ProductLattice<")

# A RecipeSpec takes its two axes at mint_recipe_spec and nowhere else.
# Braces meet no constructor, and the door refuses a reference payload and
# a payload that cannot move.
fixy_neg_compile_test(neg_recipe_spec_braces_without_door
  "no matching function for call to .foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::ToleranceLattice>, foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::RecipeFamilyLattice> >, int>::Graded\\(<brace-enclosed initializer list>\\)."
  "candidate 1: .template<class Authority> constexpr foundation::algebra::Graded<M, L, T>::Graded\\(const foundation::algebra::grade_key<Authority>&, T, grade_type\\)")
fixy_neg_compile_test(neg_mint_recipe_spec_reference_payload
  "required for the satisfaction of .RecipeSpecPayload<T>. \\[with T = int&\\]"
  "the expression .is_object_v<T> \\[with T = int&\\]. evaluated to .false.")
fixy_neg_compile_test(neg_mint_recipe_spec_immovable_payload
  "required for the satisfaction of .RecipeSpecPayload<T>. \\[with T = <unnamed>::Pinned\\]"
  "required for the satisfaction of .move_constructible<T>. \\[with T = <unnamed>::Pinned\\]")
# The two axes are stored through their order duals, so weaken() only
# relaxes a claim.  A move from RELAXED to BITEXACT stops the constant
# evaluation at the guard in weaken(), and the second regex names the
# dual lattice that the guard compared in.
fixy_neg_compile_test(neg_recipe_spec_weaken_to_a_tighter_tier
  "foundation/algebra/Graded\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE."
  "expansion of .foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::ToleranceLattice>, .*::weaken<")

# ── The eight value wrappers ──────────────────────────────────────────
#
# The checked test keeps the file name of the test it is ported from and
# carries a suffix, because test/CMakeLists.txt still registers
# test_checked and a CMake target name is global.
fixy_test(test_owned_file)
fixy_test(test_owned_mmap)
fixy_test(test_constant_time)

# A length mismatch in ct::eq is a caller bug.  The precondition traps at
# consteval, and the expansion note names the macro inside the header.
fixy_neg_compile_test(neg_ct_eq_length_mismatch_at_consteval
  "non-constant condition for static assertion"
  "fixy/ConstantTime\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_test(test_cyclic)
fixy_test(test_cyclic_buffer)

# The ring rejects a capacity that is not a power of two.  Its cursor
# finds a slot with a mask, and a mask gives the remainder only for a
# power of two.  The ring also rejects a push of a const element, because
# a push writes into the slot that it gets.
fixy_neg_compile_test(neg_cyclic_buffer_capacity_not_a_power_of_two
  "template constraint failure for .template<class T, long unsigned int N> +requires +N > 0"
  "== 0 class fixy::CyclicBuffer.")
fixy_neg_compile_test(neg_cyclic_buffer_push_of_a_const_element
  "no matching function for call to .fixy::CyclicBuffer<const "
  "is_move_assignable_v<T> .with T = const ")
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

# Each compile-time gate of fixy/Checked.h stops the build at a value the
# destination type cannot hold.  The first regex is the bracketed tag of
# the gate, and the second names the instantiation that fired it.
fixy_neg_compile_test(neg_safe_add_overflow
  "static assertion failed: \\[Checked_Capacity_Overflow\\] safe_add: A \\+ B overflows the destination integer type"
  "In instantiation of .struct fixy::detail::safe_add_impl<unsigned int, 4294967295, 1>.")
fixy_neg_compile_test(neg_safe_sub_underflow
  "static assertion failed: \\[Checked_Capacity_Overflow\\] safe_sub: A - B underflows the destination integer type"
  "In instantiation of .struct fixy::detail::safe_sub_impl<long unsigned int, 10, 100>.")
fixy_neg_compile_test(neg_safe_capacity_overflow
  "static assertion failed: \\[Checked_Capacity_Overflow\\] safe_mul: A \\* B overflows the destination integer type"
  "required from .constexpr const std::size_t fixy::safe_capacity<18446744073709551615, 2>.")
fixy_neg_compile_test(neg_safe_add_all_partial_sum_overflow
  "static assertion failed: \\[Checked_Capacity_Overflow\\] safe_add_all: partial sum overflows the destination integer type"
  "In instantiation of .struct fixy::detail::safe_add_all_impl<unsigned int, 2147483648, 2147483648, 100>.")
fixy_neg_compile_test(neg_bytes_fit_exceeded
  "static assertion failed: \\[Byte_Budget_Exceeded\\] ensure_bytes_fit<Budget, Used>\\(\\): the computed byte usage exceeds the declared budget"
  "fixy::bytes_fit_v<64, 72>. evaluates to false")
fixy_test(test_bits)
fixy_test(test_fixed_array)

# The leak witness is one reflection query over the atom catalog, so no
# translation unit can mint an authorization out of a type of its own.
fixy_neg_compile_test(neg_owned_mmap_release_without_leak_atom
  "no matching function for call to .fixy::OwnedMmap<.*>::release\\("
  "constraints not satisfied"
  "required for the satisfaction of .IsLeakAtom<LeakAtom>."
  "required for the satisfaction of .IsInstanceOf<G, \\^\\^fixy::atom::leak::resource>. \\[with G = <unnamed>::NotALeakAtom\\]")
# The leak concept reads no trait, so a specialization that tries to add
# a class to the set of leak atoms has no template to name.
fixy_neg_compile_test(neg_leak_atom_specialized_trait
  "expected initializer before .<. token"
  "neg_leak_atom_specialized_trait\\.cpp:17:49: error:")

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

# The ninth value-wrapper header.  The old spelling was a pair that included
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

# ── The OS tag namespaces, walked ─────────────────────────────────────
#
# The check reads the nine namespaces rather than a hand roster, so the
# witness that it works is a class it was never told about.  The
# fixture plants one before the header, and both halves of the check
# answer: the shape walk refuses it, and the count pin sees 46 where it
# recorded 45.  fixy::mmap::advice is one of the ten namespaces, so one
# walk covers every tag namespace.
fixy_neg_compile_test(neg_os_tag_namespace_holds_a_non_tag
  "static assertion failed: fixy/atoms/Os\\.h: a class declared in one of the ten tag namespaces has state"
  "static assertion failed: fixy/atoms/Os\\.h: the ten tag namespaces hold a different number of tags"
  "the comparison reduces to .\\(46 == 45\\).")

# ── The binary session core ───────────────────────────────────────────
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

# Entry.h holds the context-bound entry points with the empty permission
# set: mint_session, and with_session, which lends the Resource by move.
fixy_test(test_session_entry)

# The label word of a keyed branch is one value in every translation
# unit.  The two files include the headers in different orders and each
# registers a combinator of its own, and main compares their words.
fixy_test(test_session_wire_word test_session_wire_word.cpp test_session_wire_word_peer.cpp)

# A keyed message is its label word and then its value.  Two roles of a
# projected global type run on two threads over one channel, and each
# side checks the value that it reads.
fixy_test(test_session_projected_value)

# The adversarial campaign: legal code that tries to drop a protocol,
# reuse a handle or make a deadlock.  Each attack runs in a child process
# with a deadline, and the attacks that succeed are pinned on a ledger
# that can only shrink.
fixy_test(test_session_handle_attacks)

# Delegation moves a live handle into a message and keeps its watch
# record live.  The keys of the private surfaces have the sealed shape of
# a passkey.
fixy_test(test_session_endpoint_transfer)

# The handle factory builds a handle at a position that its caller names,
# and the door of the mints opens a session on it.  The builders are
# private, each class is final, and the keys befriend only classes that
# fixy/session/Handle.h defines.  A specialization of a template that
# stands beside the factory gets no access, and neither does a class that
# another translation unit defines under the name of a door.  A second
# definition of a befriended class is a redefinition error.  Each public
# member of the door of the mints states the whole gate of its mint.
fixy_neg_compile_test(neg_sess_handle_factory_called_outside
  "is private within this context"
  "fixy::session::HandleFactory::make_")
fixy_neg_compile_test(neg_sess_handle_factory_derived_look_alike
  "cannot derive from .final. base"
  "HandleFactory")
fixy_neg_compile_test(neg_sess_handle_factory_friend_specialized
  "is private within this context"
  "fixy::session::HandleFactory::make_.Resource, fixy::session::watch::session_ref, std::source_location. .with Proto = fixy::session::Recv<int, fixy::session::End>. Resource = neg_sess_handle_factory_friend_specialized_types::Wire")
fixy_neg_compile_test(neg_sess_handle_base_sibling_specialized
  "is protected within this context"
  "fixy::session::SessionHandleBase<Proto, Derived, Policy>::mark_consumed_.. .with Proto = fixy::session::Send<int, fixy::session::End>")
fixy_neg_compile_test(neg_sess_handle_factory_door_defined_elsewhere
  "is private within this context"
  "fixy::session::HandleFactory::make_.Resource, fixy::session::watch::session_ref, std::source_location. .with Proto = fixy::session::Recv<int, fixy::session::Continue>. Resource = neg_sess_handle_factory_door_defined_elsewhere_types::Wire")
fixy_neg_compile_test(neg_sess_session_open_key_door_defined_elsewhere
  "is private within this context"
  "constexpr fixy::session::SessionOpenKey::SessionOpenKey..")
fixy_neg_compile_test(neg_sess_handle_factory_redefined
  "redefinition of .class fixy::session::HandleFactory."
  "previous definition of .class fixy::session::HandleFactory.")
fixy_neg_compile_test(neg_sess_mint_door_states_the_gate
  "no matching function for call to .fixy::session::SessionMintDoor::open<fixy::session::Send<int, fixy::session::Continue>, neg_sess_mint_door_states_the_gate_types::Wire"
  "satisfaction of .is_well_formed_v<Proto>. .with Proto = fixy::session::Send<int, fixy::session::Continue>.")
fixy_neg_compile_test(neg_sess_mint_door_derived_look_alike
  "cannot derive from .final. base"
  "SessionMintDoor")

# A rewind opens a session at a position inside a protocol, for the
# rollback of fixy/session/Checkpoint.h.  It refuses a position that no
# mint admits from its start, and a handle at End that a callback entry
# owns.
fixy_neg_compile_test(neg_sess_rewind_unresumable
  "no matching function for call to .fixy::session::HandleFactory::rewind<fixy::session::Continue, void>"
  "the expression ..is_resumable_position<R, LoopCtx>... .with R = fixy::session::Continue. LoopCtx = void.. evaluated to .false.")
fixy_neg_compile_test(neg_sess_rewind_branded
  "no matching function for call to .fixy::session::HandleFactory::rewind<fixy::session::End, void>"
  "satisfaction of .carries_brand<EndLoopCtx, void>. .with EndLoopCtx = fixy::session::detail::session_brand<neg_sess_rewind_branded_types::Body, void>.")

# Delegate.h: a message that carries a live endpoint.  The test hands an
# endpoint over a channel, hands an endpoint with the token of its set,
# and checks the contravariant payload order and the refusal of a
# hand-off to a peer of the inner session.  Two fixtures for each
# refusal: no code but the delegation door builds a DelegatedSession, the
# door states the gate of the mint, the tokens of the set travel with the
# endpoint, no protocol hands an endpoint to a peer of its session, and no
# handle inside a Loop or under a brand delegates.
fixy_test(test_session_delegation)
fixy_neg_compile_test(neg_sess_delegation_door_states_the_gate
  "no matching function for call to .fixy::session::DelegationDoor::give"
  "mismatched types .void. and .fixy::session::Loop<fixy::session::Send<int, fixy::session::Continue> >.")
fixy_neg_compile_test(neg_sess_delegated_session_forged
  "no matching function for call to .fixy::session::DelegatedSession<fixy::session::Send<int, fixy::session::End>, neg_sess_delegated_session_forged_types::Wire"
  "DelegatedSession.const fixy::session::DelegationKey&, handle_type&&, hold_type&&.")
fixy_neg_compile_test(neg_sess_delegation_without_hold
  "no matching function for call to .mint_delegated_session"
  "the expression .perm_set_is_empty_v<typename H::perm_set> .with H = fixy::session::SessionHandle<fixy::session::Send<fixy::session::Transferable<int, neg_sess_delegation_without_hold_types::Region>")
fixy_neg_compile_test(neg_sess_delegation_foreign_hold
  "no matching function for call to .mint_delegated_session"
  "required for the satisfaction of .HoldsTokensOf<Hold, H>. .with Hold = fixy::session::PermHold<foundation::permissions::PermSet<neg_sess_delegation_foreign_hold_types::Other> >")
fixy_neg_compile_test(neg_sess_delegated_session_forged_by_bit_cast
  "no matching function for call to .bit_cast<neg_sess_delegated_session_forged_by_bit_cast_types::Carried>"
  "is_trivially_copyable_v<_To>")
fixy_neg_compile_test(neg_sess_delegated_session_forged_by_start_lifetime_as
  "static assertion failed"
  "neg_sess_delegated_session_forged_by_start_lifetime_as_types::Wire, fixy::session::check::detail::tracked_policy<fixy::session::AbandonAction::Abort>, foundation::permissions::PermSet<> >. is not an implicit-lifetime type")
fixy_neg_compile_test(neg_sess_delegation_to_own_peer_send
  "the expression .!.delegates_to_own_peer_v<Proto>. .with Proto = fixy::session::Send<fixy::session::PeerMsg<neg_sess_delegation_to_own_peer_send_types::Bob, neg_sess_delegation_to_own_peer_send_types::Hand,"
  "evaluated to .false.")
fixy_neg_compile_test(neg_sess_delegation_to_own_peer_in_select
  "the expression .!.delegates_to_own_peer_v<Proto>. .with Proto = fixy::session::Select<fixy::session::Send<fixy::session::PeerMsg<neg_sess_delegation_to_own_peer_in_select_types::Carol, neg_sess_delegation_to_own_peer_in_select_types::Keep, int>"
  "evaluated to .false.")
fixy_neg_compile_test(neg_sess_delegation_handle_in_loop
  "the expression .fixy::session::detail::is_delegatable_handle<H>::value .with H = fixy::session::SessionHandle<fixy::session::Send<int, fixy::session::Continue>,"
  "evaluated to .false.")
fixy_neg_compile_test(neg_sess_delegation_branded_handle
  "the expression .fixy::session::detail::is_delegatable_handle<H>::value .with H = fixy::session::SessionHandle<fixy::session::Send<int, fixy::session::End>, neg_sess_delegation_branded_handle_types::Wire, fixy::session::detail::session_brand<neg_sess_delegation_branded_handle_types::Body, void>,"
  "evaluated to .false.")

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

# ── The transition registry and subtyping ────────────────────────────
#
# Every operation of Protocol.h is one fold over the registry in
# fixy::session::combinators, so a node the registry does not know is
# refused where a query first meets it, and the message names it.  The
# involution and empty-choice fixtures compiled before the repair: the
# two old primaries answered for every type they did not know.
fixy_neg_compile_test(neg_sess_unregistered_combinator_involutive
  "static assertion failed: fixy::session::diagnostic \\[Protocol_Unregistered_Combinator\\]"
  "the combinator .*Teleport<int, fixy::session::End> has no registration")
fixy_neg_compile_test(neg_sess_unregistered_combinator_empty_choice
  "static assertion failed: fixy::session::diagnostic \\[Protocol_Unregistered_Combinator\\]"
  "the combinator .*Detour<fixy::session::Select<> > has no registration")
fixy_neg_compile_test(neg_sess_unregistered_combinator_in_dual
  "static assertion failed: fixy::session::diagnostic \\[Protocol_Unregistered_Combinator\\]"
  "the combinator .*Teleport<int, fixy::session::End> has no registration")

# A specialization of a combinator whose nested member disagrees with its
# template arguments is refused at the mint, and a specialized global node
# is refused at the projection.  test_session_node_members checks that the
# node reader of each layer claims each member of each combinator.
fixy_test(test_session_node_members)
fixy_neg_compile_test(neg_sess_combinator_specialized_member
  "fixy::session::diagnostic \\[Protocol_Specialized_Combinator\\]: the node fixy::session::Loop<fixy::session::Recv<int, fixy::session::Continue> >"
  "In instantiation of .consteval bool fixy::session::detail::require_agreeing_members\\(\\) \\[with P = fixy::session::Loop<")
fixy_neg_compile_test(neg_sess_global_node_specialized
  "fixy::session::diagnostic \\[Projection_Specialized_Global_Node\\]: the node fixy::session::global::Branch<neg_sess_global_node_specialized_types::Hello, int, fixy::session::global::End>"
  "In instantiation of .consteval auto fixy::session::detail::proj::project_under\\(\\) \\[with G = fixy::session::global::Comm<")

# Duality keeps the Sender note, so a client and a server that name
# different roles for one choice are refused from either side.
fixy_neg_compile_test(neg_sess_compatible_server_wrong_role
  "fixy::session::diagnostic \\[Subtype_Annotation\\]: assert_compatible_server"
  "because the choice notes differ")
fixy_neg_compile_test(neg_sess_compatible_client_wrong_role
  "fixy::session::diagnostic \\[Subtype_Annotation\\]: assert_compatible_client"
  "because the choice notes differ")
# The combinators, the payload rules and the payload axioms of the
# session layer are sealed at the foot of their headers.  A registration
# that stands outside a seal stops the build: a second registration of
# Send before fixy/session/Protocol.h, and a new combinator from a user
# file after it.  The coherence rules of a registration have their
# witnesses in the self-test of foundation/algebra/Transition.h.
fixy_neg_compile_test(neg_sess_payload_rule_after_seal
  "a_registration_stands_outside_its_seal"
  "expansion of .foundation::algebra::transition::read_payload_rule")
fixy_neg_compile_test(neg_sess_payload_rule_before_seal
  "fixy::session::diagnostic \\[Protocol_Payload_Rule_Outside_Seal\\]"
  "holds a payload rule that its seal does not count")
fixy_neg_compile_test(neg_sess_axiom_after_seal
  "a_registration_stands_outside_its_seal"
  "expansion of .foundation::algebra::transition::detail::subsorts_within")
fixy_neg_compile_test(neg_sess_combinator_registered_twice
  "fixy::session::diagnostic \\[Protocol_Combinator_Outside_Seal\\]"
  "holds a combinator registration that its seal does not count")
fixy_neg_compile_test(neg_sess_combinator_outside_seal
  "a_registration_stands_outside_its_seal"
  "expansion of .foundation::algebra::transition::read_combinator\\(\\^\\^fixy::session::combinators, \\^\\^.*Emit\\)")

# No trait of fixy/session/Protocol.h, Delegate.h, Crash.h or
# Checkpoint.h is a template that a user can specialize.  A class
# spelling is an alias template, so each specialization below is a
# compile error, and the verdicts of the registry stand.  The protocol,
# crash and rollback tests check that each boolean spelling is a concept.
fixy_neg_compile_test(neg_sess_trait_spec_is_send
  "specialization of alias template .template<class P> using fixy::session::is_send = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Send\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_recv
  "specialization of alias template .template<class P> using fixy::session::is_recv = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Recv\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_select
  "specialization of alias template .template<class P> using fixy::session::is_select = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Select\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_offer
  "specialization of alias template .template<class P> using fixy::session::is_offer = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Offer\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_loop
  "specialization of alias template .template<class P> using fixy::session::is_loop = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Loop\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_end
  "specialization of alias template .template<class P> using fixy::session::is_end = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^fixy::session::End\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_continue
  "specialization of alias template .template<class P> using fixy::session::is_continue = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^fixy::session::Continue\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_vendor_pinned
  "specialization of alias template .template<class P> using fixy::session::is_vendor_pinned = "
  "= fixy::session::detail::vendor_view<P>.")
fixy_neg_compile_test(neg_sess_trait_spec_is_terminal_state
  "specialization of alias template .template<class P> using fixy::session::is_terminal_state = "
  "= std::bool_constant<\\(\\(bool\\)terminal_state_of<P>\\(\\)\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_empty_choice
  "specialization of alias template .template<class P> using fixy::session::is_empty_choice = "
  "= std::bool_constant<\\(\\(bool\\)empty_choice_of<P>\\(\\)\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_dual_of
  "specialization of alias template .template<class P> using fixy::session::dual_of = "
  "= fixy::session::detail::dual_view<P>.")
fixy_neg_compile_test(neg_sess_trait_spec_compose
  "specialization of alias template .template<class P, class Q> using fixy::session::compose = "
  "= fixy::session::detail::compose_view<P, Q>.")
fixy_neg_compile_test(neg_sess_trait_spec_compose_at_branch
  "specialization of alias template .template<class P, long unsigned int I, class Q> using fixy::session::compose_at_branch = "
  "= fixy::session::detail::compose_at_branch_view<P, I, Q>.")
fixy_neg_compile_test(neg_sess_trait_spec_is_well_formed
  "specialization of alias template .template<class P, class LoopCtx> using fixy::session::is_well_formed = "
  "= std::bool_constant<\\(\\(bool\\)well_formed_of<P, LoopCtx>\\(\\)\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_offer_sender
  "specialization of alias template .template<class OfferType> using fixy::session::offer_sender = "
  "= std::type_identity<typename OfferType::sender>.")
fixy_neg_compile_test(neg_sess_trait_spec_is_delegate
  "specialization of alias template .template<class P> using fixy::session::is_delegate = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Delegate\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_accept
  "specialization of alias template .template<class P> using fixy::session::is_accept = "
  "= std::bool_constant<fixy::session::detail::head_is\\(\\^\\^P, \\^\\^struct fixy::session::Accept\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_reliable_set
  "specialization of alias template .template<class T> using fixy::session::is_reliable_set = "
  "= std::bool_constant<fixy::session::detail::crash::is_reliable_set_type\\(\\^\\^T\\)>")
fixy_neg_compile_test(neg_sess_trait_spec_is_crash_well_formed
  "specialization of alias template .template<class P> using fixy::session::is_crash_well_formed = "
  "= std::bool_constant<is_crash_well_formed_v<P> >")
fixy_neg_compile_test(neg_sess_trait_spec_is_checkpoint_compliant
  "specialization of alias template .template<class Q> using fixy::session::is_checkpoint_compliant = "
  "= std::bool_constant<fixy::session::detail::checkpoint::pair_is_compliant\\(\\^\\^Q\\)>")
# The inner loop context is an alias over a function that knows each form
# of a loop context, so no specialization sends a Continue elsewhere.
fixy_neg_compile_test(neg_sess_loop_ctx_inner_specialized
  "invalid class name in declaration of .using fixy::session::session_loop_ctx_inner_t = "
  "struct fixy::session::Loop<fixy::session::Recv<int, fixy::session::Continue> >.")

# Commit is a marker that the registry marks as not plain.  The
# well-formedness algebra refuses it at every depth, so a plain mint
# refuses a protocol that commits below a step.
fixy_neg_compile_test(neg_sess_marker_not_plain
  "no matching function for call to .mint_session<Commits>"
  "required for the satisfaction of .is_well_formed_v<Proto>. \\[with Proto = fixy::session::Send<int, fixy::session::Commit<fixy::session::Recv<int, fixy::session::End> > >\\]")

# Well-formedness now asks for a guard between a Continue and its Loop,
# and refuses VendorBackend::None.  Before, the mint gate admitted all
# three and the build failed later inside the handle.
fixy_neg_compile_test(neg_sess_mint_unguarded_loop
  "no matching function for call to .mint_session_handle"
  "well_formed_of<P, void>.\\(\\) \\[with P = fixy::session::Loop<fixy::session::Continue>\\]. evaluated to .false.")
fixy_neg_compile_test(neg_sess_mint_unguarded_pinned_loop
  "no matching function for call to .mint_session_handle"
  "Loop<fixy::session::VendorPinned<foundation::algebra::lattices::VendorBackend::NV, fixy::session::Continue> >\\]. evaluated to .false.")
fixy_neg_compile_test(neg_sess_mint_vendor_none
  "no matching function for call to .mint_session_handle"
  "VendorPinned<foundation::algebra::lattices::VendorBackend::None, fixy::session::Send<.*Ping, fixy::session::End> >\\]. evaluated to .false.")

# Subtyping: one walk gives the verdict and its reason, and each refusal
# names the class of the failure and the failed pair.  The target has a
# suffix because test/CMakeLists.txt still registers test_session_subtype
# for the old tree, and a CMake target name is global.
fixy_test(test_session_subtype_fixy test_session_subtype.cpp)

# The content-addressed marker and its payload are subsorts of each
# other, at any depth.  The suffix avoids the old tree's target name.
fixy_test(test_session_content_addressed_fixy test_session_content_addressed.cpp)
fixy_neg_compile_test(neg_sess_subtype_branch_count
  "\\[Subtype_BranchCount\\]: assert_subtype_sync"
  "because the branch counts are in the wrong direction")
fixy_neg_compile_test(neg_sess_subtype_payload
  "\\[Subtype_Payload\\]: assert_subtype_sync"
  "The failed pair is int against fixy::Refinement<")
fixy_neg_compile_test(neg_sess_subtype_vendor
  "\\[Subtype_Vendor\\]: assert_subtype_sync"
  "VendorBackend::Portable, fixy::session::Send<int, fixy::session::End> > against")
fixy_neg_compile_test(neg_sess_subtype_annotation
  "\\[Subtype_Annotation\\]: assert_subtype_sync"
  "The failed pair is fixy::session::Offer<fixy::session::Sender<.*Alice>")
fixy_neg_compile_test(neg_sess_subtype_evolution_adds_select_branch
  "\\[Subtype_BranchCount\\]: check_protocol_evolution"
  "the new protocol must refine the old one")
fixy_neg_compile_test(neg_sess_subtype_client_not_dual
  "\\[Subtype_Shape\\]: assert_compatible_client"
  "the client must refine the dual of the server")
fixy_neg_compile_test(neg_sess_subtype_external_tag_drop
  "\\[Subtype_Payload\\]: assert_subtype_sync"
  "The failed pair is fixy::Tagged<int, fixy::tags::source::External> against int")
fixy_neg_compile_test(neg_sess_subtype_sealed_refined_drop
  "\\[Subtype_Payload\\]: assert_subtype_sync"
  "fixy::Refinement<.*, int, true> against int")
fixy_neg_compile_test(neg_sess_subtype_ill_formed_operand
  "\\[Subtype_IllFormed\\]: assert_subtype_sync"
  "The failed pair is fixy::session::Loop<fixy::session::Continue> against \\(none\\)")
fixy_neg_compile_test(neg_sess_subtype_unregistered_node
  "fixy::session::diagnostic \\[Subtype_Unregistered_Combinator\\]"
  "the combinator .*Teleport<int, fixy::session::End> has no registration")
fixy_neg_compile_test(neg_sess_subtype_unregistered_in_async
  "fixy::session::diagnostic \\[Subtype_Unregistered_Combinator\\]"
  "the combinator .*Detour<fixy::session::End> has no registration")

# The bounded asynchronous check refuses what it cannot prove: two
# messages ahead on a channel of capacity one, and an input ahead of an
# output at any capacity.
fixy_neg_compile_test(neg_sess_subtype_async_capacity_one
  "fixy::session::diagnostic \\[Subtype_Async_Not_Proven\\]"
  "Channel = .*OneSlot\\]")
fixy_neg_compile_test(neg_sess_subtype_async_input_ahead
  "fixy::session::diagnostic \\[Subtype_Async_Not_Proven\\]"
  "Channel = .*EightSlots\\]")

# The capacity of the check is a property of the channel type.  A number
# in its place does not compile, and a channel type that states no
# capacity is refused before the check runs.
fixy_neg_compile_test(neg_sess_subtype_async_number_capacity
  "type/value mismatch at argument 3"
  "is_subtype_async_v")
fixy_neg_compile_test(neg_sess_subtype_async_silent_channel
  "constraints not satisfied"
  "satisfaction of .StatesChannelCapacity<Channel>. .with Channel = .*SilentEnd")

# Fair termination.  Composition refuses to let a Loop of the prefix
# capture an open Continue of the suffix, refinement keeps each exit of
# the supertype, and compatibility asks for refinement both ways.
fixy_neg_compile_test(neg_sess_compose_captures_continue
  "fixy::session::diagnostic \\[Compose_Captures_Continue\\]: compose_t<P, Q>"
  "with P = fixy::session::Loop<fixy::session::Select<fixy::session::Send<.*Job, fixy::session::Continue>, fixy::session::End> >. Q = fixy::session::Continue")
fixy_neg_compile_test(neg_sess_compose_captures_under_nested_loop
  "fixy::session::diagnostic \\[Compose_Captures_Continue\\]: compose_t<P, Q>"
  "with P = fixy::session::Send<.*Hello, fixy::session::Loop<")
fixy_neg_compile_test(neg_sess_compose_at_branch_captures_continue
  "fixy::session::diagnostic \\[Compose_Captures_Continue\\]: compose_at_branch_t<P, I, Q>"
  "long unsigned int I = 1. Q = fixy::session::Send<.*Retry, fixy::session::Continue>")
fixy_neg_compile_test(neg_sess_subtype_loses_exit
  "fixy::session::diagnostic \\[Subtype_LosesTermination\\]: assert_subtype_sync"
  "the supertype can end from this pair and the subtype cannot")
fixy_neg_compile_test(neg_sess_compatible_client_never_stopped
  "fixy::session::diagnostic \\[Subtype_LosesTermination\\]: assert_compatible_client: the server must refine the dual of the client"
  "the supertype can end from this pair and the subtype cannot")
fixy_neg_compile_test(neg_sess_subtype_async_loses_exit
  "fixy::session::diagnostic \\[Subtype_Async_Not_Proven\\]: assert_subtype_async"
  "with Sub = .*Loop<.*Select<.*Send<.*Job, .*Recv<.*Tick, .*Continue> > > >. Super = .*Loop<.*Recv<.*Tick")
fixy_neg_compile_test(neg_sess_subtype_async_endless_loop
  "fixy::session::diagnostic \\[Subtype_Async_Not_Proven\\]: assert_subtype_async"
  "with Sub = .*Loop<.*Select<.*Send<.*Job, .*Continue> > >. Super = .*Loop<.*Select<.*Send<.*Job, .*Continue>, .*Send<.*StopCmd, .*End> > >")

# An empty choice is not well-formed: under the branch rule it refines
# each larger Select, and a substitute of that type never sends.  Each
# relation refuses it as an operand, the note of an Offer included.
fixy_neg_compile_test(neg_sess_subtype_empty_select
  "\\[Subtype_IllFormed\\]: assert_subtype_sync"
  "The failed pair is fixy::session::Select<> against \\(none\\)")
fixy_neg_compile_test(neg_sess_subtype_empty_offer_note
  "\\[Subtype_IllFormed\\]: assert_subtype_sync"
  "The failed pair is fixy::session::Offer<fixy::session::Sender<.*Alice> > against \\(none\\)")
fixy_neg_compile_test(neg_sess_subtype_async_empty_select
  "fixy::session::diagnostic \\[Subtype_Async_Not_Proven\\]"
  "with Sub = Select<>. Super = Select<Send<")

# The branches of a choice.  A positional choice sends the position of a
# branch as its wire label, so its branches pair by position and a
# permutation of them is refused.  A crash branch pairs by the payload it
# receives, and a subtype that loses one is refused.
fixy_neg_compile_test(neg_sess_subtype_permuted_labels
  "\\[Subtype_Payload\\]: assert_subtype_sync"
  "The failed pair is .*Bye against .*Hello\\.")
fixy_neg_compile_test(neg_sess_subtype_missing_crash_branch
  "\\[Subtype_MissingNonLabelBranch\\]: assert_subtype_sync"
  "because a branch of the supertype that is no label has no branch in the subtype")

# A keyed choice sends a label word, so its branches pair by label in any
# order.  The relation refuses a label that the peer does not accept, a
# keyed choice against a positional one, and one label word with two keys.
fixy_neg_compile_test(neg_sess_subtype_label_set_output
  "fixy::session::diagnostic \\[Subtype_LabelSet\\]: assert_subtype_sync"
  "The failed pair is .*Select<.*Retry, int>.* against .*Select<.*Bye, int>")
fixy_neg_compile_test(neg_sess_subtype_label_set_input
  "fixy::session::diagnostic \\[Subtype_LabelSet\\]: assert_subtype_sync"
  "The failed pair is .*Offer<.*Sender<.*Bob>, .*Recv<.*Hello, int>, .*End> > against .*Offer<.*Bye, int>")
fixy_neg_compile_test(neg_sess_subtype_label_discipline_keyed
  "fixy::session::diagnostic \\[Subtype_LabelDiscipline\\]: assert_subtype_sync"
  "The failed pair is .*Select<.*PeerMsg<.*Hello, int>, .*End> > against fixy::session::Select<fixy::session::Send<int, fixy::session::End> >")
fixy_neg_compile_test(neg_sess_subtype_label_discipline_positional
  "fixy::session::diagnostic \\[Subtype_LabelDiscipline\\]: assert_subtype_sync"
  "The failed pair is fixy::session::Offer<fixy::session::Recv<int, fixy::session::End> > against .*Offer<.*PeerMsg<.*Hello, int>")
fixy_neg_compile_test(neg_sess_subtype_label_word_clash_output
  "fixy::session::diagnostic \\[Subtype_LabelWordClash\\]: assert_subtype_sync"
  "The failed pair is .*Select<.*PeerMsg<.*Bob, .*<lambda\\(\\)>, int>.* against .*Select<.*<lambda\\(\\)>, int>"
  "a stable id refuses the type .*<lambda\\(\\)>.*has no declared name")
fixy_neg_compile_test(neg_sess_subtype_label_word_clash_input
  "fixy::session::diagnostic \\[Subtype_LabelWordClash\\]: assert_subtype_sync"
  "The failed pair is .*Offer<.*Recv<.*PeerMsg<.*Bob, .*<lambda\\(\\)>, int>.* against .*Offer<.*<lambda\\(\\)>, int>"
  "a stable id refuses the type .*<lambda\\(\\)>.*has no declared name")

# Well-formedness of a choice: no message branch after a crash branch,
# no two crash branches with one payload, and no two branches that name
# one label.  The mint gate refuses each through is_well_formed_v.
fixy_neg_compile_test(neg_sess_wf_label_after_crash_branch
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Offer<fixy::session::Recv<fixy::session::Crash<")
fixy_neg_compile_test(neg_sess_wf_repeated_crash_branch
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Offer<fixy::session::Recv<.*Ping, fixy::session::End>, fixy::session::Recv<fixy::session::Crash<")
fixy_neg_compile_test(neg_sess_wf_repeated_label
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Select<fixy::session::Send<fixy::session::PeerMsg<.*Hello, int>")

# A keyed branch sends its label word and a positional branch sends its
# position, so a choice that mixes the two is refused.  Two closure types
# print one name, so their label keys share a word and are refused too.
fixy_neg_compile_test(neg_sess_wf_mixed_label_keys
  "fixy::session::diagnostic \\[Protocol_Choice_Ill_Formed\\]"
  "some label branches name a label key and some do not")
fixy_neg_compile_test(neg_sess_wf_label_word_collision
  "fixy::session::diagnostic \\[Protocol_Choice_Ill_Formed\\]"
  "two label keys of the choice have one label word"
  "a stable id refuses the type .*<lambda\\(\\)>.*has no declared name")
fixy_neg_compile_test(neg_sess_mint_mixed_label_keys
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Select<fixy::session::Send<fixy::session::PeerMsg<.*Hello, int>, fixy::session::End>, fixy::session::Send<int, fixy::session::End>")

# A keyed step is a choice of one branch.  A keyed label branch is its
# label step, and the label step of a keyed Send takes no value: the value
# step after it sends the value.  A keyed step refines a choice by label.
# The rule that a keyed step needs a keyed choice in its registration has
# its witness in the self-test of foundation/algebra/Transition.h.
fixy_neg_compile_test(neg_sess_wf_keyed_label_under_loop
  "fixy::session::diagnostic \\[Protocol_Choice_Ill_Formed\\]"
  "a label branch of a keyed choice starts with a wrapper or a binder")
fixy_neg_compile_test(neg_sess_wf_keyed_label_under_vendor_pin
  "fixy::session::diagnostic \\[Protocol_Choice_Ill_Formed\\]"
  "a label branch of a keyed choice starts with a wrapper or a binder")
fixy_neg_compile_test(neg_sess_keyed_send_with_value
  "use of deleted function"
  "Keyed_Label_Takes_No_Value\\] a Send of a PeerMsg or a Labelled is keyed")
fixy_neg_compile_test(neg_sess_keyed_send_with_value_peer_msg
  "use of deleted function"
  "Keyed_Label_Takes_No_Value\\] a Send of a PeerMsg or a Labelled is keyed")
fixy_neg_compile_test(neg_sess_subtype_keyed_step_label_set_input
  "fixy::session::diagnostic \\[Subtype_LabelSet\\]: assert_subtype_sync"
  "The failed pair is .*Offer<.*L1, int>.* against .*Recv<.*L2, int>")
fixy_neg_compile_test(neg_sess_subtype_keyed_step_label_set_output
  "fixy::session::diagnostic \\[Subtype_LabelSet\\]: assert_subtype_sync"
  "The failed pair is .*Send<.*L2, int>.* against .*Select<.*L0, int>")

# The adversarial campaign against the algebra and the relations.  The
# runtime half runs two threads over bounded queues with a watchdog, so
# a deadlock ends the run with a verdict and never hangs.  The attacks
# that succeed are pinned on a ledger that can only shrink.
fixy_test(test_session_subtype_attack)

# HS14 for mint_session_handle.  Its requires-clause is the whole
# admission gate, so each clause gets a fixture that fails through it
# and through no other: an ill-formed protocol, a protocol with an empty
# choice, and a resource whose address is not stable.
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

# A copyable Resource that reaches a channel is a second channel to the
# peer, so SessionResource refuses it: once for a struct that holds a
# pointer, and once for a raw pointer.
fixy_neg_compile_test(neg_sess_resource_copy_reaches_channel
  "no matching function for call to .mint_session_handle<Once>\\(resource_copy_fixture::SharedWire\\)"
  "satisfaction of .SessionResource<Resource>. \\[with Resource = resource_copy_fixture::SharedWire\\]"
  "no operand of the disjunction is satisfied")
fixy_neg_compile_test(neg_sess_resource_raw_pointer
  "no matching function for call to .mint_session_handle<Once, resource_pointer_fixture::Queue\\*>"
  "satisfaction of .SessionResource<Resource>. \\[with Resource = resource_pointer_fixture::Queue\\*\\]"
  "no operand of the disjunction is satisfied")

# A new-expression gives a handle storage that nothing has to free, so
# the protocol never ends.  The handle deletes its allocation functions,
# and a smart pointer that allocates reaches the same deleted function.
fixy_neg_compile_test(neg_sess_handle_heap_new
  "use of deleted function .static void\\* fixy::session::SessionHandleBase<"
  "\\[Session_Handle_On_Heap\\]")
fixy_neg_compile_test(neg_sess_handle_make_unique
  "use of deleted function .static void\\* fixy::session::SessionHandleBase<"
  "\\[Session_Handle_On_Heap\\]")

# No public mint gives the two endpoints of one channel to one caller.
# The resource-only form is deleted, and the test hatch refuses the
# background and the foreground contexts, which hold no Test capability.
fixy_neg_compile_test(neg_sess_channel_resource_only_mint_is_deleted
  "use of deleted function .void fixy::session::mint_channel"
  "\\[Channel_Needs_A_Fork\\]")
fixy_neg_compile_test(neg_sess_test_channel_outside_a_test
  "no matching function for call to .mint_test_channel"
  "required for the satisfaction of .CtxFitsTestChannel<Ctx, Proto>."
  "CtxOwnsCapability<Ctx, foundation::effects::Effect::Test>")
fixy_neg_compile_test(neg_sess_test_channel_from_the_foreground
  "no matching function for call to .mint_test_channel"
  "required for the satisfaction of .CtxFitsTestChannel<Ctx, Proto>."
  "ctx_cap::Fg")

# The fork-shaped mint starts two threads, so its context must hold Bg,
# and each body must return its endpoint at End.
fixy_neg_compile_test(neg_sess_forked_channel_without_bg
  "no matching function for call to .mint_forked_channel"
  "CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag>"
  "CtxOwnsCapability<Ctx, foundation::effects::Effect::Bg>")
fixy_neg_compile_test(neg_sess_forked_channel_body_stops_early
  "no matching function for call to .mint_forked_channel"
  "ForkedEndpointBody<PeerBody"
  "the required expression .std::move\\(handle\\).close\\(\\). is invalid")
# One context runs the two sides of a channel, so each channel mint reads
# the row gate of a session for each side (CtxAdmitsChannelRow).  A
# payload whose row the context does not hold is refused.
fixy_neg_compile_test(neg_sess_forked_channel_row_outside_ctx
  "no matching function for call to .mint_forked_channel<forked_row_fixture::Proto, forked_row_fixture::Left, forked_row_fixture::Right>"
  "satisfaction of .CtxAdmitsChannelRow<Ctx, Proto, typename fixy::session::detail::dual_view<[A-Za-z0-9]+>::type>."
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, SelfProto>. .with Ctx = .*SelfProto = fixy::session::Send<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int>, fixy::session::End>.")
fixy_neg_compile_test(neg_sess_test_channel_row_outside_ctx
  "no matching function for call to .mint_test_channel<test_row_fixture::Proto>"
  "satisfaction of .CtxAdmitsChannelRow<Ctx, Proto, typename fixy::session::detail::dual_view<[A-Za-z0-9]+>::type>."
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, SelfProto>. .with Ctx = .*SelfProto = fixy::session::Send<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>, int>, fixy::session::End>.")

# A forked channel whose sides are related by asynchronous subtyping runs
# the check at the capacity that its two ends state.  A pair that sends
# further ahead than that capacity is refused, and an end that states no
# capacity is refused.
fixy_test(test_session_async_channel)
fixy_neg_compile_test(neg_sess_async_channel_capacity_too_small
  "no matching function for call to .mint_forked_async_channel"
  "is_subtype_async_v<SelfProto, typename fixy::session::detail::dual_view<PeerProto>::type, ResourceSelf>")
fixy_neg_compile_test(neg_sess_async_channel_without_capacity
  "no matching function for call to .mint_forked_async_channel"
  "StatesChannelCapacity<ResourceSelf>. \\[with ResourceSelf = .unnamed.::SilentEnd\\]")
# Refinement keeps each exit, and duality does not keep that property, so
# the gate asks for refinement in each direction.  A pair in which one
# side never sends the stop that the other waits for is refused in each
# order.  GCC prints the parameter of dual_of under a name that can drift.
fixy_neg_compile_test(neg_sess_async_channel_peer_drops_the_exit
  "no matching function for call to .mint_forked_async_channel"
  "is_subtype_async_v<PeerProto, typename fixy::session::detail::dual_view<[A-Za-z0-9]+>::type, ResourcePeer> \\[with PeerProto = fixy::session::Loop<fixy::session::Select<fixy::session::Send<.unnamed.::Job, fixy::session::Continue> > >")
fixy_neg_compile_test(neg_sess_async_channel_self_drops_the_exit
  "no matching function for call to .mint_forked_async_channel"
  "is_subtype_async_v<SelfProto, typename fixy::session::detail::dual_view<PeerProto>::type, ResourceSelf> \\[with SelfProto = fixy::session::Loop<fixy::session::Select<fixy::session::Send<.unnamed.::Job, fixy::session::Continue> > >")
fixy_neg_compile_test(neg_sess_async_channel_row_outside_ctx
  "no matching function for call to .mint_forked_async_channel<async_row_fixture::LeftProto, async_row_fixture::RightProto, async_row_fixture::Left, async_row_fixture::Right>"
  "satisfaction of .CtxAdmitsChannelRow<Ctx, SelfProto, PeerProto>."
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, SelfProto>. .with Ctx = .*SelfProto = fixy::session::Send<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int>, fixy::session::End>.")

# The callback entry point closes the handle the body returns, so the
# body must return it at End.
fixy_neg_compile_test(neg_sess_with_session_body_stops_early
  "no matching function for call to .with_session"
  "SessionBody<Body, Proto, Resource, Policy>"
  "the required expression .std::move\\(handle\\).close\\(\\). is invalid")
fixy_neg_compile_test(neg_sess_with_session_forged_end_handle
  "no matching function for call to .with_session"
  "SessionBody<Body, Proto, Resource, Policy>"
  "satisfaction of .carries_brand<typename H::loop_ctx, Brand>.")

# check::Cancel needs a Resource that can send a cancellation.
fixy_neg_compile_test(neg_sess_cancel_policy_without_channel
  "static assertion failed: fixy::session::diagnostic \\[Cancel_Needs_A_Channel\\]"
  "CancellableResource<.anonymous.::SilentWire>")

# The permission set.  End refuses an open loan.  The mint walks every
# path of the protocol, so it refuses a send of a region the set does not
# hold, an iteration that changes the set, and an arm that leaves a loan
# open, also when no run selects that arm.
fixy_neg_compile_test(neg_sess_end_with_open_loan
  "static assertion failed: fixy::session::diagnostic \\[PermissionImbalance\\]: the protocol reaches End"
  "the expression .!.perm_set_has_open_loan_v<PS>. .with PS = foundation::permissions::PermSet<fixy::session::LentOut<.unnamed.::Region> >")
fixy_neg_compile_test(neg_sess_send_unheld_permission
  "no matching function for call to .mint_session_handle"
  "PermissionFlowCloses<Proto, foundation::permissions::PermSet<> >. \\[with Proto = fixy::session::Send<foundation::permissions::Permission<")
fixy_neg_compile_test(neg_sess_mint_unselected_arm_sends_unheld_permission
  "no matching function for call to .mint_session_handle"
  "PermissionFlowCloses<Proto, foundation::permissions::PermSet<> >. \\[with Proto = fixy::session::Select<fixy::session::End, fixy::session::Send<")
# HS14 for mint_permissioned_session.  Its gate is one concept, and each
# fixture fails one clause of it: the permission flow on three shapes of
# protocol, the row of a tag, and the empty set.  A token passed as an
# lvalue fails at its copy, before the gate.
fixy_neg_compile_test(neg_sess_loop_iteration_sends_permission
  "no matching function for call to .mint_permissioned_session<loop_iteration_fixture::Forever>"
  "satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<Tags ...> >. .with Proto = fixy::session::Loop<fixy::session::Send<"
  "permission_flow_<Proto, PS, void>::closes")
fixy_neg_compile_test(neg_sess_unselected_arm_leaves_loan_open
  "no matching function for call to .mint_permissioned_session<unselected_arm_fixture::Proto>"
  "satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<Tags ...> >. .with Proto = fixy::session::Select<fixy::session::End, fixy::session::Send<fixy::session::Borrowed<int, unselected_arm_fixture::Region>"
  "permission_flow_<Proto, PS, void>::closes")
fixy_neg_compile_test(neg_sess_permissioned_session_flow_open
  "no matching function for call to .mint_permissioned_session<flow_open_fixture::SendsTwice>"
  "satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<Tags ...> >. .with Proto = fixy::session::Send<foundation::permissions::Permission<flow_open_fixture::Region"
  "permission_flow_<Proto, PS, void>::closes")
fixy_neg_compile_test(neg_sess_permissioned_session_row_refused
  "no matching function for call to .mint_permissioned_session<row_refused_fixture::SendsRegion>"
  "CtxAdmitsPermission<Tags, Ctx> && \\.\\.\\.\\) .with Tags = \\{row_refused_fixture::DiskRegion\\}"
  "evaluated to .false.")
fixy_neg_compile_test(neg_sess_permissioned_session_no_tokens
  "no matching function for call to .mint_permissioned_session<no_tokens_fixture::SendsPing>\\(const foundation::effects::detail::ctx_witnesses::BgWitness&, no_tokens_fixture::Wire\\)"
  "sizeof \\.\\.\\. \\(Tags \\.\\.\\.\\) != 0 .with Tags = \\{\\}"
  "evaluated to .false.")
fixy_neg_compile_test(neg_sess_permissioned_session_token_kept
  "use of deleted function .foundation::permissions::Permission<Tag, Brand>::Permission\\(const foundation::permissions::Permission<Tag, Brand>&\\) .with Tag = token_kept_fixture::Region"
  "duplicating creates two simultaneous owners of the same region"
  "initializing argument 3 of .constexpr auto fixy::session::mint_permissioned_session")
# HS14 for mint_session of Entry.h.  Its gate is CtxFitsSessionFrom with no
# tag, and each fixture fails one clause: the context, the permission flow
# from the empty set, and the Resource.
fixy_neg_compile_test(neg_sess_mint_session_not_a_context
  "no matching function for call to .mint_session<not_a_context_fixture::Proto>"
  "satisfaction of .IsExecCtx<Ctx>. .with Ctx = int."
  "satisfaction of .CtxFitsSession<Ctx, Proto, Resource>.")
fixy_neg_compile_test(neg_sess_mint_session_flow_open
  "no matching function for call to .mint_session<flow_open_fixture::SendsRegion>"
  "satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<Tags ...> >. .with Proto = fixy::session::Send<fixy::session::Transferable<int, flow_open_fixture::Region>, fixy::session::End>. Tags = \\{\\}"
  "permission_flow_<Proto, PS, void>::closes")
fixy_neg_compile_test(neg_sess_mint_session_pointer_resource
  "no matching function for call to .mint_session<pointer_resource_fixture::Proto>"
  "satisfaction of .SessionResource<Resource>. .with Resource = pointer_resource_fixture::Channel\\*."
  "satisfaction of .CtxFitsSession<Ctx, Proto, Resource>.")
# The context must hold each effect that a payload of the session carries:
# a sent payload, a received payload, and a payload of a protocol that
# the session delegates.  A payload that the walk cannot classify stops
# the build.
fixy_neg_compile_test(neg_sess_mint_session_send_row_outside_ctx
  "no matching function for call to .mint_session<send_row_fixture::Proto>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. .with Ctx = .*. Proto = fixy::session::Send<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int>, fixy::session::End>.")
fixy_neg_compile_test(neg_sess_mint_session_recv_row_outside_ctx
  "no matching function for call to .mint_session<recv_row_fixture::Proto>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. .with Ctx = .*. Proto = fixy::session::Recv<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int>, fixy::session::End>.")
fixy_neg_compile_test(neg_sess_mint_session_delegated_row_outside_ctx
  "no matching function for call to .mint_session<delegated_row_fixture::Proto>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. .with Ctx = .*. Proto = fixy::session::Send<int, fixy::session::Send<fixy::session::DelegatedSession<fixy::session::Send<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int>")
fixy_neg_compile_test(neg_sess_mint_session_payload_unclassified
  "static assertion failed: payload_row<T>: T is, or holds, a type this extractor cannot classify"
  "The refused type: unclassified_fixture::Envelope<int>"
  "In instantiation of .struct fixy::concurrent::payload_row_under<fixy::session::Send<unclassified_fixture::Envelope<int>, fixy::session::End>, fixy::session::session_payload_families>.")
# A receive can deliver a permission, in a message or in the set of a
# delegated endpoint.  The context must admit the row of each delivered
# region, and a delivered tag that declares no row stops the build.
fixy_neg_compile_test(neg_sess_mint_session_receives_region_outside_ctx
  "no matching function for call to .mint_session<receives_region_fixture::Proto>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. .with Ctx = .*Proto = fixy::session::Recv<fixy::session::Transferable<int, receives_region_fixture::IoRegion>, fixy::session::End>."
  "is_subrow_v<typename fixy::session::detail::permission_rows_of_set<.*>::type, typename Ctx::row_type> .with Proto = fixy::session::Recv<fixy::session::Transferable<int, receives_region_fixture::IoRegion>, fixy::session::End>")
fixy_neg_compile_test(neg_sess_mint_session_delegated_region_outside_ctx
  "no matching function for call to .mint_session<delegated_region_fixture::Proto>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. .with Ctx = .*Proto = fixy::session::Recv<fixy::session::DelegatedSession<fixy::session::End, delegated_region_fixture::Wire, "
  "is_subrow_v<typename fixy::session::detail::permission_rows_of_set<.*>::type, typename Ctx::row_type> .with Proto = fixy::session::Recv<fixy::session::DelegatedSession<")
fixy_neg_compile_test(neg_sess_mint_session_delivered_region_without_row
  "static assertion failed: fixy::session::diagnostic \\[Delivered_Region_Without_Row\\]: a receive of the protocol delivers a permission of the region region_without_row_fixture::BareRegion, and its tag declares no permission row"
  "In instantiation of .struct fixy::session::detail::union_of_permission_rows<foundation::effects::Row<>, region_without_row_fixture::BareRegion>.")
# The context-bound with_session lends its Resource for the body only.
# A body that returns before End, keeps the handle, or returns the End
# handle of another session fails the body gate, and a Resource passed as
# an lvalue fails at its copy.
fixy_neg_compile_test(neg_sess_ctx_with_session_returns_open_handle
  "no matching function for call to .with_session<returns_open_fixture::Stream>"
  "satisfaction of .CtxFitsSessionBody<Ctx, Proto, Resource, Policy, Body>."
  "the required expression .std::move\\(handle\\).close\\(\\). is invalid")
fixy_neg_compile_test(neg_sess_ctx_with_session_keeps_handle
  "no matching function for call to .with_session<keeps_handle_fixture::Stream>"
  "satisfaction of .CtxFitsSessionBody<Ctx, Proto, Resource, Policy, Body>."
  "the required type .typename H::loop_ctx. is invalid")
fixy_neg_compile_test(neg_sess_ctx_with_session_foreign_end
  "no matching function for call to .with_session<foreign_end_fixture::Stream>"
  "satisfaction of .CtxFitsSessionBody<Ctx, Proto, Resource, Policy, Body>."
  "satisfaction of .carries_brand<typename H::loop_ctx, Brand>.")
fixy_neg_compile_test(neg_sess_ctx_with_session_copies_resource
  "use of deleted function .copies_resource_fixture::Channel::Channel\\(const copies_resource_fixture::Channel&\\)."
  "initializing argument 2 of .constexpr Resource fixy::session::with_session\\(const Ctx&, Resource, Body, std::source_location\\)")

# ── The machine and Vigil-mode session bridges ────────────────────────
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

# The address of a cell is its identity, so a cell with the read shape
# that is not Pinned is refused by the same concept.
fixy_neg_compile_test(neg_sess_atomic_cell_not_pinned
  "no matching function for call to .mint_atomic_session<Once>\\(const foundation::effects::detail::ctx_witnesses::BgWitness&, loose_cell_fixture::LoosePhaseCell&\\)"
  "satisfaction of .AtomicMachineCell<Cell>. \\[with Cell = loose_cell_fixture::LoosePhaseCell\\]"
  "_Base = foundation::Pinned<loose_cell_fixture::LoosePhaseCell>")

# An atomic session and a view of a machine are sessions, so the context
# admits the effect row of each payload, as the context of mint_session
# does.
fixy_neg_compile_test(neg_sess_atomic_ctx_row_refused
  "no matching function for call to .mint_atomic_session<SendsIo>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
fixy_neg_compile_test(neg_sess_machine_view_ctx_row_refused
  "no matching function for call to .fixy::session::SessionFromMachine<neg_sess_machine_view_ctx_row_refused_types::Payload,"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")

# HS14 for mint_vigil_mode_bridge.  The two fixtures separate the two
# ways to fail the cell clause: an unrelated type, and a cell that
# satisfies the structural concept and is still refused, which is what
# says the gate is nominal rather than structural.
fixy_neg_compile_test(neg_sess_vigil_bridge_wrong_cell
  "no matching function for call to .mint_vigil_mode_bridge"
  "satisfaction of .CtxFitsVigilModeBridge<Ctx, Cell>."
  "long unsigned int")

fixy_neg_compile_test(neg_sess_vigil_bridge_lookalike_cell
  "no matching function for call to .mint_vigil_mode_bridge"
  "satisfaction of .CtxFitsVigilModeBridge<Ctx, Cell>."
  "LookalikeCell")

# The transition relation itself: DIVERGED has no edge in either
# direction, so forming the transition type is where the pair is
# refused.
fixy_neg_compile_test(neg_sess_vigil_illegal_transition
  "static assertion failed"
  "fixy::session::diagnostic \\[VigilModeBridge_IllegalTransition\\]"
  "admitted_mode_transitions")

# ── Crash-stop sessions ───────────────────────────────────────────────
#
# Crash.h holds the rules of Barwell, Hou, Yoshida and Zhou (LMCS 2025),
# and CrashTransport.h runs a handle under them.  The test runs the
# paper's Example 3.2 under three schedules.
fixy_test(test_session_crash_stop)

# HS14 for mint_crash_session.  Rule 3 refuses a bare reception from an
# unreliable peer, also when the reception hides in a loop body.  Rule 5
# refuses a crash branch for a reliable peer.  The cell must outlive the
# session, so a temporary cell is refused.
fixy_neg_compile_test(neg_sess_crash_bare_recv_unreliable
  "no matching function for call to .mint_crash_session"
  "every_reception_handles_crash_v<Proto, Peer, Reliable>. \\[with Proto = fixy::session::Send<int, fixy::session::Recv<int")

fixy_neg_compile_test(neg_sess_crash_hidden_in_loop
  "no matching function for call to .mint_crash_session"
  "every_reception_handles_crash_v<Proto, Peer, Reliable>. \\[with Proto = fixy::session::Loop<")

fixy_neg_compile_test(neg_sess_crash_branch_for_reliable
  "no matching function for call to .mint_crash_session"
  "every_reception_handles_crash_v<Proto, Peer, Reliable>"
  "Reliable = fixy::session::ReliableSet<.unnamed.::Bob>")

fixy_neg_compile_test(neg_sess_crash_cell_temporary
  "use of deleted function .void fixy::session::mint_crash_session"
  "Crash_Cell_Temporary")

# The context of a crash session admits the effect row of each payload.
fixy_neg_compile_test(neg_sess_crash_ctx_row_refused
  "no matching function for call to .mint_crash_session<ReceivesIo,"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")

# The door of the crash session builds each decorator and does each step.
# Its opening member states the whole gate of the mint, and the class is
# final.  The decorator befriends the door alone, so a specialization of
# the decorator reaches no constructor of another.
fixy_neg_compile_test(neg_sess_crash_door_states_the_gate
  "no matching function for call to .fixy::session::CrashSessionDoor::open<fixy::session::Recv<int, fixy::session::End>,"
  "satisfaction of .every_reception_handles_crash_v<Proto, Peer, Reliable>. \\[with Proto = fixy::session::Recv<int, fixy::session::End>")
fixy_neg_compile_test(neg_sess_crash_door_derived_look_alike
  "cannot derive from .final. base"
  "CrashSessionDoor")
fixy_neg_compile_test(neg_sess_crash_watched_friend_specialized
  "is private within this context"
  "fixy::session::CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::CrashWatched\\(Handle, const fixy::session::PeerCrashCell&")

# Rules 1 and 2, and Stop as runtime syntax, hold in every session, so
# the plain handle factory refuses them.
fixy_neg_compile_test(neg_sess_crash_label_sent
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Select<fixy::session::Send<int, fixy::session::End>, fixy::session::Send<fixy::session::Crash<")

fixy_neg_compile_test(neg_sess_crash_pure_choice
  "no matching function for call to .mint_session_handle"
  "is_empty_choice_v<Proto>\\) \\[with Proto = fixy::session::Send<int, fixy::session::Offer<fixy::session::Recv<fixy::session::Crash<")

fixy_neg_compile_test(neg_sess_crash_stop_at_design_time
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Send<int, fixy::session::Stop>\\]")

# HS14 for crash(): a reliable role cannot crash, and an ended process
# cannot crash.
fixy_neg_compile_test(neg_sess_crash_reliable_role_crashes
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Of_Reliable_Role\\]")

fixy_neg_compile_test(neg_sess_crash_after_end
  "no matching function for call to .fixy::session::CrashWatched<"
  "is_terminal_state_v<P>\\) \\[with P = fixy::session::End\\]")

fixy_neg_compile_test(neg_sess_crash_branch_index_missing
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Branch_Missing\\]")

# The crash-stop theory has no delegation, so no payload may hold a
# delegated endpoint, alone or in a member.  The mint starts from the
# empty set, so a token it never received cannot be sent.
fixy_neg_compile_test(neg_sess_crash_hidden_in_delegation
  "no matching function for call to .mint_crash_session"
  "satisfaction of .is_delegation_free_v<Proto>. \\[with Proto = fixy::session::Offer<fixy::session::Recv<fixy::session::DelegatedSession")
fixy_neg_compile_test(neg_sess_crash_delegation_in_a_member
  "no matching function for call to .mint_crash_session"
  "satisfaction of .is_delegation_free_v<Proto>. \\[with Proto = fixy::session::Offer<fixy::session::Recv<.*Envelope")
# A handle and a pointer to one delegate as the hand-off marker does.  The
# payload walk refuses each in every session, so the permission flow of
# the crash mint does not close.  A pointer to void names no endpoint, and
# only the crash theory, which has no delegation, refuses it.
fixy_neg_compile_test(neg_sess_crash_delegation_by_handle
  "no matching function for call to .mint_crash_session"
  "required for the satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<> >. .with Proto = fixy::session::Offer<fixy::session::Recv<fixy::session::SessionHandle<")
fixy_neg_compile_test(neg_sess_crash_delegation_behind_pointer
  "no matching function for call to .mint_crash_session"
  "required for the satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<> >. .with Proto = fixy::session::Offer<fixy::session::Recv<.*Envelope")
fixy_neg_compile_test(neg_sess_crash_delegation_opaque_pointer
  "no matching function for call to .mint_crash_session"
  "satisfaction of .is_delegation_free_v<Proto>. \\[with Proto = fixy::session::Offer<fixy::session::Recv<void\\*, fixy::session::End>")
fixy_neg_compile_test(neg_sess_crash_token_not_held
  "no matching function for call to .mint_crash_session"
  "PermissionFlowCloses<Proto, foundation::permissions::PermSet<> >. \\[with Proto = fixy::session::Select<fixy::session::Send<fixy::session::Transferable<")
# A keyed message names its sender or its target, and the crash gate
# refuses one that is not the watched peer and not reliable.
fixy_neg_compile_test(neg_sess_crash_keyed_sender_unwatched
  "no matching function for call to .mint_crash_session<neg_sess_crash_keyed_sender_unwatched_types::FromQ,"
  "required for the satisfaction of .every_reception_handles_crash_v<Proto, Peer, Reliable>. \\[with Proto = fixy::session::Recv<fixy::session::PeerMsg<neg_sess_crash_keyed_sender_unwatched_types::Q,")
fixy_neg_compile_test(neg_sess_crash_keyed_target_unwatched
  "no matching function for call to .mint_crash_session<neg_sess_crash_keyed_target_unwatched_types::ToQ,"
  "required for the satisfaction of .is_every_sender_watched_v<Proto, Peer, Reliable>. \\[with Proto = fixy::session::Send<fixy::session::PeerMsg<neg_sess_crash_keyed_target_unwatched_types::Q,")
fixy_neg_compile_test(neg_sess_crash_between_label_and_payload
  "fixy::session::diagnostic \\[Crash_Splits_A_Message\\]"
  "CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::crash\\(")
# The label word of a keyed message and its value are one message too.
fixy_neg_compile_test(neg_sess_crash_between_keyed_label_and_value
  "fixy::session::diagnostic \\[Crash_Splits_A_Message\\]"
  "CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::crash\\(.*\\[with P = fixy::session::Send<int, fixy::session::End>. Handle = fixy::session::SessionHandle<fixy::session::Send<int, fixy::session::End>, neg_sess_crash_between_keyed_label_and_value_types::Wire")
fixy_neg_compile_test(neg_sess_crash_recorded_between_label_and_payload
  "fixy::session::diagnostic \\[Crash_Splits_A_Message\\]"
  "required from .fixy::session::Recorded<Inner>::resource_type fixy::session::Recorded<Inner>::crash\\(")
fixy_neg_compile_test(neg_sess_crash_recv_blocking_read
  "fixy::session::diagnostic \\[Crash_Read_Must_Poll\\]: recv\\(\\)"
  "CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::recv\\(Read\\)")
fixy_neg_compile_test(neg_sess_crash_branch_blocking_poll
  "fixy::session::diagnostic \\[Crash_Read_Must_Poll\\]: branch\\(\\)"
  "CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::branch\\(Poll, Handler\\)")
fixy_neg_compile_test(neg_sess_crash_token_send_without_refusal
  ": \\[Crash_Write_Must_Try\\] the write of a crash session is a trying write"
  "use of deleted function .void fixy::session::CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::send\\(typename P::message_type, Transport\\)")
fixy_neg_compile_test(neg_sess_crash_recorded_token_send_without_refusal
  ": \\[Crash_Write_Must_Try\\] the write of a crash session is a trying write"
  "In instantiation of .static constexpr auto fixy::session::RecordingDoor::send\\(fixy::session::Recorded<Inner>&&, T&&, Transport\\)")
fixy_neg_compile_test(neg_sess_crash_send_transport_result
  ": \\[Crash_Write_Must_Try\\] the write of a crash session is a trying write"
  "use of deleted function .void fixy::session::CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>::send\\(typename P::message_type, Transport\\)")
fixy_neg_compile_test(neg_sess_crash_recorded_send_transport_result
  ": \\[Transport_Shape\\] a write either tries, as bool\\(Resource&, T&\\)"
  "use of deleted function .void fixy::session::Recorded<Inner>::send\\(T, Transport\\)")
fixy_neg_compile_test(neg_sess_crash_delegation_undefined_template
  "Payload_Refused.: the payload cannot travel on a session channel, because it reaches a class or a template that is only declared"
  "In instantiation of .struct fixy::session::detail::payload_readable_gate<.anonymous.::Names<.anonymous.::OpaqueChannel<int> > >.")
fixy_neg_compile_test(neg_sess_crash_delegation_declared_class
  "fixy::session::diagnostic \\[Payload_Delegation_Unreadable\\]: the payload reaches .anonymous.::OpaqueChannel,"
  "In instantiation of .struct fixy::session::detail::delegation_readable_gate<.anonymous.::Frame>.")
fixy_neg_compile_test(neg_sess_crash_delegation_specialized_after_query
  "specialization of ..anonymous.::Frame<int>. after instantiation"
  "redefinition of .struct .anonymous.::Frame<int>.")

# ── Checkpoint and rollback ───────────────────────────────────────────
#
# Checkpoint.h checks a pair of protocols against the compliance of
# Mezzina, Tiezzi and Yoshida (LMCS 2025), with each primitive as a
# coordinated label.  The test runs their Video-on-Demand example.
fixy_test(test_session_rollback)

# HS14 for mint_checkpoint_session and for the one-endpoint refusal.
# Each fixture is one way for one side to choose alone, or one rule of
# the compliance check.
fixy_neg_compile_test(neg_sess_checkpoint_single_endpoint
  "no matching function for call to .mint_session_handle"
  "is_well_formed_v<Proto>. \\[with Proto = fixy::session::Select<fixy::session::Commit<")

fixy_neg_compile_test(neg_sess_checkpoint_labels_disagree
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::LabelsDisagree")

fixy_neg_compile_test(neg_sess_checkpoint_both_choose
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::Stuck")

fixy_neg_compile_test(neg_sess_checkpoint_local_roll
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::NotCheckpointShaped \\[with Proto = fixy::session::Send<int, fixy::session::Roll>")

fixy_neg_compile_test(neg_sess_checkpoint_roll_to_imposed
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::RollToImposedCheckpoint")

fixy_neg_compile_test(neg_sess_checkpoint_permission_payload
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::NotCheckpointShaped \\[with Proto = fixy::session::Select<fixy::session::Commit<fixy::session::Send<fixy::session::Transferable<")

fixy_neg_compile_test(neg_sess_checkpoint_delegation_payload
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::NotCheckpointShaped \\[with Proto = fixy::session::Select<fixy::session::Commit<fixy::session::Send<fixy::session::DelegatedSession<")
fixy_neg_compile_test(neg_sess_checkpoint_delegation_owned_handle
  "no matching function for call to .mint_checkpoint_session"
  "!= fixy::session::CheckpointVerdict::NotCheckpointShaped \\[with Proto = fixy::session::Select<fixy::session::Commit<fixy::session::Send<std::unique_ptr<fixy::session::SessionHandle<")

# The context of a checkpoint session admits the effect row of each
# payload.
fixy_neg_compile_test(neg_sess_checkpoint_ctx_row_refused
  "no matching function for call to .mint_checkpoint_session<Decide, Follow>"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, .: fixy::session::detail::erased\\(\\^\\^[A-Za-z]+, fixy::session::detail::erasure::checkpoints\\) :.>.")

# The checkpoint door builds each checkpoint handle and does each step.
# Its opening member states the whole gate of the mint, and the class is
# final.  The checkpoint handle befriends the door alone.  A rollback
# rebuilds the plain handle through the rewind of fixy/session/Handle.h,
# and the fixtures of the rewind stand beside the handle factory.
fixy_neg_compile_test(neg_sess_checkpoint_door_states_the_gate
  "no matching function for call to .fixy::session::CheckpointDoor::open<Decide, Decide,"
  "!= fixy::session::CheckpointVerdict::Stuck")
fixy_neg_compile_test(neg_sess_checkpoint_door_derived_look_alike
  "cannot derive from .final. base"
  "CheckpointDoor")
fixy_neg_compile_test(neg_sess_checkpoint_handle_friend_specialized
  "is private within this context"
  "fixy::session::CheckpointHandle<Inner, Head, HeadLoop, Frame>::CheckpointHandle\\(Inner\\)")

# ── The event log and the recorder ────────────────────────────────────
#
# EventLog.h decodes bytes only through a check of every control byte,
# and Recording.h writes each step of a plain, crash-watched or
# checkpoint session.  The test corrupts records one byte at a time.
fixy_test(test_session_recording)

# HS14 for mint_recorded_session: a value that is no handle, a handle
# that already has a recorder, and a log that dies before the recorder
# does.
fixy_neg_compile_test(neg_sess_record_not_a_handle
  "no matching function for call to .mint_recorded_session"
  "required for the satisfaction of .RecordableHandle<H>. \\[with H = int\\]")

fixy_neg_compile_test(neg_sess_record_recorded_handle
  "no matching function for call to .mint_recorded_session"
  "the required type .typename fixy::session::detail::recording::wire_protocol<H>::type. is invalid")

# The recording door builds each recorder and does each step.  Its
# opening member states the whole gate of the mint, and the class is
# final.  The recorder befriends the door alone, so a specialization of
# the recorder reaches no inner handle of another.
fixy_neg_compile_test(neg_sess_record_door_states_the_gate
  "no matching function for call to .fixy::session::RecordingDoor::make<neg_sess_record_door_states_the_gate_types::NotAHandle>"
  "required for the satisfaction of .RecordableHandle<H>. \\[with H = neg_sess_record_door_states_the_gate_types::NotAHandle\\]")
fixy_neg_compile_test(neg_sess_record_door_derived_look_alike
  "cannot derive from .final. base"
  "RecordingDoor")
fixy_neg_compile_test(neg_sess_recorded_friend_specialized
  "is private within this context"
  "fixy::session::Recorded<fixy::session::SessionHandle<fixy::session::Send<int, fixy::session::End>, neg_sess_recorded_friend_specialized_types::Wire")

fixy_neg_compile_test(neg_sess_record_temporary_log
  "no matching function for call to .mint_recorded_session"
  "cannot convert .fixy::session::SessionEventLog\\(fixy::session::SessionTagId\\{0\\}\\). \\(type .fixy::session::SessionEventLog.\\) to type .fixy::session::SessionEventLog&.")

# An event exists only through a factory or the decoder, and a log does
# not copy its step counter.
fixy_neg_compile_test(neg_sess_event_forged
  "is private within this context"
  "fixy::session::SessionEvent::SessionEvent\\(fixy::session::SessionOp")

fixy_neg_compile_test(neg_sess_event_log_copied
  "use of deleted function .fixy::session::SessionEventLog::SessionEventLog\\(const fixy::session::SessionEventLog&\\)"
  "Pinned<T>: stable address")

# No byte route builds an event: the event is not trivially copyable and
# not an implicit-lifetime type, so the decoder is the one door.
fixy_neg_compile_test(neg_sess_event_forged_by_bit_cast
  "no matching function for call to .bit_cast<fixy::session::SessionEvent>"
  "is_trivially_copyable_v<_To>")
fixy_neg_compile_test(neg_sess_event_forged_by_start_lifetime_as
  "static assertion failed"
  "fixy::session::SessionEvent. is not an implicit-lifetime type")

# ── Crash and checkpoint attacks ──────────────────────────────────────

# The crash reporter is the one author of a cell's report.  No byte route
# builds one, a const cell mints none, and it does not copy.
fixy_neg_compile_test(neg_sess_crash_reporter_by_bit_cast
  "no matching function for call to .bit_cast<fixy::session::CrashReporter>"
  "is_trivially_copyable_v<_To>")
fixy_neg_compile_test(neg_sess_crash_reporter_by_start_lifetime_as
  "static assertion failed"
  "fixy::session::CrashReporter. is not an implicit-lifetime type")
fixy_neg_compile_test(neg_sess_crash_reporter_from_const_cell
  "use of deleted function .void fixy::session::mint_crash_reporter"
  "\\[Crash_Reporter_From_Const_Cell\\]")
fixy_neg_compile_test(neg_sess_crash_reporter_copied
  "use of deleted function .fixy::session::CrashReporter::CrashReporter\\(const fixy::session::CrashReporter&\\)"
  "a cell has one reporter, so a report has one author")
# The count of a crash report comes from the session of the endpoint.  A
# report takes no count, and the one writer of a cell is move-only and
# comes only from a cell that is not const.
fixy_neg_compile_test(neg_sess_crash_writer_from_const_cell
  "use of deleted function .void fixy::session::mint_crash_writer"
  "\\[Crash_Writer_From_Const_Cell\\]")
fixy_neg_compile_test(neg_sess_crash_writer_copied
  "use of deleted function .fixy::session::CrashWriter::CrashWriter\\(const fixy::session::CrashWriter&\\)"
  "one session counts into a cell, so the cell has one writer")
fixy_neg_compile_test(neg_sess_crash_report_with_count
  "no matching function for call to .fixy::session::CrashReporter::report"
  "report\\(fixy::session::CrashCause, fixy::session::MessageCount\\)")
#
# The priority order of fixy/session/Watch.h: the two ends of a channel
# state one priority, and a Resource states it with its own type.
fixy_neg_compile_test(neg_sess_channel_ends_disagree_on_priority
  "no matching function for call to .mint_test_channel"
  "session_priority_v<ResourceA>\\) == \\(session_priority_v<ResourceB>\\)")
fixy_neg_compile_test(neg_sess_session_priority_untyped
  "fixy::session::diagnostic \\[Session_Priority_Type\\]"
  "declares session_priority with a type other than fixy::session::watch::priority")
fixy_neg_compile_test(neg_sess_send_write_waits_in_secret
  ": \\[Transport_Shape\\] a write either tries, as bool\\(Resource&, T&\\)"
  "use of deleted function .void fixy::session::SessionHandle<fixy::session::Send<T, R>, Resource, LoopCtx, Policy, PS>::send\\(T, Transport\\)")
fixy_neg_compile_test(neg_sess_recv_read_waits_in_secret
  ": \\[Transport_Shape\\] a read either polls, as std::optional<T>\\(Resource&\\)"
  "use of deleted function .void fixy::session::SessionHandle<fixy::session::Recv<T, R>, Resource, LoopCtx, Policy, PS>::recv\\(Transport\\)")
fixy_neg_compile_test(neg_sess_select_write_waits_in_secret
  ": \\[Transport_Shape\\] a write of a word either tries, as bool\\(Resource&, std::size_t\\)"
  "use of deleted function .void fixy::session::SessionHandle<fixy::session::Select<Branches ...>, Resource, LoopCtx, Policy, PS>::select\\(Transport\\)")
fixy_neg_compile_test(neg_sess_branch_read_waits_in_secret
  ": \\[Transport_Shape\\] a read of a word either polls, as std::optional<std::size_t>\\(Resource&\\)"
  "use of deleted function .void fixy::session::SessionHandle<fixy::session::Offer<Branches ...>, Resource, LoopCtx, Policy, PS>::branch\\(Transport, Handler\\)")
#
# Legal code that tries to defeat the crash transport, the checkpoint
# session and the event log.  Each runtime attack runs in a child under
# a watchdog, and the attacks that succeed are pinned on a ledger that
# can only shrink.
fixy_test(test_session_crash_attack)

# ── Global types, projection and liveness ─────────────────────────────
#
# Global.h checks well-formedness and balanced+, Projection.h projects
# onto each role and checks association, and Liveness.h joins the chain
# (Pischke, Masters and Yoshida, arXiv 2505.17676 version 4).  The first
# test holds the paper's examples.  The second runs an explorer of the
# asynchronous context semantics against the paper's non-live contexts,
# the accepted types, the contexts each refusal prevents, generated
# global types and a grid of merges, and it pins the one attack that
# succeeds on purpose.
fixy_test(test_session_global_types)
fixy_test(test_session_global_attack)

# ── Transition systems and operational correspondence ─────────────────
#
# Semantics.h gives the transition systems of global types and of
# configurations under crash-stop failures (Barwell, Hou, Yoshida and
# Zhou, LMCS 2025, Definitions 4.12 and 4.18).  The test checks each rule,
# and it checks operational correspondence on a corpus of global types.
fixy_test(test_session_semantics)

# Two fixtures for each input predicate of the public aliases.  A state
# holds a well-formed global type and a set of crashed roles.
fixy_neg_compile_test(neg_sess_semantics_state_self_communication
  "template constraint failure for .template<class S, class Reliable>  requires \\(is_global_state_v<S>\\)"
  "the expression .is_global_state_v<S> .with S = fixy::session::global::State<fixy::session::global::Roles<>, fixy::session::global::Comm<neg_sess_semantics_state_self_communication_types::P, neg_sess_semantics_state_self_communication_types::P,")
fixy_neg_compile_test(neg_sess_semantics_state_crashed_twice
  "template constraint failure for .template<class S, class A, class Reliable>  requires \\(is_global_state_v<S>\\)"
  "the expression .is_global_state_v<S> .with S = fixy::session::global::State<fixy::session::global::Roles<neg_sess_semantics_state_crashed_twice_types::P, neg_sess_semantics_state_crashed_twice_types::P>,")

# A configuration holds local types that the rules read.
fixy_neg_compile_test(neg_sess_semantics_config_bare_payload
  "template constraint failure for .template<class Ctx, class Reliable>  requires \\(is_configuration_v<Ctx>\\)"
  "the expression .is_configuration_v<Ctx> .with Ctx = fixy::session::TypingContext<fixy::session::RoleState<neg_sess_semantics_config_bare_payload_types::P, fixy::session::OutQueue<>, fixy::session::Recv<int, fixy::session::End> > >.. evaluated to .false.")
fixy_neg_compile_test(neg_sess_semantics_config_unguarded_loop
  "template constraint failure for .template<class Ctx, class A, class Reliable>  requires \\(is_configuration_v<Ctx>\\)"
  "the expression .is_configuration_v<Ctx> .with Ctx = fixy::session::TypingContext<fixy::session::RoleState<neg_sess_semantics_config_unguarded_loop_types::P, fixy::session::OutQueue<>, fixy::session::Loop<fixy::session::Continue> > >.. evaluated to .false.")

# A step takes a label of Definition 4.9.
fixy_neg_compile_test(neg_sess_semantics_global_step_not_a_label
  "template constraint failure for .template<class S, class A, class Reliable>  requires"
  "the expression .is_transition_label_v<A> .with A = int.. evaluated to .false.")
fixy_neg_compile_test(neg_sess_semantics_config_step_not_a_label
  "template constraint failure for .template<class Ctx, class A, class Reliable>  requires"
  "the expression .is_transition_label_v<A> .with A = fixy::session::global::Actions<>.. evaluated to .false.")

# The reliability assumption is a ReliableSet or EveryRoleReliable.
fixy_neg_compile_test(neg_sess_semantics_global_reliability_malformed
  "template constraint failure for .template<class S, class Reliable>  requires"
  "the expression .is_reliability_v<Reliable> .with Reliable = fixy::session::global::Roles<neg_sess_semantics_global_reliability_malformed_types::P>.. evaluated to .false.")
fixy_neg_compile_test(neg_sess_semantics_config_reliability_malformed
  "template constraint failure for .template<class Ctx, class Reliable>  requires"
  "the expression .is_reliability_v<Reliable> .with Reliable = neg_sess_semantics_config_reliability_malformed_types::P.. evaluated to .false.")

# CrashAssociation.h relates a configuration to a state under crash-stop
# failures (Definition 4.19 of the crash-stop paper).  The test checks
# each clause, and it walks the corpus with crashes to check operational
# correspondence.
fixy_test(test_session_crash_association)

# Two fixtures for each fault of the gate.  Each fixture breaks one
# clause and keeps the clauses before it.
fixy_neg_compile_test(neg_sess_crash_assoc_malformed_context
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Malformed\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = fixy::session::TypingContext<fixy::session::RoleState<neg_sess_crash_assoc_malformed_context_types::P, fixy::session::OutQueue<>, fixy::session::Recv<int, fixy::session::End> > >")
fixy_neg_compile_test(neg_sess_crash_assoc_malformed_reliability
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Malformed\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = fixy::session::TypingContext<>; S = .*; Reliable = fixy::session::global::Roles<neg_sess_crash_assoc_malformed_reliability_types::P>\\]")
fixy_neg_compile_test(neg_sess_crash_assoc_reliable_role_crashed
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Not_Well_Annotated\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*; Reliable = fixy::session::ReliableSet<neg_sess_crash_assoc_reliable_role_crashed_types::P, neg_sess_crash_assoc_reliable_role_crashed_types::Q>\\]")
fixy_neg_compile_test(neg_sess_crash_assoc_annotation_not_crashed
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Not_Well_Annotated\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*; S = fixy::session::global::State<fixy::session::global::Roles<>, fixy::session::global::EnRouteChoice<fixy::session::global::Crashed<")
fixy_neg_compile_test(neg_sess_crash_assoc_not_balanced_plus
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Not_Balanced_Plus\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*; S = fixy::session::global::State<fixy::session::global::Roles<>, fixy::session::global::Comm<")
fixy_neg_compile_test(neg_sess_crash_assoc_loop_starves_a_role
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Not_Balanced_Plus\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = fixy::session::TypingContext<>; S = fixy::session::global::State<fixy::session::global::Roles<>, fixy::session::global::Rec<")
fixy_neg_compile_test(neg_sess_crash_assoc_stopped_but_live
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Crashed_Role_Mismatch\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*fixy::session::Stop.*; S = fixy::session::global::State<fixy::session::global::Roles<>,")
fixy_neg_compile_test(neg_sess_crash_assoc_crashed_but_running
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Crashed_Role_Mismatch\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*; S = fixy::session::global::State<fixy::session::global::Roles<neg_sess_crash_assoc_crashed_but_running_types::P>,")
fixy_neg_compile_test(neg_sess_crash_assoc_drops_crash_branch
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Live_Role_Mismatch\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*fixy::session::Recv<fixy::session::PeerMsg<neg_sess_crash_assoc_drops_crash_branch_types::P, neg_sess_crash_assoc_drops_crash_branch_types::M, int>, fixy::session::End> > >;")
fixy_neg_compile_test(neg_sess_crash_assoc_live_role_missing
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Live_Role_Mismatch\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = fixy::session::TypingContext<fixy::session::RoleState<neg_sess_crash_assoc_live_role_missing_types::P, fixy::session::OutQueue<>, [^;]*> >;")
fixy_neg_compile_test(neg_sess_crash_assoc_stray_role
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Unfinished_Role\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*fixy::session::RoleState<neg_sess_crash_assoc_stray_role_types::R,")
fixy_neg_compile_test(neg_sess_crash_assoc_role_after_end
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Unfinished_Role\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*; S = fixy::session::global::State<fixy::session::global::Roles<>, fixy::session::global::End>;")
fixy_neg_compile_test(neg_sess_crash_assoc_early_message
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Queue_Mismatch\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = fixy::session::TypingContext<fixy::session::RoleState<neg_sess_crash_assoc_early_message_types::P, fixy::session::OutQueue<fixy::session::Queued<")
fixy_neg_compile_test(neg_sess_crash_assoc_message_to_crashed
  "static assertion failed"
  "fixy::session::diagnostic \\[Crash_Association_Queue_Mismatch\\]"
  "ensure_crash_associated\\(\\) \\[with Ctx = .*fixy::session::Stop.*; S = fixy::session::global::State<fixy::session::global::Roles<neg_sess_crash_assoc_message_to_crashed_types::Q>,")

# Two fixtures for each gate.  Each refused type satisfies each earlier
# clause of its gate, so only the named clause can refuse it.
fixy_neg_compile_test(neg_sess_global_self_communication
  "static assertion failed"
  "fixy::session::diagnostic \\[Global_Self_Communication\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_duplicate_label
  "static assertion failed"
  "fixy::session::diagnostic \\[Global_Duplicate_Label\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_unbalanced_loop
  "static assertion failed"
  "fixy::session::diagnostic \\[Global_Not_Balanced\\]"
  "ensure_balanced_plus\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_en_route_under_loop
  "static assertion failed"
  "fixy::session::diagnostic \\[Global_EnRoute_Count_Undefined\\]"
  "ensure_balanced_plus\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_project_unbound_variable
  "template constraint failure for .template<class G, class R>"
  "is_global_well_formed_v<G>"
  "fixy::session::project_t")

fixy_neg_compile_test(neg_sess_project_not_a_global_type
  "template constraint failure for .template<class G, class R>"
  "is_global_well_formed_v<G>"
  "fixy::session::project_t")

fixy_neg_compile_test(neg_sess_project_merge_label_mismatch
  "static assertion failed"
  "fixy::session::diagnostic \\[Projection_Merge_Label_Set_Mismatch\\]"
  "ensure_projectable\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_project_merge_shape_mismatch
  "static assertion failed"
  "fixy::session::diagnostic \\[Projection_Merge_Shape_Mismatch\\]"
  "ensure_projectable\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_project_queue_differs
  "static assertion failed"
  "fixy::session::diagnostic \\[Projection_Queue_Differs_Across_Branches\\]"
  "ensure_projectable\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_assoc_missing_role
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Missing_Role\\]"
  "ensure_associated\\(\\) \\[with Ctx")

fixy_neg_compile_test(neg_sess_assoc_local_mismatch
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Local_Mismatch\\]"
  "ensure_associated\\(\\) \\[with Ctx")

fixy_neg_compile_test(neg_sess_assoc_not_balanced_plus
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Global_Not_Balanced_Plus\\]"
  "ensure_associated\\(\\) \\[with Ctx")

fixy_neg_compile_test(neg_sess_assoc_select_widened
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Local_Mismatch\\]"
  "PeerMsg<neg_sess_assoc_select_widened_types::Carol, neg_sess_assoc_select_widened_types::Stop, int>")

fixy_neg_compile_test(neg_sess_assoc_offer_narrowed
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Local_Mismatch\\]"
  "RoleState<neg_sess_assoc_offer_narrowed_types::Carol, OutQueue<>, Offer<Sender<neg_sess_assoc_offer_narrowed_types::Bob>, Recv<PeerMsg<neg_sess_assoc_offer_narrowed_types::Bob, neg_sess_assoc_offer_narrowed_types::Left, int>, End> > >")

fixy_neg_compile_test(neg_sess_assoc_untrusted_payload
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Local_Mismatch\\]"
  "Tagged<int, fixy::tags::source::FromUser>")

fixy_neg_compile_test(neg_sess_live_unbalanced
  "static assertion failed"
  "fixy::session::diagnostic \\[Global_Not_Balanced\\]"
  "Liveness\\.h:[0-9]+:[0-9]+: +required from here")

fixy_neg_compile_test(neg_sess_live_unprojectable
  "static assertion failed"
  "fixy::session::diagnostic \\[Projection_Merge_Shape_Mismatch\\]"
  "Liveness\\.h:[0-9]+:[0-9]+: +required from here")

fixy_neg_compile_test(neg_sess_context_live_extra_role
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Extra_Role_Not_Terminated\\]"
  "Liveness\\.h:[0-9]+:[0-9]+: +required from here")

fixy_neg_compile_test(neg_sess_context_live_queue_mismatch
  "static assertion failed"
  "fixy::session::diagnostic \\[Association_Queue_Mismatch\\]"
  "Liveness\\.h:[0-9]+:[0-9]+: +required from here")

fixy_neg_compile_test(neg_sess_strip_peers_two_peers
  "template constraint failure for .template<class Local>"
  "fixy::session::strip_peers_t"
  "local_peers_t<Local>::size\\) <= 1")

# The network model of a carrier (fixy/session/Network.h).  A mailbox
# refuses a receiver with two senders.  A bag refuses a choice, and two
# messages to one receiver with one label that no receive orders: two
# payload types, two senders, or a stream.  A carrier that states no
# network, or states it with another type, is refused.  A session mint
# refuses a local type that names two peers.
fixy_test(test_session_network)
fixy_neg_compile_test(neg_sess_network_mailbox_two_senders
  "fixy::session::diagnostic \\[Network_Mailbox_Receiver_Has_Two_Senders\\]"
  "the senders of one receiver share one FIFO queue")
fixy_neg_compile_test(neg_sess_network_mailbox_head_blocks
  "fixy::session::diagnostic \\[Network_Mailbox_Receiver_Has_Two_Senders\\]"
  "can reach the head while the receiver waits for the other sender")
fixy_neg_compile_test(neg_sess_network_mailbox_loop
  "fixy::session::diagnostic \\[Network_Mailbox_Receiver_Has_Two_Senders\\]"
  "Give each receiver one sender, or bind to a per-pair FIFO carrier")
fixy_neg_compile_test(neg_sess_network_bag_choice
  "fixy::session::diagnostic \\[Network_Bag_Choice\\]"
  "a later label of a choice can overtake an earlier one")
fixy_neg_compile_test(neg_sess_network_bag_repeated_label_two_payloads
  "fixy::session::diagnostic \\[Network_Bag_Repeated_Word\\]"
  "types::X\\(int\\) from [a-z_:]+::R0 to [a-z_:]+::R1 and the message [a-z_:]+::X\\(double\\) from [a-z_:]+::R0 to [a-z_:]+::R1 go to one receiver with one word")
fixy_neg_compile_test(neg_sess_network_bag_repeated_label_two_senders
  "fixy::session::diagnostic \\[Network_Bag_Repeated_Word\\]"
  "types::X\\(int\\) from [a-z_:]+::R0 to [a-z_:]+::R2 and the message [a-z_:]+::X\\(int\\) from [a-z_:]+::R1 to [a-z_:]+::R2 go to one receiver with one word")
fixy_neg_compile_test(neg_sess_network_bag_repeated_label_stream
  "fixy::session::diagnostic \\[Network_Bag_Repeated_Word\\]"
  "types::X\\(int\\) from [a-z_:]+::R0 to [a-z_:]+::R1 and the message [a-z_:]+::X\\(int\\) from [a-z_:]+::R0 to [a-z_:]+::R1 go to one receiver with one word")
fixy_neg_compile_test(neg_sess_network_carrier_undeclared
  "fixy::session::diagnostic \\[Carrier_Network_Undeclared\\]"
  "the carrier states no network model")
fixy_neg_compile_test(neg_sess_network_carrier_wrong_type
  "fixy::session::diagnostic \\[Session_Network_Type\\]"
  "declares session_network with a type other than fixy::session::Network")
fixy_neg_compile_test(neg_sess_network_crash_stop_on_mailbox
  "fixy::session::diagnostic \\[Network_Crash_Stop_Needs_Per_Pair_Fifo\\]"
  "a crash-stop protocol binds only to a per-pair FIFO carrier")
fixy_neg_compile_test(neg_sess_mint_local_names_two_peers
  "no matching function for call to .mint_session_handle"
  "satisfaction of .NamesAtMostOnePeer<Proto>.")
fixy_neg_compile_test(neg_sess_with_session_local_names_two_peers
  "no matching function for call to .with_session"
  "satisfaction of .NamesAtMostOnePeer<Proto>.")

# A choice that puts no label on the wire needs a carrier that states the
# Local network, and a Local carrier refuses a protocol with a peer.  The
# crash transport enters a crash branch through recover, which refuses a
# label branch and a held permission.
fixy_neg_compile_test(neg_sess_select_no_label_on_a_channel
  "\\[Local_Choice_Needs_A_Local_Carrier\\] select<I>\\(no_label\\) puts no word on the wire"
  "use of deleted function .void fixy::session::SessionHandle<fixy::session::Select<Branches ...>, Resource, LoopCtx, Policy, PS>::select\\(Transport\\) && \\[with long unsigned int I = 1. Transport = fixy::session::no_label_t")
fixy_neg_compile_test(neg_sess_network_local_two_roles
  "fixy::session::diagnostic \\[Network_Local_Has_A_Peer\\]"
  "the carrier states the Local network, so it has no peer")
fixy_neg_compile_test(neg_sess_recover_label_branch
  "no matching function for call to .fixy::session::HandleFactory::recover<0>"
  "I = 0. LoopCtx = void. PS = foundation::permissions::PermSet<>.. evaluated to .false.")
fixy_neg_compile_test(neg_sess_recover_permission_held
  "no matching function for call to .fixy::session::HandleFactory::recover<1>"
  "I = 1. LoopCtx = void. PS = foundation::permissions::PermSet<neg_sess_recover_permission_held_types::Region>.. evaluated to .false.")

fixy_neg_compile_test(neg_sess_strip_peers_not_a_local_type
  "invalid use of incomplete type .struct fixy::session::detail::proj::peer_walk<"
  "fixy::session::strip_peers_t")

# ── Crash-stop global types ───────────────────────────────────────────
#
# The crash label, the crash annotation, role removal, crash-stop
# projection and the liveness predicate of Theorem 4.31, on the examples
# of the crash-stop paper.  Each gate has fixtures for the faults it
# names.
fixy_test(test_session_global_crash)

fixy_neg_compile_test(neg_sess_global_crash_only_choice
  "fixy::session::diagnostic \\[Global_Crash_Only_Choice\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_crash_label_payload
  "fixy::session::diagnostic \\[Global_Crash_Label_Payload\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_crashed_sender_of_comm
  "fixy::session::diagnostic \\[Global_Misplaced_Crash_Annotation\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_live_role_sends_crash_label
  "fixy::session::diagnostic \\[Global_Misplaced_Crash_Annotation\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_unknown_chosen_label
  "fixy::session::diagnostic \\[Global_Unknown_Chosen_Label\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_global_unknown_chosen_label_crashed_sender
  "fixy::session::diagnostic \\[Global_Unknown_Chosen_Label\\]"
  "ensure_global_well_formed\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_projection_missing_crash_branch
  "fixy::session::diagnostic \\[Projection_Missing_Crash_Branch\\]"
  "expansion of .fixy::session::ensure_crash_projectable<")

fixy_neg_compile_test(neg_sess_projection_crash_branch_hidden_in_loop
  "fixy::session::diagnostic \\[Projection_Missing_Crash_Branch\\]"
  "expansion of .fixy::session::ensure_crash_live_by_construction<"
  "expansion of .fixy::session::ensure_crash_projectable<")

fixy_neg_compile_test(neg_sess_projection_crash_branch_from_reliable
  "fixy::session::diagnostic \\[Projection_Crash_Branch_From_Reliable_Sender\\]"
  "expansion of .fixy::session::ensure_crash_projectable<")

fixy_neg_compile_test(neg_sess_projection_crash_branch_in_crash_free_projection
  "fixy::session::diagnostic \\[Projection_Crash_Branch_From_Reliable_Sender\\]"
  "In instantiation of .consteval void fixy::session::ensure_projectable\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_projection_onto_crashed_role
  "fixy::session::diagnostic \\[Projection_Onto_Crashed_Role\\]"
  "expansion of .fixy::session::ensure_crash_projectable<")

fixy_neg_compile_test(neg_sess_projection_en_route_unreliable
  "fixy::session::diagnostic \\[Projection_EnRoute_From_Unreliable_Sender\\]"
  "expansion of .fixy::session::ensure_crash_projectable<")

fixy_neg_compile_test(neg_sess_crash_live_runtime_construct
  "fixy::session::diagnostic \\[Crash_Liveness_Runtime_Construct\\]"
  "ensure_crash_live_by_construction\\(\\) \\[with G")

fixy_neg_compile_test(neg_sess_crash_live_reliable_set_malformed
  "fixy::session::diagnostic \\[Projection_Reliable_Set_Malformed\\]"
  "expansion of .fixy::session::ensure_crash_live_by_construction<")

# ── Every gate predicate is armed ─────────────────────────────────────
#
# The walk in test_armed_roster.cpp reads every predicate the two layers
# declare, so the translation unit has to see every header they ship.
# The list is the glob, not a hand list: a header added to either layer
# joins the walk at the next configure.  A predicate with no armed cell
# and no ledger entry fails the build of that one test.
file(GLOB_RECURSE _armed_roster_headers CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/include/foundation/*.h
  ${CMAKE_SOURCE_DIR}/include/fixy/*.h)
list(SORT _armed_roster_headers)
set(_armed_roster_include_lines "")
foreach(_armed_roster_header IN LISTS _armed_roster_headers)
  string(APPEND _armed_roster_include_lines "#include <${_armed_roster_header}>\n")
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/armed_roster/every_header.h
  CONTENT "#pragma once\n${_armed_roster_include_lines}")
fixy_test(test_armed_roster)
target_include_directories(test_armed_roster PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/armed_roster)

# No proof type is built without a constructor.  std::bit_cast and
# std::start_lifetime_as build an object with no constructor call, so a
# trivially copyable or implicit-lifetime proof type is forgeable.  The
# walk reads the same generated header as the armed roster.
fixy_test(test_forgeable_proofs)
target_include_directories(test_forgeable_proofs PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/armed_roster)

# Attacks on ownership transfer that use the discipline correctly.  The
# threaded half runs under a watchdog, and it is the half that means
# anything under the tsan preset.
fixy_test(test_session_ownership_attacks)

# A user specialization of each view, gate and cache cell of the payload
# walk changes no verdict of the session layer.
fixy_test(test_session_payload_seal)

# A hold used after a move, and a hold dropped with a loan open, each
# abort with a named diagnostic.  Each attack runs in a child process,
# and each test passes only on its diagnostic.
fixy_test(test_session_hold_consumed_aborts)
fixy_test(test_session_hold_open_loan_aborts)

# The payload walk refuses a token laundered through a pair, behind a
# pointer or inside a std::optional, and a payload behind type erasure.
fixy_neg_compile_test(neg_session_payload_token_behind_pointer
  "Payload_Refused.: the payload cannot travel on a session channel"
  "reaches a permission token through a pointer or a reference")
fixy_neg_compile_test(neg_session_payload_token_in_optional
  "Payload_Refused.: the payload cannot travel on a session channel"
  "holds a permission token in a union, a std::optional or a std::variant")
fixy_neg_compile_test(neg_session_payload_type_erased
  "Payload_Refused.: the payload cannot travel on a session channel"
  "holds a type-erasure family")
fixy_neg_compile_test(neg_session_payload_token_in_boxed_pointee
  "Payload_Refused.: the payload cannot travel on a session channel, because it reaches a permission token through a pointer or a reference"
  "payload_perm_delta<std::unique_ptr<.anonymous.::Box<int> > >")
fixy_neg_compile_test(neg_session_payload_declared_template_behind_pointer
  "Payload_Refused.: the payload cannot travel on a session channel, because it reaches a class or a template that is only declared"
  "In instantiation of .struct fixy::session::detail::payload_readable_gate<.anonymous.::OpaqueFrame<int>\\*>.")

# An endpoint travels only as a DelegatedSession held by value.  A bare
# endpoint and a hand-off behind a pointer are refused in every session,
# and a plain mint refuses a protocol that carries a bare handle.
fixy_neg_compile_test(neg_sess_payload_bare_endpoint
  "Payload_Refused.: the payload cannot travel on a session channel, because it holds, points at or names a session endpoint outside DelegatedSession"
  "payload_perm_delta<neg_sess_payload_bare_endpoint_types::Envelope>")
fixy_neg_compile_test(neg_sess_payload_bare_endpoint_mint
  "no matching function for call to .mint_session_handle<neg_sess_payload_bare_endpoint_mint_types::Carrier"
  "required for the satisfaction of .PermissionFlowCloses<Proto, foundation::permissions::PermSet<> >. .with Proto = fixy::session::Send<fixy::session::SessionHandle<fixy::session::Send<int, fixy::session::Continue>")
# Each verdict of the payload walk is a concept, so a user specialization
# of one no longer compiles.  Each of these specializations compiled while
# the verdict was a variable template, and it flipped a send, a receive,
# a close, a delegation or a classification.
fixy_neg_compile_test(neg_sess_seal_is_permission_classified
  "template-id .is_permission_classified_v<neg_sess_seal_is_permission_classified_types::Evil>. used as a declarator"
  "too many template headers for .fixy::session::is_permission_classified_v.")
fixy_neg_compile_test(neg_sess_seal_is_plain_payload
  "template-id .is_plain_payload_v<fixy::session::Transferable<int, neg_sess_seal_is_plain_payload_types::Region> >. used as a declarator"
  "too many template headers for .fixy::session::is_plain_payload_v.")
fixy_neg_compile_test(neg_sess_seal_payload_conveys_delegation
  "template-id .payload_conveys_delegation_v<fixy::session::DelegatedSession<fixy::session::End, neg_sess_seal_payload_conveys_delegation_types::Wire"
  "too many template headers for .fixy::session::payload_conveys_delegation_v.")
fixy_neg_compile_test(neg_sess_seal_delegates_to_own_peer
  "template-id .delegates_to_own_peer_v<fixy::session::Send<fixy::session::PeerMsg<neg_sess_seal_delegates_to_own_peer_types::Bob"
  "too many template headers for .fixy::session::delegates_to_own_peer_v.")
fixy_neg_compile_test(neg_sess_seal_perm_set_has_open_loan
  "template-id .perm_set_has_open_loan_v<foundation::permissions::PermSet<fixy::session::LentOut<neg_sess_seal_perm_set_has_open_loan_types::Region> > >. used as a declarator"
  "too many template headers for .fixy::session::perm_set_has_open_loan_v.")
fixy_neg_compile_test(neg_sess_seal_plain_tag
  "template-id .PlainTag<fixy::session::LentOut<neg_sess_seal_plain_tag_types::Region> >. used as a declarator"
  "too many template headers for .fixy::session::detail::PlainTag.")
fixy_neg_compile_test(neg_sess_seal_handle_admits_send
  "template-id .handle_admits_send_v<foundation::permissions::PermSet<>, fixy::session::Transferable<int, neg_sess_seal_handle_admits_send_types::Region> >. used as a declarator"
  "too many template headers for .fixy::session::detail::handle_admits_send_v.")
fixy_neg_compile_test(neg_sess_seal_handle_admits_recv
  "template-id .handle_admits_recv_v<foundation::permissions::PermSet<neg_sess_seal_handle_admits_recv_types::Region>, fixy::session::Transferable<int, neg_sess_seal_handle_admits_recv_types::Region"
  "too many template headers for .fixy::session::detail::handle_admits_recv_v.")
fixy_neg_compile_test(neg_sess_seal_perm_set_admits_close
  "template-id .perm_set_admits_close_v<foundation::permissions::PermSet<fixy::session::LentOut<neg_sess_seal_perm_set_admits_close_types::Region> > >. used as a declarator"
  "too many template headers for .fixy::session::detail::perm_set_admits_close_v.")
fixy_neg_compile_test(neg_sess_payload_hand_off_behind_pointer
  "Payload_Refused.: the payload cannot travel on a session channel, because it reaches a DelegatedSession through a pointer"
  "The refused type: fixy::session::DelegatedSession<fixy::session::End, neg_sess_payload_hand_off_behind_pointer_types::Wire")

# A token reaches a hold once: never by name, and never twice by tag.
fixy_neg_compile_test(neg_session_hold_lvalue_token
  "use of deleted function .foundation::permissions::Permission<Tag, Brand>::Permission\\(const foundation::permissions::Permission<Tag, Brand>&\\)"
  "Permission<Tag>: linear")
fixy_neg_compile_test(neg_session_hold_duplicate_tag
  "no matching function for call to .mint_permission_hold\\("
  "perm_tags_unique_v<Tags ...> .with Tags = .<unnamed>::Region, <unnamed>::Region.*evaluated to .false")

# A hold and the loan markers befriend only HoldFactory, which the header
# defines and which is not a template.  A specialization of PermHold reads
# neither the tokens of a hold nor the loan of a marker, and a second
# definition of the factory is a redefinition.
fixy_neg_compile_test(neg_sess_hold_friend_specialized
  "is private within this context"
  "fixy::session::PermHold<foundation::permissions::PermSet<neg_sess_hold_friend_specialized_types::Region> >::slots_")
fixy_neg_compile_test(neg_sess_hold_loan_by_specialization
  "is private within this context"
  "fixy::session::Borrowed<int, neg_sess_hold_loan_by_specialization_types::Region>::loan_")
fixy_neg_compile_test(neg_sess_hold_factory_redefined
  "redefinition of .class fixy::session::HoldFactory."
  "previous definition of .class fixy::session::HoldFactory.")

# A read loan carries a read proof of its own region, and a hold lends
# only a region it holds.
fixy_neg_compile_test(neg_session_borrowed_without_proof
  "no matching function for call to .fixy::session::Borrowed<int, .anonymous.::Region>::Borrowed\\("
  "candidate expects 2 arguments, 1 provided")
fixy_neg_compile_test(neg_session_borrowed_proof_of_another_region
  "no matching function for call to .fixy::session::Borrowed<int, .anonymous.::Region>::Borrowed\\("
  "mismatched types ..anonymous.::Region. and ..anonymous.::Other.")
fixy_neg_compile_test(neg_session_hold_lend_without_token
  "no matching function for call to .fixy::session::PermHold<foundation::permissions::PermSet<.anonymous.::Other> >::lend<.anonymous.::Region>"
  "required for the satisfaction of .SendablePayload<fixy::session::Borrowed<T, Tag>")

# A read through a reader or a hold that stays named.  read consumes the
# object for the call and hands it back, so a body cannot end the share
# or spend the hold through a capture while the view lives.
fixy_neg_compile_test(neg_session_shared_reader_read_by_name
  "passing .fixy::session::SharedReader<.anonymous.::Region"
  "in call to .auto fixy::session::SharedReader<Tag, Brand>::read\\(Body&&\\) &&")
fixy_neg_compile_test(neg_session_hold_read_by_name
  "as .this. argument discards qualifiers"
  "in call to .constexpr auto fixy::session::PermHold<foundation::permissions::PermSet<Tags \\.\\.\\.> >::read\\(Body&&\\) &&")

# A read of a region the hold does not borrow.  read is gated by the set.
fixy_neg_compile_test(neg_session_hold_read_without_borrow
  "no matching function for call to .fixy::session::PermHold<foundation::permissions::PermSet<.anonymous.::Region> >::read<.anonymous.::Region>"
  "perm_set_contains_v<foundation::permissions::PermSet<Tags \\.\\.\\.>, fixy::session::BorrowedIn<Tag> >")

# A release with no loan.  The release carries the loan back, and the
# lender gets its token only with that loan.
fixy_neg_compile_test(neg_session_released_without_loan
  "no matching function for call to .fixy::session::Released<int, .anonymous.::Region>::Released\\("
  "candidate expects 2 arguments, 1 provided")

# A classified value travels only in DeclassifyOnSend under an admitted
# policy, and a constant-time value only in CTPayload, which has no ==.
fixy_neg_compile_test(neg_session_payload_secret_bare
  "Payload_Refused.: the payload cannot travel on a session channel"
  "holds a fixy::Secret outside DeclassifyOnSend")
fixy_neg_compile_test(neg_session_payload_secret_in_optional
  "Payload_Refused.: the payload cannot travel on a session channel"
  "holds a fixy::Secret outside DeclassifyOnSend")
fixy_neg_compile_test(neg_session_payload_constant_time_bare
  "Payload_Refused.: the payload cannot travel on a session channel"
  "holds a constant-time value outside CTPayload")
fixy_neg_compile_test(neg_session_ctpayload_equality
  "use of deleted function .bool fixy::session::CTPayload<T>::operator==\\(const fixy::session::CTPayload<T>&\\) const"
  "a CTPayload compares only through fixy::session::eq")
fixy_neg_compile_test(neg_session_declassify_unadmitted_policy
  "template constraint failure for .template<class T, class Policy>  requires  AdmittedDeclassification<Policy> class fixy::session::DeclassifyOnSend"
  "required for the satisfaction of .AdmittedDeclassification<Policy>. .with Policy = <unnamed>::UnadmittedPolicy")

# ── The lock-free rings ───────────────────────────────────────────────
#
# The threaded half of this test is what checks the ordering claim, so
# it is the half that means anything under the tsan preset.
fixy_test(test_concurrent_rings)

# The Chase-Lev deque maps a position to a cell with a mask, and a thief
# reads a cell without a lock.  So a capacity that is not a power of two
# and a value that is not always lock-free are refused.
fixy_neg_compile_test(neg_chase_lev_deque_capacity_not_power_of_two
  "static assertion failed: Capacity must be a power of two"
  "In instantiation of .class fixy::concurrent::ChaseLevDeque<int, 12>.")
fixy_neg_compile_test(neg_chase_lev_deque_value_not_lock_free
  "template constraint failure for .template<class T, long unsigned int Capacity>  requires  DequeValue<T> class fixy::concurrent::ChaseLevDeque."
  "the expression .std::atomic<_Tp>::is_always_lock_free .with T = <unnamed>::WideValue.. evaluated to .false.")

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

# ── The permissioned channels ─────────────────────────────────────────
#
# The split relation for each channel's tag triple is declared in the
# channel's own header, which is why include/fixy/concurrent/ is an
# authoring location in scripts/check-splits-orphan.py.
fixy_test(test_concurrent_channels)
fixy_test(test_channel_moved_from)

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

# ── The channel sessions, the endpoints and the stage bridge ──────────
#
# A channel handle bridges to a session of its role, an endpoint binds the
# handle to a context, and the stage mints consume endpoints by move.
fixy_test(test_endpoint)

# The session of a channel handle admits each effect that its payload
# carries, it owns the handle, and the table names only its two channels.
fixy_neg_compile_test(neg_substrate_session_payload_row_outside_ctx
  "no matching function for call to .mint_substrate_session<substrate_row_fixture::Channel"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>."
  "satisfaction of .CtxFitsSubstrateSessionMint<Substr, Dir, Ctx>.")
fixy_neg_compile_test(neg_substrate_session_lvalue_handle
  "no matching function for call to .mint_substrate_session<substrate_lvalue_fixture::Channel"
  "cannot convert .producer.")
fixy_neg_compile_test(neg_substrate_session_not_bridgeable
  "no matching function for call to .mint_substrate_session<substrate_table_fixture::FakeChannel"
  "incomplete type .struct fixy::concurrent::handle_for<substrate_table_fixture::FakeChannel")

# An endpoint binds a handle to an execution context, and its gate is the
# gate of the session that the endpoint can start.
fixy_neg_compile_test(neg_endpoint_int_as_ctx
  "no matching function for call to .mint_endpoint<endpoint_ctx_fixture::Channel"
  "satisfaction of .IsExecCtx<Ctx>. .with Ctx = int.")
fixy_neg_compile_test(neg_endpoint_payload_row_outside_ctx
  "no matching function for call to .mint_endpoint<endpoint_row_fixture::Channel"
  "satisfaction of .CtxAdmitsProtocolRow<Ctx, Proto>."
  "satisfaction of .CtxFitsEndpointMint<Substr, Dir, Ctx>.")

# The stage mints consume their endpoints by move, in the direction the
# body names, one endpoint for each parameter of the body.
fixy_neg_compile_test(neg_stage_from_endpoints_lvalue_endpoint
  "no matching function for call to .mint_stage_from_endpoints<stage_lvalue_fixture::body>"
  "satisfaction of .IsMovedEndpoint<ConsumerEp>.")
fixy_neg_compile_test(neg_stage_from_endpoints_swapped_direction
  "no matching function for call to .mint_stage_from_endpoints<stage_swap_fixture::body>"
  "satisfaction of .IsConsumerEndpoint<ConsumerEp>.")
fixy_neg_compile_test(neg_mpmc_stage_from_endpoints_arity_mismatch
  "no matching function for call to .mint_mpmc_stage_from_endpoints<mpmc_arity_fixture::fan_in>"
  "satisfaction of .CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints ...>.")
fixy_neg_compile_test(neg_mpmc_stage_from_endpoints_lvalue_endpoint
  "no matching function for call to .mint_mpmc_stage_from_endpoints<mpmc_lvalue_fixture::fan_in>"
  "satisfaction of .CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints ...>.")
fixy_neg_compile_test(neg_swmr_stage_non_writer
  "no matching function for call to .mint_swmr_stage<swmr_writer_fixture::body>"
  "satisfaction of .CtxFitsSwmrStageFromEndpoint<FnPtr, Ctx, ConsumerEp, Writer>.")
fixy_neg_compile_test(neg_swmr_stage_producer_endpoint
  "no matching function for call to .mint_swmr_stage<swmr_direction_fixture::body>"
  "satisfaction of .CtxFitsSwmrStageFromEndpoint<FnPtr, Ctx, ConsumerEp, Writer>.")

# The recording wrapper takes an endpoint and a session event log.
fixy_neg_compile_test(neg_recording_endpoint_non_endpoint
  "no matching function for call to .mint_recording_endpoint"
  "is not derived from .fixy::concurrent::Endpoint<Substr, Dir, Ctx>.")
fixy_neg_compile_test(neg_recording_endpoint_wrong_log_type
  "no matching function for call to .mint_recording_endpoint"
  "cannot convert .not_a_log. .type .int.. to type .fixy::session::SessionEventLog&.")

# ── The snapshot and the single-writer many-reader channel ────────────
#
# A seqlock snapshot publishes one value to many readers, and the channel
# over it gives one linear writer and a pool of reader shares.
fixy_test(test_concurrent_snapshot)
fixy_test(test_concurrent_swmr_session)

# The writer mint takes the writer permission of a channel of the SWMR
# shape, and the reader mint lends from a channel that can change.
fixy_neg_compile_test(neg_swmr_session_writer_wrong_permission_tag
  "invalid initialization of reference of type .foundation::permissions::Permission<swmr_writer_tag_fixture::WriterTag"
  "from expression of type .foundation::permissions::Permission<swmr_writer_tag_fixture::ReaderTag")
# The session names the brand of its writer root, so a writer root minted
# at another site does not become a second writer.
fixy_neg_compile_test(neg_swmr_session_writer_other_brand
  "cannot convert .foundation::permissions::Permission<swmr_writer_brand_fixture::WriterTag"
  "initializing argument 1 of .*SwmrSession<T, WriterTag, ReaderTag, ReaderBrand, WriterBrand>::writer\\(")
fixy_neg_compile_test(neg_swmr_session_writer_non_surface
  "no matching function for call to .mint_swmr_writer<swmr_writer_surface_fixture::NotAChannel>"
  "satisfaction of .SwmrSessionSurface<Swmr>. .with Swmr = swmr_writer_surface_fixture::NotAChannel.")
fixy_neg_compile_test(neg_swmr_session_reader_non_surface
  "no matching function for call to .mint_swmr_reader<int>"
  "satisfaction of .SwmrSessionSurface<Swmr>. .with Swmr = int.")
fixy_neg_compile_test(neg_swmr_session_reader_const_channel
  "no matching function for call to .mint_swmr_reader"
  "satisfaction of .SwmrSessionSurface<Swmr>. .with Swmr = const fixy::concurrent::swmr_session::SwmrSession<int, swmr_reader_const_fixture::WriterTag")

# Each session mint takes the handle of its own role, by move.
fixy_neg_compile_test(neg_swmr_session_writer_session_reader_handle
  "no matching function for call to .mint_writer_runtime_session<swmr_writer_session_role_fixture::Swmr>"
  "satisfaction of .CtxFitsSwmrWriterSession<[A-Za-z]+, [A-Za-z]+, [A-Za-z]+>.")
fixy_neg_compile_test(neg_swmr_session_writer_session_lvalue_handle
  "no matching function for call to .mint_writer_runtime_session<swmr_writer_session_lvalue_fixture::Swmr>"
  "satisfaction of .CtxFitsSwmrWriterSession<[A-Za-z]+, [A-Za-z]+, [A-Za-z]+>.")
fixy_neg_compile_test(neg_swmr_session_reader_session_writer_handle
  "no matching function for call to .mint_reader_runtime_session<swmr_reader_session_role_fixture::Swmr>"
  "satisfaction of .CtxFitsSwmrReaderSession<[A-Za-z]+, [A-Za-z]+, [A-Za-z]+>.")
fixy_neg_compile_test(neg_swmr_session_reader_session_lvalue_handle
  "no matching function for call to .mint_reader_runtime_session<swmr_reader_session_lvalue_fixture::Swmr>"
  "satisfaction of .CtxFitsSwmrReaderSession<[A-Za-z]+, [A-Za-z]+, [A-Za-z]+>.")

# ── The handle poles and the stage shape ──────────────────────────────
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

# ── The payload row is fail-closed ────────────────────────────────────
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
  "static assertion failed: payload_row<T>: T is, or holds, a type this extractor cannot classify"
  "with T = \\{anonymous\\}::UnclassifiedWrapper<int>")
fixy_neg_compile_test(neg_payload_row_unclassified_hides_effect
  "static assertion failed: payload_row<T>: T is, or holds, a type this extractor cannot classify"
  "with T = \\{anonymous\\}::UnclassifiedWrapper<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>, int> >")
#
# A plain class is read for what it holds, so an unrostered wrapper in a
# member is refused as it is at the root.  The second regex is the
# refused type as the text of the assertion names it, which the
# instantiation note for payload_row<T> does not.
fixy_neg_compile_test(neg_payload_row_member_hides_unclassified
  "static assertion failed: payload_row<T>: T is, or holds, a type this extractor cannot classify"
  "The refused type: \\{anonymous\\}::UnclassifiedWrapper<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>, int> >")

# ── The machine topology ──────────────────────────────────────────────
#
# The sysfs probe Pipeline::run reads for its cache sizes and its
# affinity.  The machine under test is unknown, so every assertion is
# structural: it bounds a value or relates two, and never pins a number.
# The test keeps the file name of the one it is ported from and carries
# a suffix, because test/CMakeLists.txt still registers test_topology
# and a CMake target name is global.
fixy_test(test_topology_fixy test_topology.cpp)

# Topology::probe_tree reads a sysfs tree under another root, and only a
# context that owns the Test capability reaches it.
fixy_neg_compile_test(neg_topology_probe_tree_needs_test_ctx
  "no matching function for call to .fixy::concurrent::Topology::probe_tree\\("
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Test>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg> >\\]")

# ── The stage and the pipeline ────────────────────────────────────────
#
# Stage.h and Pipeline.h carry their type-level claims as static
# assertions, so the two tests below are the parts only a constructed
# run can show: which thread each stage body ran on, and that a
# branching graph runs every stage exactly once.  Both keep the file
# names of the tests they are ported from and carry a suffix, because
# test/CMakeLists.txt still registers the plain names and a CMake
# target name is global.
fixy_test(test_pipeline_dispatch_fixy test_pipeline_dispatch.cpp)
fixy_test(test_pipeline_dag_fixy test_pipeline_dag.cpp)

# The nine refusals of mint_stage and of the stage it returns.  Every
# second regex is a string the compiler emits and the fixture source
# does not: a satisfaction note naming the conjunct, a substitution with
# the anonymous-namespace prefix the source never writes, or a
# constructor rendered with parentheses where the source spells braces.
#
# The first three separate the shape gate from the context gate: a body
# with no producer slot fails where the factory's own signature names
# that slot, so the refusal is an arity bound rather than a concept.
fixy_neg_compile_test(neg_stage_mint_body_not_a_stage
  "no matching function for call to .mint_stage<\\{anonymous\\}::consumer_only>"
  "I < foundation::reflect::signature_traits<FnPtr>::arity \\[with I = 1. FnPtr = &consumer_only")
fixy_neg_compile_test(neg_stage_mint_ctx_not_exec_ctx
  "no matching function for call to .mint_stage<\\{anonymous\\}::pass_through>\\(int&"
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = int\\]"
  "is_exec_ctx_v<T> \\[with T = int\\]. evaluated to .false.")

# The row gate, one fixture per direction and one for both at once.
# The gate is a conjunction and the compiler reports the first conjunct
# that fails, so the both-rows fixture floors on the input conjunct like
# the input fixture; what makes it a distinct cell is the payload pair,
# which its second regex names.  The capability fixture is the fourth
# case because a capability carries the row of the effect it grants
# rather than a row written into a computation.
fixy_neg_compile_test(neg_stage_mint_input_row_not_admitted
  "required for the satisfaction of .StageInputRowAdmitted<FnPtr, Ctx>."
  "FakeConsumer<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::IO>, int> >&&, <unnamed>::FakeProducer<int>&&\\)")
# The same row held in a member of a plain struct payload.
fixy_neg_compile_test(neg_stage_mint_input_member_row_not_admitted
  "required for the satisfaction of .StageInputRowAdmitted<FnPtr, Ctx>."
  "FakeConsumer<<unnamed>::IoPayload>&&, <unnamed>::FakeProducer<int>&&\\)")
fixy_neg_compile_test(neg_stage_mint_output_row_not_admitted
  "required for the satisfaction of .StageOutputRowAdmitted<FnPtr, Ctx>."
  "Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init")
fixy_neg_compile_test(neg_stage_mint_both_rows_not_admitted
  "required for the satisfaction of .StageInputRowAdmitted<FnPtr, Ctx>."
  "FakeProducer<foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>, int> >&&\\). Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg")
fixy_neg_compile_test(neg_stage_mint_capability_payload_not_admitted
  "required for the satisfaction of .StageInputRowAdmitted<FnPtr, Ctx>."
  "FakeConsumer<foundation::effects::Capability<foundation::effects::Effect::Alloc, foundation::effects::Bg> >&&")

# The parameter binding, which is a gate the concept does not make: both
# handles satisfy the shape predicates and only the element type is
# wrong, so the refusal is the argument failing to bind rather than a
# constraint answering false.
fixy_neg_compile_test(neg_stage_mint_handle_type_mismatch
  "no matching function for call to .mint_stage<\\{anonymous\\}::int_to_int>"
  "cannot convert .std::move<\\{anonymous\\}::FakeConsumer<float>&>"
  "to type .std::remove_reference_t<\\{anonymous\\}::FakeConsumer<int>&&>&&.")

# The two disciplines the stage carries after it is minted.
fixy_neg_compile_test(neg_stage_copied
  "use of deleted function"
  "fixy::concurrent::Stage<FnPtr, Ctx>::Stage\\(const fixy::concurrent::Stage<FnPtr, Ctx>&\\)"
  "Stage holds linear Permission tokens via the consumer/producer handles")
fixy_neg_compile_test(neg_stage_run_on_an_lvalue
  "as .this. argument discards qualifiers"
  "in call to .void fixy::concurrent::Stage<FnPtr, Ctx>::run\\(\\) &&")

# The fourteen refusals of the pipeline, its graph and its two mints.
# The chain gate has three cells that fail through the same concept and
# are separated by what the fold answered false about: an element that
# is not a stage at all, two stages whose payloads do not meet, and an
# empty pack.
# `pipeline_chain` is reported as `pipeline_chain<Stages ...>` rather than in
# the `remove_cvref<_ArgTypes>` spelling, because mint_pipeline's clause now
# names CtxFitsPipeline and the unwrapping happens at that concept's call
# site.  The second regex of each pair is the discriminating one and did not
# move: IsStage for the non-stage, pipeline_chain_check for the broken chain.
fixy_neg_compile_test(neg_pipeline_mint_not_a_stage
  "required for the satisfaction of .pipeline_chain<Stages \\.\\.\\.>."
  "the expression .\\(IsStage<Stages> && \\.\\.\\.\\).*, int\\}\\].* evaluated to .false.")
fixy_neg_compile_test(neg_pipeline_mint_chain_break
  "required for the satisfaction of .pipeline_chain<Stages \\.\\.\\.>."
  "pipeline_chain_check<std::tuple<typename std::remove_cvref<_Tps>::type \\.\\.\\.> >\\)\\(\\{\\}\\)"
  "&float_to_double\\(<unnamed>::FakeConsumer<float>&&")
# One payload on two channels.  The payloads of the two stages agree, so
# only the channel that each handle names can refuse the chain.  The two
# last regexes pin the producer on channel A and the consumer on channel B.
fixy_neg_compile_test(neg_pipeline_mint_chain_across_channels
  "required for the satisfaction of .pipeline_chain<Stages \\.\\.\\.>."
  "&writes_a\\(fixy::concurrent::PermissionedSpscChannel<int, 8, <unnamed>::InTag>::ConsumerHandle&&, fixy::concurrent::PermissionedSpscChannel<int, 8, <unnamed>::ATag>::ProducerHandle&&\\)"
  "&drains_b\\(fixy::concurrent::PermissionedSpscChannel<int, 8, <unnamed>::BTag>::ConsumerHandle&&")
fixy_neg_compile_test(neg_pipeline_mint_empty_pack
  "no matching function for call to .mint_pipeline\\(const fixy::BgDrainCtx&\\)."
  "sizeof \\.\\.\\. \\(Stages \\.\\.\\.\\) >= 1 \\[with Stages = \\{\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_pipeline_mint_ctx_not_exec_ctx
  "no matching function for call to .mint_pipeline\\(int&"
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = int\\]"
  "is_exec_ctx_v<T> \\[with T = int\\]. evaluated to .false.")

# The authority to start threads.  A pipeline starts one thread per stage
# unless the cache sizes probed at run time say it fits inline, so the
# context that mints it and the context that runs it must own Bg or Init.
# The foreground context owns neither.  The first fixture is the mint, the
# second is run on a pipeline that a background context minted, and the
# third is the graph mint.
fixy_neg_compile_test(neg_pipeline_mint_ctx_starts_no_threads
  "required for the satisfaction of .CtxStartsStageThreads<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "no matching function for call to .mint_pipeline\\(fixy::HotFgCtx&")
fixy_neg_compile_test(neg_pipeline_run_ctx_starts_no_threads
  "required for the satisfaction of .CtxStartsStageThreads<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "::run\\(fixy::HotFgCtx&\\).")
fixy_neg_compile_test(neg_pipeline_dag_mint_ctx_starts_no_threads
  "required for the satisfaction of .CtxStartsStageThreads<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
  "no matching function for call to .mint_pipeline_dag\\(fixy::HotFgCtx&")

# The coordinator's row admission: the payloads chain and every stage fits
# its own context, and what fails is the union against the context that
# starts them.  The refusal is a constraint.  The third regex keeps this
# fixture honest: it pins the atomic constraint that answered false, so
# deleting the row conjunct from CtxFitsPipeline changes the diagnostic and
# reds the fixture.  Matching only "constraints not satisfied" would pass for
# any of the four conjuncts.
fixy_neg_compile_test(neg_pipeline_mint_row_union_not_admitted
  "constraints not satisfied"
  "required for the satisfaction of .CtxFitsPipeline<"
  "row_subset<.*Ctx::row_type>.*evaluated to")

# The two disciplines the pipeline carries, as for the stage above.
fixy_neg_compile_test(neg_pipeline_copied
  "use of deleted function"
  "fixy::concurrent::Pipeline<Stages>::Pipeline\\(const fixy::concurrent::Pipeline<Stages>&\\)"
  "Pipeline holds move-only Stages")
fixy_neg_compile_test(neg_pipeline_run_on_an_lvalue
  "as .this. argument discards qualifiers"
  "in call to .void fixy::concurrent::Pipeline<Stages>::run\\(const RunCtx&\\) &&")

# The inline opt-in, on three axes.  The first is the claim a stage
# makes about itself: opting in without a stated working set is refused
# where the pipeline forms, because the opt-in means the working set
# fits a private cache and a handle that states none cannot support it.
fixy_neg_compile_test(neg_pipeline_inline_opt_in_without_working_set
  "static assertion failed: Pipeline inline opt-in requires both stage handles to expose "
  "fixy::concurrent::stage_per_call_ws_known_v<fixy::concurrent::Stage<\\{anonymous\\}::body")

# The other two are the compile-time claim asked of a stated cache size,
# and they are two axes rather than two values on one: the oversized
# pipeline IS opted in and fails on the size, the tiny one fits in any
# cache and fails on the missing opt-in.  Each floors on its own stage
# body as the compiler renders it, with the working set expanded to a
# literal the fixture source never writes.
fixy_neg_compile_test(neg_pipeline_inline_claim_oversized
  "no matching function for call to .claim_inline<\\{anonymous\\}::HugePipeline>\\(\\)."
  "P::will_run_inline_v<32 \\* <unnamed>::KiB, 1 \\* <unnamed>::MiB>"
  "&heavy\\(<unnamed>::HeavyConsumer<10485760>&&")
fixy_neg_compile_test(neg_pipeline_inline_claim_without_opt_in
  "no matching function for call to .claim_inline<\\{anonymous\\}::TinyPipeline>\\(\\)."
  "P::will_run_inline_v<32 \\* <unnamed>::KiB, 1 \\* <unnamed>::MiB>"
  "&tiny\\(<unnamed>::TinyConsumer<128>&&")

# The graph's well-formedness refusals.  The first two are separated by
# the edge each one declares: an edge running backwards is a cycle
# witness, and a graph of three stages with one edge leaves the third
# with no edge in either direction.
fixy_neg_compile_test(neg_pipeline_dag_cyclic_graph
  "required for the satisfaction of .StageGraphWellFormed<Graph>."
  "fixy::concurrent::EdgePack<fixy::concurrent::StageEdge<1, 0, 0, 0> >")
fixy_neg_compile_test(neg_pipeline_dag_unreachable_stage
  "required for the satisfaction of .StageGraphWellFormed<Graph>."
  "fixy::concurrent::EdgePack<fixy::concurrent::StageEdge<0, 1, 0, 0> >")
# The third refusal: the one edge agrees in payload and joins channel A
# to channel B.  The graph is connected and the edge runs forward, so only
# the channel check can refuse it.
fixy_neg_compile_test(neg_pipeline_dag_edge_across_channels
  "required for the satisfaction of .StageGraphWellFormed<Graph>."
  "StagePack<fixy::concurrent::Stage<&writes_a\\(.*<unnamed>::ATag>::ProducerHandle&&\\)"
  "&drains_b\\(fixy::concurrent::PermissionedSpscChannel<int, 8, <unnamed>::BTag>::ConsumerHandle&&")

# The dag mint's two clauses, in order: the graph is a graph at all, and
# the supplied pack is the graph's own stage pack.  The second fixture's
# graph is well formed, so only the pack clause can be what refuses.
fixy_neg_compile_test(neg_pipeline_dag_mint_not_a_graph
  "required for the satisfaction of .CtxFitsPipelineDagMint<Ctx, Graph, Stages \\.\\.\\.>."
  "required for the satisfaction of .StageGraphWellFormed<Graph>. \\[with Graph = int\\]")
fixy_neg_compile_test(neg_pipeline_dag_mint_stage_pack_mismatch
  "required for the satisfaction of .graph_stage_pack_is<Graph, Stages \\.\\.\\.>."
  "Graph = fixy::concurrent::StageGraph<fixy::concurrent::StagePack<fixy::concurrent::Stage<&pass_through")

# ── The publication handles ───────────────────────────────────────────
#
# The double-publish abort is deliberately not a contract clause — a
# hot-path TU sets the contract semantic to ignore, where a clause
# elides and the second publish quietly no-ops.  Observing an abort
# needs a process that can die, so a passing run prints one
# PublishOnceDoublePublish banner from the child.
fixy_test(test_handle_publish)

# The commit counter: only the publishing stage bumps it, and a reader
# that sees a count sees the writes made before the bump.  The friend
# list is the gate, and the fixture is the witness that it refuses.
fixy_test(test_handle_publish_commit)
fixy_neg_compile_test(neg_handle_publish_commit_bump_outside_auth
  "is private within this context"
  "fixy::handle::PublishCommitCell<Tag, WriteAuth>::bump\\(\\)")

# The one-shot signal: a raise that arrives while the body runs is kept,
# and several consumers never run two bodies for one raise.  A reset
# names its proof of quiescence, and the two fixtures refuse a reset with
# no proof and one with an empty brace.
fixy_test(test_handle_one_shot_flag)
fixy_neg_compile_test(neg_handle_one_shot_flag_reset_without_proof
  "no matching function for call to .fixy::handle::OneShotFlag::reset_in_quiescent_context\\(\\)."
  "candidate expects 1 argument, 0 provided")
fixy_neg_compile_test(neg_handle_one_shot_flag_reset_braced_proof
  "converting to .fixy::handle::OneShotFlag::QuiescenceProof. from initializer list would use explicit constructor"
  "fixy::handle::OneShotFlag::QuiescenceProof::QuiescenceProof\\(\\)")

# The lazily established channel gives one session over its resource, to
# the first observer after the publish.  The mint reads the gate of
# fixy::session::mint_session, so a value that is not a context and a
# context without the effect of the protocol are refused.  A Resource
# that can move has no stable address for the session to hold.
fixy_test(test_handle_lazy_channel)
fixy_neg_compile_test(neg_handle_lazy_channel_mint_without_context
  "no matching function for call to .fixy::handle::LazyEstablishedChannel<.*>::mint_established_session\\(int\\)."
  "IsExecCtx")
fixy_neg_compile_test(neg_handle_lazy_channel_mint_context_lacks_io
  "no matching function for call to .fixy::handle::LazyEstablishedChannel<.*>::mint_established_session"
  "CtxFitsSession")
fixy_neg_compile_test(neg_handle_lazy_channel_resource_not_pinned
  "static assertion failed"
  "LazyEstablishedChannel<Proto, Resource>: the Resource must derive from foundation::Pinned<Resource>")

# The standing witness for the guard that once admitted
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

# PublishSlot makes its own pointee check, apart from PublishOnce's.
fixy_neg_compile_test(neg_handle_publish_slot_pointer_pointee
  "static assertion failed"
  "PublishSlot<T> hands off a T\\*, so T is the pointee")

# The three preconditions of SetOnce, reached in a constant evaluation.
fixy_neg_compile_test(neg_handle_set_once_null
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .slot\\.fixy::handle::SetOnce<int>::set\\(0\\)."
  "fixy/handle/Once\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_handle_set_once_twice
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .slot\\.fixy::handle::SetOnce<int>::set\\(\\(& second\\)\\)."
  "fixy/handle/Once\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_handle_set_once_reset_unset
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .slot\\.fixy::handle::SetOnce<int>::reset\\(\\)."
  "fixy/handle/Once\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# Once::call and Lazy::get_or_init each refuse, at the door, an argument
# their body cannot use as it must.
fixy_neg_compile_test(neg_handle_once_call_not_invocable
  "no matching function for call to .fixy::handle::Once::call\\(int\\)."
  "the expression .is_invocable_v<F> \\[with F = int\\]. evaluated to .false.")
fixy_neg_compile_test(neg_handle_lazy_init_explicit_conversion
  "no matching function for call to .fixy::handle::Lazy<int>::get_or_init"
  "the expression .is_invocable_r_v<T, F> \\[with T = int; F = .*\\]. evaluated to .false.")

# ── The fixy/os smoke tests, migrated out of the headers ─────────────
#
# Each of these used to be a `runtime_smoke_test` inside the header it
# exercises, compiled into every translation unit that included it.
# Removing them is also what let three entries leave
# scripts/ctx-testing-boundary-allowlist.txt: the headers no longer name
# foundation::effects::testing, and the guard fails on a stale entry.
fixy_test(test_os_time)

# The gate and the clamp safety/_Mutation.h's MonotonicClock carried,
# restored to fixy/os/Time.h.  A clock read on the replay-bound
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

# The TSC has no clock id, so the clock-reader mint has no candidate for
# it, and a TSC read goes through the pin-gated mint.
fixy_neg_compile_test(neg_os_clock_reader_tsc_source
  "no matching function for call to .mint_clock_reader<foundation::algebra::lattices::ClockSource::TscRaw>\\(const fixy::TestRunnerCtx&\\)."
  "required for the satisfaction of .ClockBacked<Source>. \\[with Source = foundation::algebra::lattices::ClockSource::TscRaw\\]")

# The sleeper blocks, so its mint asks for Block, and its bound keeps the
# deadline in 64 bits.  One fixture for each.
fixy_neg_compile_test(neg_os_bounded_sleep_ctx_lacks_block
  "no matching function for call to .mint_bounded_sleep<1000>\\(const fixy::BgDrainCtx&\\)."
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Block>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
fixy_neg_compile_test(neg_os_bounded_sleep_bound_past_limit
  "no matching function for call to .mint_bounded_sleep<\\(4611686018427387904 \\+ 1\\)>\\(const fixy::TestRunnerCtx&\\)."
  "the expression .MaxNanos <= fixy::time::max_bounded_sleep_nanos \\[with MaxNanos = 4611686018427387905\\]. evaluated to .false.")

# The PTP clock reader.  Its gate folds the clock gate and the file
# system gate into one concept, because the mint opens /dev/ptpN.  The
# first fixture is a foreground context, and the clock half refuses it.
# The second is a background context with IO and without Block.  The
# clock half admits it, and the file system half refuses it.  In each,
# the first regex holds the anonymous-namespace prefix.  The second names
# the half that failed, the third names the folded gate, and the fourth
# names the effects that the half demanded.  The compiler spells all four,
# and the source spells none of them.
fixy_neg_compile_test(neg_os_ptp_clock_reader_foreground_ctx
  "no matching function for call to .mint_ptp_clock_reader\\(const \\{anonymous\\}::ForegroundCtx&, fixy::time::PtpDeviceIndex&\\)"
  "required for the satisfaction of .CtxFitsMonotonicClock<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]"
  "required for the satisfaction of .CtxFitsPtpClockReaderMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]"
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init, foundation::effects::Effect::Test\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_ptp_clock_reader_ctx_lacks_block
  "no matching function for call to .mint_ptp_clock_reader\\(const \\{anonymous\\}::BackgroundIoCtx&, fixy::time::PtpDeviceIndex&\\)"
  "required for the satisfaction of .CtxAdmitsFs<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::IO> >\\]"
  "required for the satisfaction of .CtxFitsPtpClockReaderMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::IO> >\\]"
  "Es = \\{foundation::effects::Effect::IO, foundation::effects::Effect::Block\\}\\]. evaluated to .false.")
fixy_test(test_os_sched)

# A SCHED_DEADLINE class carries its CBS budget in the type.  A budget
# that the kernel does not admit fails the class-body static_assert at the
# instantiation of the class.
fixy_neg_compile_test(neg_os_sched_class_deadline_runtime_exceeds_deadline
  "static assertion failed: SchedClass<Deadline, \\.\\.\\.>: SCHED_DEADLINE requires RuntimeNs < DeadlineNs <= PeriodNs"
  "In instantiation of .class fixy::SchedClass<foundation::algebra::lattices::SchedulerPolicy::Deadline, int, 100, 50, 200>.")

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
  "CpuPinned\\(Unit, uint64_t\\)")

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

# A deleted copy with a defaulted move left the pin trivially copyable,
# so std::bit_cast built one.  The move is user-provided now.
fixy_neg_compile_test(neg_os_cpu_pinned_bit_cast
  "no matching function for call to .bit_cast"
  "is_trivially_copyable_v<_To>")

fixy_neg_compile_test(neg_os_cpu_pinned_start_as_array
  "no matching function for call to .start_as_array<Pin>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = fixy::CpuPinned<")

# ── A clock reading comes only from a read of that clock ────────────
#
# The value constructor, the in_place constructor, the free mint and
# peek_mut each stamped a value that no clock returned.  The one stamp is
# private to the readers of fixy/os/Time.h now, and the reading is not
# trivially copyable, so no byte pattern becomes a reading.
fixy_neg_compile_test(neg_os_clock_source_value_ctor
  "is private within this context"
  "fixy::ClockSource<Source, T>::ClockSource\\(T\\)")

fixy_neg_compile_test(neg_os_clock_source_bit_cast
  "no matching function for call to .bit_cast"
  "is_trivially_copyable_v<_To>")

fixy_neg_compile_test(neg_os_clock_source_start_as_array
  "no matching function for call to .start_as_array<Reading>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = fixy::ClockSource<")

fixy_neg_compile_test(neg_os_clock_source_peek_mut
  "has no member named .peek_mut"
  "class fixy::ClockSource<foundation::algebra::lattices::ClockSource::Boot, long unsigned int>")

# ── A thread-name witness comes only from mint_thread_name ──────────
#
# The witness was an aggregate.  Its one constructor is private now, the
# mint is the sole friend, and the witness is neither copyable nor movable,
# so it stays in the frame of the thread that it names.  GCC reports a
# class whose copy and move are deleted as trivially copyable, so the
# bit_cast route is refused by the deleted move, which the third fixture
# keeps.
fixy_neg_compile_test(neg_os_thread_named_forged
  "is private within this context"
  "fixy::ThreadNamed<Name>::ThreadNamed\\(\\)")

fixy_neg_compile_test(neg_os_thread_named_moved
  "use of deleted function"
  "a move could carry it to a thread that carries no such name")

fixy_neg_compile_test(neg_os_thread_named_bit_cast
  "use of deleted function"
  "a thread-name witness is bound to the thread that minted it")

fixy_neg_compile_test(neg_os_thread_named_start_as_array
  "no matching function for call to .start_as_array<Witness>"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = fixy::ThreadNamed<")

# The name mint asks for an init context.  One fixture for a background
# context, and one for a value that is not a context.
fixy_neg_compile_test(neg_os_thread_name_bg_ctx
  "no matching function for call to .mint_thread_name<\"crux-bg\">\\(const \\{anonymous\\}::BgDrainCtx&\\)."
  "required for the satisfaction of .CtxIsInitPhase<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
fixy_neg_compile_test(neg_os_thread_name_not_a_ctx
  "no matching function for call to .mint_thread_name<\"crux-int\">\\(int\\)."
  "required for the satisfaction of .CtxIsInitPhase<Ctx>. \\[with Ctx = int\\]")

# ── The TSC reader recognises a pin by reflection ────────────────────
#
# IsSingletonCpuPin recognises a CpuPinned by the reflection query of
# foundation/reflect/Instance.h.  The first fixture is a class that
# copies the shape of a pin, refused at the mint.  The second tries to
# specialize the variable template the concept once read, which let a
# translation unit declare any class a pin.  That template is gone, so
# the specialization names nothing.
fixy_neg_compile_test(neg_os_tsc_reader_shape_of_a_pin
  "required for the satisfaction of .IsInstanceOf<T, \\^\\^fixy::CpuPinned>. .with T = forged_pin::CopiedShape"
  "required for the satisfaction of .IsSingletonCpuPin<PinT>.")
fixy_neg_compile_test(neg_os_tsc_reader_recogniser_specialized
  "expected initializer before .<. token"
  "recogniser_specialized\\.cpp:[0-9]+:[0-9]+: error")

# The rest of the gate of the TSC mint: the pin is given up, not lent, it
# has the explicit posture, and the context is off the foreground path.
fixy_neg_compile_test(neg_os_tsc_reader_lvalue_pin
  "no matching function for call to .mint_tsc_reader<fixy::time::TscMode::Raw>\\(const \\{anonymous\\}::InitCtx&, \\{anonymous\\}::Pin&\\)."
  "the expression .!\\(is_reference_v<PinT>\\) \\[with PinT = fixy::CpuPinned<")
fixy_neg_compile_test(neg_os_tsc_reader_auto_posture
  "no matching function for call to .mint_tsc_reader<fixy::time::TscMode::Raw>\\(const \\{anonymous\\}::InitCtx&"
  "the expression .PinT::meets_posture<fixy::PinningPosture::PinnedExplicit> \\[with PinT = fixy::CpuPinned<.*fixy::PinningPosture::PinnedAuto, int>\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_tsc_reader_foreground_ctx
  "no matching function for call to .mint_tsc_reader<fixy::time::TscMode::Raw>\\(const fixy::HotFgCtx&"
  "required for the satisfaction of .CtxFitsMonotonicClock<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# ── Every scheduling door reads one gate ─────────────────────────────
#
# mint_affinity, mint_scheduler_policy and mint_priority read the same
# gate as apply_affinity_to_cpu: the context must own Bg or Init.  Two
# fixtures for each mint.  The first context is the foreground one,
# with the empty row.  The second is a test context, with a wide row
# that still holds neither effect.  The second regex names the gate as
# the compiler spells its context, which the fixture source does not.
fixy_neg_compile_test(neg_os_sched_mint_affinity_foreground_ctx
  "no matching function for call to .mint_affinity<"
  "required for the satisfaction of .CtxFitsRuntimeAffinity<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_sched_mint_affinity_test_ctx
  "no matching function for call to .mint_affinity<"
  "required for the satisfaction of .CtxFitsRuntimeAffinity<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, "
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_sched_mint_scheduler_policy_foreground_ctx
  "no matching function for call to .mint_scheduler_policy<"
  "required for the satisfaction of .CtxFitsRuntimeAffinity<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_sched_mint_scheduler_policy_test_ctx
  "no matching function for call to .mint_scheduler_policy<"
  "required for the satisfaction of .CtxFitsRuntimeAffinity<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, "
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_sched_mint_priority_foreground_ctx
  "no matching function for call to .mint_priority<5>"
  "required for the satisfaction of .CtxFitsRuntimeAffinity<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_sched_mint_priority_test_ctx
  "no matching function for call to .mint_priority<5>"
  "required for the satisfaction of .CtxFitsRuntimeAffinity<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, "
  "Es = \\{foundation::effects::Effect::Bg, foundation::effects::Effect::Init\\}\\]. evaluated to .false.")

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

# The ring mint: a context without Block, a submission count that is not
# a power of two, and one ring flag named two times.
fixy_neg_compile_test(neg_os_io_uring_ring_ctx_lacks_block
  "no matching function for call to .mint_io_uring_ring<\\{anonymous\\}::Engine, \\{anonymous\\}::Sq8>\\(const \\{anonymous\\}::IoOnlyCtx&\\)."
  "the expression .is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::IO>\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_io_uring_ring_sq_not_pow2
  "no matching function for call to .mint_io_uring_ring<\\{anonymous\\}::Engine, \\{anonymous\\}::Sq7>\\(const \\{anonymous\\}::IoBlockCtx&\\)."
  "the expression .sq_entries_is_pow2_v<Atoms ...> \\[with Atoms = \\{fixy::atom::io::engine<fixy::io::engine::IoUring>, fixy::atom::io::sq_entries<7>\\}\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_io_uring_ring_repeated_flag
  "no matching function for call to .mint_io_uring_ring<\\{anonymous\\}::Engine, \\{anonymous\\}::Sq8, \\{anonymous\\}::SqPoll, \\{anonymous\\}::SqPoll>\\(const \\{anonymous\\}::IoBlockCtx&\\)."
  "the expression .!\\(RepeatsAnAtomOf<.*, Atoms ...>\\) \\[with Atoms = \\{.*fixy::atom::io::ring_flag<fixy::io::ring_flag::SqPoll>, fixy::atom::io::ring_flag<fixy::io::ring_flag::SqPoll>\\}\\]. evaluated to .false.")

# ── A discard of pages takes the permission of the mapping ───────────
#
# advise_release_aware took Permission<RegionTag> with RegionTag free, so
# a permission of any region admitted a discard of any mapping.  The
# mapping takes its tag and its brand from the permission it is minted
# with, and the gate compares each with the proof.  One fixture for each
# clause: another tag, another brand of the same tag, and a mapping on
# the erased brand, where an erased proof would agree.
fixy_neg_compile_test(neg_os_mmap_release_other_tag
  "no matching function for call to .advise_release_aware<fixy::mmap::advice::DontNeed>\\("
  "required for the satisfaction of .ProofNamesMappingTag<Proof, Region>. .with Proof = foundation::permissions::Permission<<unnamed>::OtherRegion, "
  "Tag = <unnamed>::MappedRegion.. evaluated to .false.")

fixy_neg_compile_test(neg_os_mmap_release_other_brand
  "no matching function for call to .advise_release_aware<fixy::mmap::advice::DontNeed>\\("
  "required for the satisfaction of .ProofNamesMappingBrand<Proof, Region>. .with Proof = foundation::permissions::Permission<<unnamed>::MappedRegion, "
  "required for the satisfaction of .SameBrand<Proof, Region>.")

fixy_neg_compile_test(neg_os_mmap_release_erased_mapping
  "no matching function for call to .advise_release_aware<fixy::mmap::advice::DontNeed>\\("
  "required for the satisfaction of .MappingIsBranded<Region>. .with Region = fixy::OwnedMmap<<unnamed>::MappedRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous, foundation::brand::DefaultBrand>"
  "the expression .!\\(is_same_v<B, foundation::brand::DefaultBrand>\\) .with B = foundation::brand::DefaultBrand.. evaluated to .false.")

# The plain advise surface refuses each advice that lets the kernel
# discard the pages, Free as well as DontNeed.  The MADV_* value of a tag
# is a row of a closed table, so no class of a caller brings a value of
# its own, and a specialization of a variable template of advice values
# names nothing.
fixy_neg_compile_test(neg_os_mmap_free_on_safe_surface
  "no matching function for call to .advise<fixy::mmap::advice::Free>\\("
  "the expression .!\\(DiscardsPages<Advice>\\) \\[with Advice = fixy::mmap::advice::Free\\]. evaluated to .false.")
fixy_neg_compile_test(neg_os_mmap_foreign_advice_row
  "expected initializer before .<. token"
  "neg_os_mmap_foreign_advice_row\\.cpp:17:48: error:")

# ── A mapping is minted with the exclusive permission of its region ──
#
# mint_mmap and mint_mmap_anon read the tag and the brand of the
# permission the caller presents, and the mapping takes both.  Three
# fixtures for each mint, one reason each: a share where the exclusive
# is asked for, a context whose row lacks IO, and a capability token in
# the place of a context.
fixy_neg_compile_test(neg_os_mmap_shared_permission
  "no matching function for call to .mint_mmap<\\{anonymous\\}::ReadOnly, \\{anonymous\\}::Shared>\\(\\{anonymous\\}::IoBlockCtx&, foundation::permissions::SharedPermissionGuard<"
  ".foundation::permissions::SharedPermissionGuard<\\{anonymous\\}::FileRegion, foundation::permissions::<lambda\\(\\)> >. is not derived from .const foundation::permissions::Permission<Tag, Brand>.")

fixy_neg_compile_test(neg_os_mmap_ctx_lacks_io
  "no matching function for call to .mint_mmap<\\{anonymous\\}::ReadOnly, \\{anonymous\\}::Shared>\\(\\{anonymous\\}::BlockOnlyCtx&"
  "required for the satisfaction of .CtxFitsMmapMint<Ctx, Atoms ...>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::Block> >"
  "the expression .is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::Block>.. evaluated to .false.")

fixy_neg_compile_test(neg_os_mmap_non_ctx
  "no matching function for call to .mint_mmap<\\{anonymous\\}::ReadOnly, \\{anonymous\\}::Shared>\\(const foundation::effects::Test&"
  "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = foundation::effects::Test.")

fixy_neg_compile_test(neg_os_mmap_anon_pool_share
  "no matching function for call to .mint_mmap_anon<\\{anonymous\\}::WriteAnon, \\{anonymous\\}::Anonymous>\\(\\{anonymous\\}::IoBlockCtx&, const foundation::permissions::SharedPermission<"
  ".const foundation::permissions::SharedPermission<\\{anonymous\\}::AnonRegion, foundation::permissions::<lambda\\(\\)> >. is not derived from .const foundation::permissions::Permission<Tag, Brand>.")

fixy_neg_compile_test(neg_os_mmap_anon_ctx_lacks_io
  "no matching function for call to .mint_mmap_anon<\\{anonymous\\}::WriteAnon, \\{anonymous\\}::Anonymous>\\(\\{anonymous\\}::BlockOnlyCtx&"
  "required for the satisfaction of .CtxFitsAnonMmapMint<Ctx, Atoms ...>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::Block> >"
  "the expression .is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::Block>.. evaluated to .false.")

fixy_neg_compile_test(neg_os_mmap_anon_non_ctx
  "no matching function for call to .mint_mmap_anon<\\{anonymous\\}::WriteAnon, \\{anonymous\\}::Anonymous>\\(const foundation::effects::Test&"
  "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = foundation::effects::Test.")

# ── A NUMA placement comes only from a successful mbind ─────────────
#
# The proof owns an anonymous region, and fixy::numa::mint_numa_placement
# is the sole friend of its one value constructor.  The positive test
# binds a real region and reads the policy back from the kernel.  The
# fixtures close the other routes to a proof: the default constructor,
# a copy, bytes through std::bit_cast and through the checked lifetime
# start, a shared region, and two contexts that do not own IO and Block.
fixy_test(test_os_numa_placement)

fixy_neg_compile_test(neg_os_numa_placement_default_ctor
  "use of deleted function"
  "would claim a binding that nobody made")

fixy_neg_compile_test(neg_os_numa_placement_copied
  "use of deleted function"
  "a copy would unmap it twice")

fixy_neg_compile_test(neg_os_numa_placement_bit_cast
  "no matching function for call to .bit_cast"
  "is_trivially_copyable_v<_To> \\[with _To = fixy::NumaPlacement<")

fixy_neg_compile_test(neg_os_numa_placement_start_as_array
  "no matching function for call to .start_as_array<"
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. \\[with T = fixy::NumaPlacement<")

fixy_neg_compile_test(neg_os_numa_placement_shared_region
  "no matching function for call to .mint_numa_placement\\("
  "mismatched types .fixy::mmap::share::Anonymous. and .fixy::mmap::share::Shared.")

fixy_neg_compile_test(neg_os_numa_placement_io_only_ctx
  "no matching function for call to .mint_numa_placement\\("
  "required for the satisfaction of .CtxFitsNumaBind<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::IO> >\\]")

fixy_neg_compile_test(neg_os_numa_placement_foreground_ctx
  "no matching function for call to .mint_numa_placement\\("
  "required for the satisfaction of .CtxFitsNumaBind<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# ── OwnedMmap has one door, and it is the one that maps ──────────────
#
# The constructor over an address is private, and OwnedMmap::mint_region
# performs the ::mmap itself.  Two routes to a forged region, because the
# region is handed out inside Linear and the generic mint_linear forwarder
# sits between the mints and the object.  The direct route fails on
# access, and the forwarded route finds no reachable constructor.
fixy_neg_compile_test(neg_owned_mmap_forged_address
  "is private within this context"
  "OwnedMmap\\(void\\*, std::size_t\\)")

fixy_neg_compile_test(neg_owned_mmap_forged_through_mint_linear
  "no matching function for call"
  "mint_linear<.*::Region>\\(void\\*, long unsigned int\\)")

# The door calculates the PROT_* and MAP_* words from the region type,
# so the type states what the kernel mapped.  One fixture for each class
# of refusal: a shared range typed as an anonymous one, an executable
# page with no licence, and a context without Block.
fixy_neg_compile_test(neg_owned_mmap_region_shared_as_modifier
  "no matching function for call to .fixy::OwnedMmap<\\{anonymous\\}::Region, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous, "
  "the expression .\\(RegionModifier<Modifiers> && \\.\\.\\.\\) \\[with Modifiers = \\{fixy::mmap::share::Shared\\}\\]. evaluated to .false.")

fixy_neg_compile_test(neg_owned_mmap_region_exec_without_jit
  "no matching function for call to .fixy::OwnedMmap<\\{anonymous\\}::Region, fixy::mmap::prot::Exec, fixy::mmap::share::Private, "
  "required for the satisfaction of .ExecIsLicensed<Prot, Modifiers \\.\\.\\.>. .with Prot = fixy::mmap::prot::Exec")

fixy_neg_compile_test(neg_owned_mmap_region_ctx_lacks_block
  "no matching function for call to .fixy::OwnedMmap<\\{anonymous\\}::Region, fixy::mmap::prot::ReadOnly, fixy::mmap::share::Private, "
  "required for the satisfaction of .CtxAdmitsMapping<Ctx, Prot, Share, Modifiers \\.\\.\\.>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::IO> >")

# The PROT_* word of a tag is a row of a closed table, so no class of a
# caller brings bits of its own, and a specialization of a variable
# template of protection words names nothing.
fixy_neg_compile_test(neg_owned_mmap_foreign_prot_row
  "expected initializer before .<. token"
  "neg_owned_mmap_foreign_prot_row\\.cpp:17:45: error:")

# ── A descriptor comes only from a gated mint ────────────────────────
#
# OwnedFd had three public static factories, and each opened a descriptor
# with no context and no gate.  The calls moved into door classes whose
# members are private, and only the gated mints are friends.
fixy_neg_compile_test(neg_os_fs_file_door_is_private
  "is private within this context"
  "fixy::fs::FileDoor::open_path_\\(const char\\*, int, mode_t\\)")

fixy_neg_compile_test(neg_os_socket_door_is_private
  "is private within this context"
  "fixy::net::SocketDoor::open_\\(\\) .with Kind = fixy::net::socket_kind::NetlinkRoute.")

fixy_neg_compile_test(neg_os_ptp_device_door_is_private
  "is private within this context"
  "fixy::time::PtpDeviceDoor::open_\\(fixy::time::PtpDeviceIndex\\)")

# ── OwnedFile has two doors, and both are the ones that open ─────────
#
# The same defect as OwnedMmap, over a FILE*: the constructor was public
# and the destructor fcloses whatever it holds, so `OwnedFile{stdin}`
# closed standard input on scope exit.  The constructor is private now,
# and OwnedFileDoor performs the open for mint_owned_file and
# mint_temporary_file.
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

# OwnedFile::release has the two gates of OwnedMmap::release: a leak atom
# as the witness, and an rvalue handle.
fixy_neg_compile_test(neg_owned_file_release_without_leak_atom
  "no matching function for call to .fixy::OwnedFile::release\\("
  "required for the satisfaction of .IsLeakAtom<LeakAtom>."
  "required for the satisfaction of .IsInstanceOf<G, \\^\\^fixy::atom::leak::resource>. \\[with G = <unnamed>::NotALeakAtom\\]")

fixy_neg_compile_test(neg_owned_file_release_from_an_lvalue
  "passing .fixy::OwnedFile. as .this. argument discards qualifiers"
  "in call to .FILE\\* fixy::OwnedFile::release\\(LeakAtom\\) &&")

# Each open enters the kernel and can wait on the file system, so the two
# mints ask for a context that owns IO and Block.  One fixture for each
# missing atom, for each mint.
fixy_neg_compile_test(neg_owned_file_mint_without_block
  "no matching function for call to .mint_owned_file\\(const foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::IO> >&"
  "required for the satisfaction of .CtxFitsFileOpen<Ctx>.")

fixy_neg_compile_test(neg_owned_file_mint_without_io
  "no matching function for call to .mint_owned_file\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >&"
  "required for the satisfaction of .CtxFitsFileOpen<Ctx>.")

fixy_neg_compile_test(neg_owned_file_temporary_without_block
  "no matching function for call to .mint_temporary_file\\(const foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::IO> >&\\)"
  "required for the satisfaction of .CtxFitsFileOpen<Ctx>.")

fixy_neg_compile_test(neg_owned_file_temporary_without_io
  "no matching function for call to .mint_temporary_file\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&\\)"
  "required for the satisfaction of .CtxFitsFileOpen<Ctx>.")

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

# The socket mint.  The first fixture is a context without Block, which
# the row of the socket syscall atom demands.  The second is a kind tag
# with no triple.  Each second regex holds the anonymous-namespace prefix
# that the compiler emits and the source does not.
fixy_test(test_os_socket)
fixy_neg_compile_test(neg_os_socket_ctx_lacks_block
  "no matching function for call"
  "mint_socket<fixy::net::socket_kind::NetlinkRoute>\\(\\{anonymous\\}::IoOnlyCtx&\\)")
fixy_neg_compile_test(neg_os_socket_unmapped_kind
  "no matching function for call"
  "mint_socket<\\{anonymous\\}::NotASocketKind>\\(\\{anonymous\\}::IoBlockCtx&\\)")

fixy_neg_compile_test(neg_os_fs_mint_file_duplicate_mode
  "no matching function for call"
  "mode<fixy::fs::open_mode::WriteTruncate> >\\(\\{anonymous\\}::IoBlockCtx&")
# The bits of a flag are a row of a closed table, so a specialization of
# a variable template of flag bits names nothing.
fixy_neg_compile_test(neg_os_fs_foreign_flag_row
  "expected initializer before .<. token"
  "neg_os_fs_foreign_flag_row\\.cpp:17:43: error:")

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

# atomicity::None renames nothing, so the commit gate refuses it.
fixy_neg_compile_test(neg_os_fs_commit_atomic_none
  "no matching function for call to .commit_atomic<fixy::fs::atomicity::None>\\("
  "the expression .!\\(is_same_v<Atomicity, fixy::fs::atomicity::None>\\) \\[with Atomicity = fixy::fs::atomicity::None\\]. evaluated to .false.")

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
# cannot see.  scripts/check-witness-roster.py --gen writes
# neg/neg_witness_<slug>_direct_construction.cpp for each closed line,
# and it writes neg/witness-fixtures.json with one record per fixture:
# the test name, the refusal reason, and the refused constructor as the
# compiler prints it, with parentheses, which a brace-spelled fixture
# never contains, so the source cannot satisfy its own gate.  This loop
# reads that manifest, so the slug and the regexes have one
# implementation, in the generator.  --check refuses a manifest that the
# roster does not produce, and the guard's self-test checks that ctest
# registers precisely the fixtures on disk.
set(_witness_manifest ${CMAKE_CURRENT_SOURCE_DIR}/neg/witness-fixtures.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_witness_manifest})
file(READ ${_witness_manifest} _witness_json)
string(JSON _witness_count ERROR_VARIABLE _witness_error LENGTH "${_witness_json}" fixtures)
if(_witness_error)
  message(FATAL_ERROR "${_witness_manifest}: ${_witness_error}.  "
    "Run scripts/check-witness-roster.py --gen.")
endif()
if(_witness_count GREATER 0)
  math(EXPR _witness_last "${_witness_count} - 1")
  foreach(_witness_index RANGE ${_witness_last})
    string(JSON _witness_name GET "${_witness_json}" fixtures ${_witness_index} name)
    string(JSON _witness_why GET "${_witness_json}" fixtures ${_witness_index} reason)
    string(JSON _witness_constructor GET "${_witness_json}" fixtures ${_witness_index} constructor)
    fixy_neg_compile_test(${_witness_name} "${_witness_why}" "${_witness_constructor}")
  endforeach()
endif()

# ── The cross-wrapper harness and the cheat probe ─────────────────────
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

# ── The spin gate, the throws walk, fp, and spawn ─────────────────────
#
# fixy/Throws.h carries its whole claim as static assertions, so it has
# no runtime unit of its own.  The units below are the parts that only a
# constructed sequence of calls can show.
#
# test_fp_canonicalize runs the projection of fixy/fp/Canonicalize.h on
# the host at run time, and it compares each result with an oracle that
# reads the IEEE 754 fields.
fixy_test(test_fp_canonicalize)
#
# test_fp_polynomial re-checks at run time every value the header pins at
# compile time.  The two are different arithmetic engines, GCC's constant
# folding and the host FPU, and the claim is that they agree.
fixy_test(test_fp_polynomial)

# test_fp_polynomial_contracted is the same source compiled with
# -ffp-contract=on and fused multiply-add instructions, where a written
# a * b + c can fuse.  It must reach the same pinned digests, and it checks
# that it does contract.  The source options come after the
# -ffp-contract=off of the strict FP floor, and the compiler uses the last.
fixy_test(test_fp_polynomial_contracted)
set_property(SOURCE test_fp_polynomial_contracted.cpp APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=on)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set_property(SOURCE test_fp_polynomial_contracted.cpp APPEND PROPERTY COMPILE_OPTIONS -mfma)
endif()
set_property(SOURCE test_fp_polynomial_contracted.cpp APPEND PROPERTY
  COMPILE_DEFINITIONS CRUCIBLE_TEST_FP_CONTRACTED=1)

# A constant evaluation that reaches a domain precondition of the
# polynomials stops at its consteval trap.  Three fixtures refuse angles
# outside one turn, at sin_poly, cos_poly and reduce_quarter_pi, and three
# refuse arguments of log_poly that are not positive normal floats.
fixy_neg_compile_test(neg_fp_sin_poly_past_one_turn
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::fp::sin_poly\\(6\\.28318596e\\+0f\\)."
  "fixy/fp/Polynomial\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_fp_cos_poly_of_nan
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::fp::cos_poly\\(std::numeric_limits<float>::quiet_NaN\\(\\)\\)."
  "fixy/fp/Polynomial\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_fp_reduce_quarter_pi_past_int32
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::fp::reduce_quarter_pi\\(-1\\.0e\\+10f\\)."
  "fixy/fp/Polynomial\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_fp_log_poly_of_zero
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::fp::log_poly\\(0\\.0f\\)."
  "fixy/fp/Polynomial\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_fp_log_poly_of_negative
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::fp::log_poly\\(-2\\.0e\\+0f\\)."
  "fixy/fp/Polynomial\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
fixy_neg_compile_test(neg_fp_log_poly_of_subnormal
  "non-constant condition for static assertion"
  "in .constexpr. expansion of .fixy::fp::log_poly\\(std::numeric_limits<float>::denorm_min\\(\\)\\)."
  "fixy/fp/Polynomial\\.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# test_os_spinlock acquires and releases through every door of the spin
# gate and the blocking gate, and wakes a waiter that sleeps on the
# blocking gate.  The cells that prove a door is CLOSED are the eight
# fixtures below: a refused door is a compile error, not a runtime result.
fixy_test(test_os_spinlock)

# test_os_spawn runs both mints for real: that every child body ran, that
# the parent Permission comes back, that the budget chooses the arm, and
# that a recombined region still describes the storage it started with.
fixy_test(test_os_spawn)

# test_parallelism_rule_fixy asks the cost model the questions whose
# answers hold on any host: the tier boundaries, the sequential answer
# inside L2, the factor ladder and the container cap.  The old test of the
# same name is a separate target, and a CMake target name is global.
fixy_test(test_parallelism_rule_fixy test_parallelism_rule.cpp)

# Every second regex is text the compiler emits and the fixture source
# does not: the signature of a private member with its template
# arguments, a candidate-arity line, a constraint note naming the concept,
# or a sentence from the header's own static_assert.  So no fixture can
# satisfy its own gate by echoing itself.

# Deviations 1 and 3 of fixy/os/SpinLock.h: the three doors are private,
# and the guard is the way in and out.  A public door beside the guard
# would let a caller acquire with no context, or release a gate that it
# does not hold.
fixy_neg_compile_test(neg_spin_lock_door_is_private
  "is private within this context"
  "fixy::spin::Gate<Tag, Strategy>::lock\\(\\) \\[with Tag = \\{anonymous\\}::GateTag. foundation::algebra::lattices::WaitStrategy Strategy = foundation::algebra::lattices::WaitStrategy::BoundedSpin\\]")

fixy_neg_compile_test(neg_spin_try_lock_door_is_private
  "is private within this context"
  "fixy::spin::Gate<Tag, Strategy>::try_lock\\(\\) \\[with Tag = \\{anonymous\\}::GateTag. foundation::algebra::lattices::WaitStrategy Strategy = foundation::algebra::lattices::WaitStrategy::BoundedSpin\\]")

fixy_neg_compile_test(neg_spin_unlock_door_is_private
  "is private within this context"
  "fixy::spin::Gate<Tag, Strategy>::unlock\\(\\) \\[with Tag = \\{anonymous\\}::GateTag. foundation::algebra::lattices::WaitStrategy Strategy = foundation::algebra::lattices::WaitStrategy::BoundedSpin\\]")

# Deviation 2, the spin side: a background context may also allocate,
# syscall or block.  The context in the fixture owns Block as well, so
# owning Block does not open the spin gate to background work.  A cold
# init context owns no Bg, but it owns Alloc and IO, and the spin gate
# refuses it as well.
fixy_neg_compile_test(neg_spin_guard_under_init_ctx
  "no operand of the disjunction is satisfied"
  "required for the satisfaction of .CtxMayAcquire<Ctx, Strategy>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<")

fixy_neg_compile_test(neg_spin_guard_under_bg_ctx
  "no operand of the disjunction is satisfied"
  "required for the satisfaction of .CtxMayAcquire<Ctx, Strategy>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >; Strategy = foundation::algebra::lattices::WaitStrategy::BoundedSpin\\]")

fixy_neg_compile_test(neg_spin_lock_without_permission
  "no matching function for call to .fixy::spin::GateGuard<\\{anonymous\\}::GateTag, foundation::algebra::lattices::WaitStrategy::BoundedSpin, foundation::brand::DefaultBrand>::GateGuard\\("
  "candidate expects 3 arguments, 2 provided")

# Deviation 2, the blocking side: a wait on the blocking gate is a block,
# so a context that owns no Block is refused at the guard.
fixy_neg_compile_test(neg_blocking_guard_without_block
  "no operand of the disjunction is satisfied"
  "required for the satisfaction of .CtxMayAcquire<Ctx, Strategy>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >; Strategy = foundation::algebra::lattices::WaitStrategy::AcquireWait\\]")

fixy_neg_compile_test(neg_blocking_guard_without_permission
  "no matching function for call to .fixy::spin::GateGuard<\\{anonymous\\}::GateTag, foundation::algebra::lattices::WaitStrategy::AcquireWait, \\{anonymous\\}::GateBrand>::GateGuard\\("
  "candidate expects 3 arguments, 2 provided")

# Deviation 1 of fixy/os/Spawn.h, and the hole fixy/Throws.h closes: the
# callable here carries the atom at a NAMED exception family, which the
# old needle did not match because it compared against the default
# specialization alone.
fixy_neg_compile_test(neg_spawn_callable_carries_throws
  "static assertion failed: mint_spawn: a callable.s type carries fixy::atom::ctrl::throws"
  "tears through the join")

# The same refusal when the atom sits in a member of a plain callable,
# where no template argument names it.
fixy_neg_compile_test(neg_spawn_callable_member_carries_throws
  "static assertion failed: mint_spawn: a callable.s type carries fixy::atom::ctrl::throws"
  "tears through the join")

# A spawn that no join ties to the frame has to say why.
fixy_neg_compile_test(neg_spawn_empty_rationale
  "static assertion failed: detach_with<\"\"> rejected"
  "enumerates every legitimate detach across the codebase")

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
  "required for the satisfaction of .CtxFitsSpawn<Ctx, Parent, Brand, std::tuple<_Ts \\.\\.\\.>")

fixy_neg_compile_test(neg_spawn_mint_parallel_for_ctx_lacks_bg
  "constraints not satisfied"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Bg>.")

# Deviation 4: recombine needs every shard back, so a body that consumes
# its shard is refused.
fixy_neg_compile_test(neg_spawn_mint_parallel_for_body_consumes_shard
  "static assertion failed: a body that consumes its shard must be refused"
  "is_nothrow_invocable_v<Body&, fixy::OwnedRegion<T, fixy::Slice<Whole, 0[^>]*>, Brand>&>")

# The fan-out of mint_parallel_for is a private member of
# ParallelForRunner, and the mint is its only friend: no other scope calls
# it or derives from the runner to reach it.
fixy_neg_compile_test(neg_spawn_parallel_for_runner_is_private
  "is private within this context"
  "ParallelForRunner::run_shards_")
fixy_neg_compile_test(neg_spawn_parallel_for_runner_derived_look_alike
  "cannot derive from .final. base"
  "ParallelForRunner")

# The fan-out runner checks the background capability again, so a
# fixture with more than one shard is refused even without the check on
# the door.  With one shard the runner is never reached, and the door is
# the only check.
fixy_neg_compile_test(neg_spawn_mint_parallel_for_one_shard_ctx_lacks_bg
  "no matching function for call to .mint_parallel_for<1>"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Bg>.")
# Without each check below, the call passes the door and fails later,
# inside the split or the fan-out.  Each fixture names the door and the
# conjunct that fails, so it tells the two apart.
fixy_neg_compile_test(neg_spawn_mint_parallel_for_zero_shards
  "no matching function for call to .mint_parallel_for<0>"
  "the expression .N > 0 \\[with N = 0\\]. evaluated to .false.")
fixy_neg_compile_test(neg_spawn_mint_parallel_for_ctx_refuses_row
  "no matching function for call to .mint_parallel_for<2>"
  "required for the satisfaction of .CtxAdmitsPermission<Whole, Ctx>. \\[with Whole = <unnamed>::IoRegion")
fixy_neg_compile_test(neg_spawn_mint_parallel_for_body_not_copyable
  "no matching function for call to .mint_parallel_for<2>\\(.*MoveOnlyBody\\)"
  "required for the satisfaction of .CtxFitsParallelFor<N, Ctx, T, Whole, Brand, Body>. \\[with N = 2")

# ── The two Qtt discipline recognisers, armed ─────────────────────────
#
# The port carried both gates of fixy/Qtt.h and neither table's arms, so
# Affine<Linear<int>> compiled for a release and an exactly-once
# obligation silently became at-most-once.  Both recognisers are
# structural now, and these four fixtures are what holds them there:
# each names one composition the gates must refuse, and the second
# regex is the compiler's rendering of the instantiation, which the
# fixture source never spells.
fixy_neg_compile_test(neg_qtt_affine_over_linear
  "Affine<Token> over a consume-disciplined token is unsound"
  "QttGrade::Zero, fixy::Qtt<foundation::algebra::lattices::QttGrade::One")

fixy_neg_compile_test(neg_qtt_linear_over_linear
  "Linear<Token> over an already-linear token is redundant"
  "QttGrade::One, fixy::Qtt<foundation::algebra::lattices::QttGrade::One")

fixy_neg_compile_test(neg_qtt_linear_over_permission
  "Linear<Token> over an already-linear token is redundant"
  "QttGrade::One, foundation::permissions::Permission")

fixy_neg_compile_test(neg_qtt_affine_over_shared_permission
  "Affine<Token> over a consume-disciplined token is unsound"
  "QttGrade::Zero, foundation::permissions::SharedPermission")

# ── Brands: identity per mint ────────────────────────────────────────
#
# A region carries the brand of the permission it was built from, and a
# view or a borrow carries its carrier's brand or a fresh one per mint
# site.  Each fixture names one thing the brand refuses, and its second
# regex is the compiler's own rendering of the refusal: the two closure
# types one Brand parameter was deduced to, the constraint on a
# constructor that answered false, or the deleted twin's signature.
# Every fixture was proven live by a mutant that removes its mechanism
# and lets it compile.

# Two regions of one tag from two roots are two brands.
fixy_neg_compile_test(neg_brand_owned_region_cross_brand
  "no matching function for call to .same_region\\("
  "deduced conflicting types for parameter .Brand. \\(.foundation::permissions::<lambda\\(\\)>. and .foundation::permissions::<lambda\\(\\)>.\\)")

# The region's tag is the permission's tag.
fixy_neg_compile_test(neg_brand_owned_region_tag_mismatch
  "no matching function for call to .mint_owned_region<int, \\{anonymous\\}::Buffer>\\("
  "mismatched types .\\{anonymous\\}::Buffer. and .\\{anonymous\\}::OtherBuffer.")

# Two mints of a view are two brands: the refusal by identity.
fixy_neg_compile_test(neg_brand_scoped_view_cross_brand
  "no matching function for call to .same_minting\\("
  "deduced conflicting types for parameter .Brand. \\(.fixy::<lambda\\(\\)>. and .fixy::<lambda\\(\\)>.\\)")

# A view of a temporary carrier: the refusal by value category, which
# is the deleted rvalue twin.
fixy_neg_compile_test(neg_brand_scoped_view_from_temporary_carrier
  "use of deleted function"
  "mint_view\\(const Carrier&&\\)"
  "a view over a temporary carrier outlives it")

# ── The view mints' state gate ───────────────────────────────────────
#
# A carrier declares one view_ok per state it witnesses, and the pair of
# carrier and tag either reaches one or does not.  That half of the gate
# is CarrierDeclaresViewState, and before it the pair reached only the
# contract predicate: both refusals below arrived from inside the header
# after the call site had chosen the factory, and
# `requires { mint_view<Tag>(c) }` answered true for either one.
#
# Each fixture's second regex names the concept and the pair it was
# checked for, which is text the fixture source never spells.  Removing
# the clause leaves the compile failing on view_ok instead, and the
# second regex then matches nothing.
fixy_neg_compile_test(neg_scoped_view_carrier_declares_no_state
  "no matching function for call to .mint_view<"
  "required for the satisfaction of .CarrierDeclaresViewState<Carrier, Tag>. .with Carrier = .*CarrierWithoutPredicate. Tag = .*Ready")
fixy_neg_compile_test(neg_scoped_view_tag_has_no_predicate
  "no matching function for call to .mint_view<"
  "required for the satisfaction of .CarrierDeclaresViewState<Carrier, Tag>. .with Carrier = .*CarrierOpenOnly. Tag = .*Closed")

# The linear form carries the view further than the scoped one, so it
# spells the same gate rather than inheriting it from the inner call.
fixy_neg_compile_test(neg_scoped_view_linear_carrier_declares_no_state
  "no matching function for call to .mint_linear_view<"
  "required for the satisfaction of .CarrierDeclaresViewState<Carrier, Tag>. .with Carrier = .*CarrierWithoutPredicate. Tag = .*Ready")
fixy_neg_compile_test(neg_scoped_view_linear_tag_has_no_predicate
  "no matching function for call to .mint_linear_view<"
  "required for the satisfaction of .CarrierDeclaresViewState<Carrier, Tag>. .with Carrier = .*CarrierOpenOnly. Tag = .*Closed")

# Two mints of a borrow are two brands.
fixy_neg_compile_test(neg_brand_borrowed_cross_brand
  "no matching function for call to .same_minting\\("
  "deduced conflicting types for parameter .Brand. \\(.fixy::<lambda\\(\\)>. and .fixy::<lambda\\(\\)>.\\)")

fixy_neg_compile_test(neg_brand_borrowed_ref_cross_brand
  "no matching function for call to .same_minting\\("
  "deduced conflicting types for parameter .(Brand|Other). \\(.fixy::<lambda\\(\\)>. and .fixy::<lambda\\(\\)>.\\)")

# A brand read off one borrow cannot be spelled onto another object:
# the one public constructor is constrained to the erased brand.
fixy_neg_compile_test(neg_brand_borrowed_ref_forged_brand
  "no matching function for call to .fixy::BorrowedRef<int, fixy::<lambda\\(\\)> >::BorrowedRef\\("
  "the expression .is_same_v<Brand, foundation::brand::DefaultBrand> \\[with Brand = fixy::[^]]*\\]. evaluated to .false.")

# The twins of the two borrow mints.  The template-argument list the
# compiler prints opens with `[` and carries a `;` between arguments,
# and CMake counts brackets when it splits a list: an unbalanced `[` in
# one regex swallows the `;` and the regex that follows it.  So the
# bracket is matched by `.` and the argument list is cut before its
# first separator.
fixy_neg_compile_test(neg_brand_mint_borrowed_ref_from_temporary
  "use of deleted function"
  "mint_borrowed_ref\\(T&&\\) .with T = int"
  "a borrow of a temporary dangles at the end of the full expression")

fixy_neg_compile_test(neg_brand_mint_borrowed_from_temporary_range
  "use of deleted function"
  "mint_borrowed\\(R&&\\) .with Source = \\{anonymous\\}::Owner"
  "a borrow of a temporary range dangles at the end of the full expression")

# ── Disjointness: one split, one receipt, one rebuild ─────────────────
#
# A shard says which split cut it and the receipt says which split wrote
# it, so a rebuild needs both to agree.  Each fixture below names one
# thing that agreement refuses, and each was proven live by a mutant
# that removes its mechanism and lets it compile.

# Shards alone are no longer a call.
fixy_neg_compile_test(neg_disjoint_recombine_without_receipt
  "no matching function for call to .fixy::OwnedRegion<int, \\{anonymous\\}::Buffer, foundation::permissions::<lambda\\(\\)> >::recombine\\("
  "candidate expects 2 arguments, 1 provided")

# The headline: two splits of two same-typed regions, and a tuple that
# takes one shard from each.  The shard's own type carries the name of
# the split, so the mixed tuple is not the one the receipt goes with.
fixy_neg_compile_test(neg_disjoint_receipt_from_another_split
  "no matching function for call to .fixy::OwnedRegion<int, \\{anonymous\\}::Buffer>::recombine\\("
  "fixy::Disjoint<\\{anonymous\\}::Buffer, foundation::brand::DefaultBrand, fixy::<lambda\\(\\)>, 2>"
  "std::tuple<fixy::OwnedRegion<T, fixy::Slice<Tag, Is, SplitName>, Brand>\\.\\.\\.>&&")

# One split authorizes one rebuild.
fixy_neg_compile_test(neg_disjoint_receipt_copied
  "use of deleted function"
  "Disjoint\\(const fixy::Disjoint<Tag, Brand, SplitName, N>&\\)"
  "a second receipt would let one split authorize two rebuilds")

# The name of a split cannot be spelled, so a second receipt for one
# split cannot be written.
fixy_neg_compile_test(neg_disjoint_split_refuses_spelled_name
  "static assertion failed: mint_split<N>\\(region\\) takes one template argument"
  "Never = \\{\\{anonymous\\}::HeldName\\}")

# ── The row hash reaches every wrapper ────────────────────────────────
#
# foundation/diag/RowHash.h folds the row hash over one shape rather
# than over fifty-two named wrappers, so the property it owes this tree
# is coverage: every wrapper still lands in a slot of its own.  Half the
# wrappers here are aliases for Graded and half are classes over
# graded_facade, and a fold that read the template rather than the shape
# would hand the class half a zero.  The cell lives here rather than in
# test/foundation because foundation must not depend on fixy.
#
# The census reads the classes that its translation unit can see, so it
# includes every public header of include/fixy and include/foundation.
# The list comes from the tree and not from a hand, because a hand-kept
# list did not reach 16 headers of the session layer, and the census never
# saw the decorators, doors and queries that they declare.  The build does
# the glob again, so a header that a later commit adds joins the census,
# and a carrier in it with no row fails the census.  A superseded _Name.h
# header is not public.
fixy_test(test_row_hash_wrappers)
file(GLOB_RECURSE _census_public_headers CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/include/fixy/*.h" "${CMAKE_SOURCE_DIR}/include/foundation/*.h")
list(FILTER _census_public_headers EXCLUDE REGEX "(^|/)_[^/]*\\.h$")
list(SORT _census_public_headers)
list(LENGTH _census_public_headers _census_public_header_count)
if(_census_public_header_count EQUAL 0)
  message(FATAL_ERROR "test_row_hash_wrappers: the glob found no public header under include/fixy or "
                      "include/foundation, so the census would prove nothing")
endif()
set(_census_text "// Written by test/fixy/CMakeLists.txt: each public header of include/fixy and\n")
string(APPEND _census_text "// include/foundation, for the row-hash census of test_row_hash_wrappers.cpp.\n")
string(APPEND _census_text "#pragma once\n")
string(APPEND _census_text "#define CRUCIBLE_CENSUS_PUBLIC_HEADER_COUNT ${_census_public_header_count}\n")
foreach(_census_header IN LISTS _census_public_headers)
  string(APPEND _census_text "#include <${_census_header}>\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/census/census_public_headers.h" CONTENT "${_census_text}" @ONLY)
target_include_directories(test_row_hash_wrappers PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/census")
# A class in a censused namespace with no row fails the census.  The
# fixture declares one and then compiles the census file.
fixy_neg_compile_test(neg_row_hash_census_undisposed_carrier
  "static assertion failed: a carrier in a censused namespace is unproven"
  "the comparison reduces to .\\(([0-9]+) == ([0-9]+)\\).")
target_include_directories(neg_row_hash_census_undisposed_carrier PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/census")

# A row hash is a function of the type alone, so two translation units
# of one build compute one hash for one type.  The second unit declares
# generic closures before its includes, which moves the counter GCC
# prints in every closure name.  The positive control proves the shift
# is real, and every entry must still agree across the two units.
fixy_test(test_row_hash_translation_units
  test_row_hash_translation_units.cpp
  test_row_hash_translation_units_shifted.cpp)

# Every stable id refuses a type whose printed name is not an identity.
# A refinement over a closure predicate takes no row hash, and a value
# that points at a closure's static invoker takes no stable id.
fixy_neg_compile_test(neg_row_hash_of_a_closure_predicate
  "a stable id refuses the type"
  "has no declared name")
fixy_neg_compile_test(neg_stable_id_of_a_closure_invoker_value
  "a stable id refuses the type"
  "has no declared name")
fixy_neg_compile_test(neg_stable_id_of_an_internal_type
  "a stable id refuses the type"
  "has internal linkage")

# A predicate class states its expression as a constraint, so a payload
# the expression cannot evaluate fails the invocability gate at the mint.
fixy_neg_compile_test(neg_refined_mint_predicate_not_comparable
  "constraints not satisfied"
  "required for the satisfaction of .PredicateInvocableOn<Pred, T>. \\[with Pred = .*; T = refined_not_comparable::Uncomparable\\]")

# ── The three axes as one surface ─────────────────────────────────────
#
# A brand names one instance, a share names a live fraction, and an
# effect row names what the caller may do at all.  Each refuses
# something the other two cannot see, and the read is where all three
# have to agree.  Each fixture below names one of the three refusals,
# and each was proven live by a mutant that removes its mechanism.
fixy_test(test_shared_region)

# The composition's own refusal: the share is outstanding, the context
# admits the row, and the region is the wrong instance of the tag.
fixy_neg_compile_test(neg_shared_read_of_another_region
  "no matching function for call to .mint_shared_read\\(\\{anonymous\\}::BgCtx&"
  "deduced conflicting types for parameter .Brand.")

# The erased brand reads nothing.  On the erased brand every region of
# the tag is one type, so the gate asks for a fresh brand.  One fixture
# builds the region through the erased door, the other erases a branded
# region.
fixy_neg_compile_test(neg_shared_read_on_the_erased_brand
  "no matching function for call to .mint_shared_read\\(\\{anonymous\\}::BgCtx&"
  "required for the satisfaction of .IsFreshBrand<Brand>. \\[with Brand = foundation::brand::DefaultBrand\\]")

fixy_neg_compile_test(neg_shared_read_of_an_erased_region
  "no matching function for call to .mint_shared_read\\(\\{anonymous\\}::BgCtx&"
  "required for the satisfaction of .IsFreshBrand<Brand>. \\[with Brand = foundation::brand::DefaultBrand\\]")

# The fractional axis: no guard, no read.
fixy_neg_compile_test(neg_shared_read_without_a_share
  "no matching function for call to .mint_shared_read\\(\\{anonymous\\}::BgCtx&"
  "candidate expects 3 arguments, 2 provided")

# The effect axis, inside the composition rather than beside it.
fixy_neg_compile_test(neg_shared_read_ctx_refuses_the_row
  "required for the satisfaction of .CtxAdmitsPermission<Tag, Ctx>."
  "required for the satisfaction of .CtxFitsSharedRead<T, Tag, Brand, Ctx>."
  "is_subrow_v<.*permission_row_lookup<Tag>::type, typename Ctx::row_type>")

# The share a read stands on has to outlive it.
fixy_neg_compile_test(neg_shared_read_from_a_temporary_share
  "use of deleted function"
  "mint_shared_read\\(const Ctx&, const foundation::permissions::SharedPermissionGuard<Tag, Brand>&&"
  "the guard is released at the end of the full expression")

# The region a read points into has to outlive it.
fixy_neg_compile_test(neg_shared_read_from_a_temporary_region
  "use of deleted function"
  "mint_shared_read\\(const Ctx&, const foundation::permissions::SharedPermissionGuard<Tag, Brand>&, const SharedRegion<T, Tag, Brand>&&"
  "the region is destroyed at the end of the full expression")

# ── The consume tracker ───────────────────────────────────────────────
#
# test_qtt is fixy/Qtt.h with CRUCIBLE_QTT_TRACK_CONSUME off, which is
# every other target here and every shipped build: sizeof(T), a trivial
# move, a trivial destructor.  This one is the same header with the key
# on, and it is the only target in the tree that sets it.
#
# The key belongs to a target and not to a preset because it changes
# sizeof(Qtt<G, T>).  fixy is an INTERFACE library, so there is no
# compiled artifact for this TU to disagree with, and [[gnu::abi_tag]]
# in the header turns any future disagreement into an undefined
# reference rather than a silent layout mismatch.
fixy_test(test_qtt_tracked)
target_compile_definitions(test_qtt_tracked PRIVATE CRUCIBLE_QTT_TRACK_CONSUME=1)

# ── The witnessed deref ───────────────────────────────────────────────
#
# A borrow carries an owner tag and a brand, and leaves WHEN it may be
# read to lexical scope.  A witnessed borrow names an obligation instead
# and exposes no accessor, so every read goes through one gate.  Two
# obligations ship: a protocol position and an effect row.  The fixtures
# below are the four refusals plus the two the mints make, and each was
# proven live by a mutant that removes its mechanism.
fixy_test(test_witnessed)

# TEMPORAL, the refusal the whole kind exists for: the session moved on,
# and the handle it handed back is not the handle the borrow names.
fixy_neg_compile_test(neg_witnessed_deref_at_a_later_protocol_position
  "no matching function for call to .deref\\("
  "required for the satisfaction of .Discharges<Witness, Presented>."
  "Presented = fixy::session::SessionHandle<fixy::session::End,")

# TEMPORAL, presented the wrong kind of thing entirely.
fixy_neg_compile_test(neg_witnessed_deref_without_a_handle
  "no matching function for call to .deref\\("
  "Witness = fixy::witness::AtProtocol<fixy::session::Send<"
  "Presented = foundation::effects::ExecCtx<")

# EFFECT: the region's tag says the read costs IO, and the context's row
# is empty.  The same Subrow test a lend already runs, on the borrow.
fixy_neg_compile_test(neg_witnessed_deref_ctx_refuses_the_row
  "no matching function for call to .deref\\("
  "Witness = fixy::witness::UnderRow<<unnamed>::Spilled>"
  "Presented = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg")

# EFFECT, presented the wrong kind of thing entirely.  With the one
# above, this pair is what shows the two kinds share a gate and not an
# obligation.
fixy_neg_compile_test(neg_witnessed_deref_with_a_handle_for_a_row
  "no matching function for call to .deref\\("
  "Witness = fixy::witness::UnderRow<<unnamed>::Cache"
  "Presented = fixy::session::SessionHandle<fixy::session::Send<")

# The mint refuses a tag that states no obligation, rather than minting
# a witness every context discharges.
fixy_neg_compile_test(neg_witnessed_under_a_tag_with_no_row
  "no matching function for call to .mint_witnessed_under\\("
  "has_permission_row_v<typename UBorrow::source_type>"
  "evaluated to .false.")

# The position a borrow names has to outlive the borrow.
fixy_neg_compile_test(neg_witnessed_at_from_a_temporary_handle
  "use of deleted function"
  "mint_witnessed_at\\(const session::SessionHandle<Proto, Resource, LoopCtx, Policy>&&"
  "the protocol position the borrow would name is already gone")

# ── The component walk, read by four gates ────────────────────────────
#
# A function type and a pointer to member are components of the walk in
# foundation/reflect/TypeComponents.h.  This TU pins what the ReadView
# result gate, the throws search, the delegation query and the payload
# row answer for them.
fixy_test(test_component_walk_gates)

# ── The escape surface, reflected ─────────────────────────────────────
#
# The compiler reads the public surface of each wrapper through std::meta:
# its own members, the members of each public base, and each member of a
# base that a using-declaration makes public.  A member that hands out a
# raw reference or pointer under an unsanctioned name fails to compile
# however it is spelled.  This TU is the assertion.  Each fixture below
# proves one shape of door: a trailing return, a door in a public base, a
# door that a using-declaration exports, a conversion operator, and a
# public data member.
fixy_test(test_escape_reflection)

fixy_neg_compile_test(neg_escape_unsanctioned_member
  "the public surface of .*Leaky holds int. .*Leaky::steal_the_pointer"
  "concept foundation::reflect::RawEscapesSanctioned")
fixy_neg_compile_test(neg_escape_inherited_member
  "the public surface of .*InheritsTheDoor holds int. .*LeakyBase::steal_the_pointer"
  "concept foundation::reflect::RawEscapesSanctioned")
fixy_neg_compile_test(neg_escape_reexported_member
  "the public surface of .*ReExportsTheDoor holds int. .*LeakyBase::steal_the_pointer"
  "concept foundation::reflect::RawEscapesSanctioned")
fixy_neg_compile_test(neg_escape_conversion_operator
  "the public surface of .*ConvertsToPointer holds .*ConvertsToPointer::operator int."
  "concept foundation::reflect::RawEscapesSanctioned")
fixy_neg_compile_test(neg_escape_public_data_member
  "the public surface of .*ExposesStorage holds .*ExposesStorage::count.  A caller takes the address"
  "concept foundation::reflect::RawEscapesSanctioned")
