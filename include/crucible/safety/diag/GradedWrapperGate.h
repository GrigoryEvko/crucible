#pragma once
//
// safety/diag/GradedWrapperGate.h — concept_gate specialization
// bridging the FOUND-E05 cheat-probe pattern to the EXISTING
// GradedWrapper concept (algebra/GradedTrait.h).
//
// ═════════════════════════════════════════════════════════════════════
// What this header does
// ═════════════════════════════════════════════════════════════════════
//
// `safety/diag/CheatProbe.h` ships the `concept_gate<Category>`
// extension point.  This header SPECIALIZES that gate for
// `Category::GradedWrapperViolation`, wiring it to the production
// concept `crucible::algebra::GradedWrapper<T>` which has been live
// since the substrate's Round-1 audit.
//
// After including this header, every `cheat_probe_type<T,
// Category::GradedWrapperViolation>` instantiation ENFORCES the
// existing concept's strictness — the 18 cheats already shipped at
// `test/test_concept_cheat_probe.cpp` become per-concept-rejection
// regression locks.
//
// ═════════════════════════════════════════════════════════════════════
// Why a separate header
// ═════════════════════════════════════════════════════════════════════
//
// CheatProbe.h is deliberately lean — it doesn't include
// algebra/GradedTrait.h (a heavy header).  Projects that don't need
// GradedWrapper integration (most diagnostic surfaces don't) skip
// this header.  Projects that DO want the integration include this
// header BEFORE any cheat probe instantiation against
// Category::GradedWrapperViolation.
//
// ═════════════════════════════════════════════════════════════════════
// Header-include ordering discipline
// ═════════════════════════════════════════════════════════════════════
//
// This specialization MUST be visible BEFORE any TU instantiates
// `concept_gate<Category::GradedWrapperViolation>`.  In practice:
//
//   * Include this header from the same TU as your cheat probe
//     instantiations against GradedWrapperViolation, BEFORE the
//     probe `using` declarations.
//   * Or include it from the project's diagnostic umbrella header
//     (e.g., `safety/diag/Diag.h`) so every consumer sees it.
//
// Not following the ordering: the `concept_gate<...
// GradedWrapperViolation>` primary template gets instantiated first
// (with `defined = false`), and the specialization in THIS header
// later becomes ill-formed (specialization-after-instantiation).
// CheatProbe.h's eager-instantiation discipline (the shape check
// only touches Category::EffectRowMismatch) does NOT trigger this
// issue for GradedWrapperViolation specifically.

#include <crucible/safety/diag/CheatProbe.h>
#include <crucible/algebra/GradedTrait.h>          // GradedWrapper concept

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// Specialization — Category::GradedWrapperViolation gate
// ═════════════════════════════════════════════════════════════════════
//
// `defined = true` activates every `cheat_probe_type<T,
// Category::GradedWrapperViolation>` registered downstream.
//
// `admits_type<T>` forwards to `crucible::algebra::GradedWrapper<T>`
// — the existing concept's full strictness applies.
//
// `admits_function<F>` is unused for this gate — GradedWrapper is
// a TYPE concept, not a function-pointer concept.  Returns false
// uniformly (correctly rejects every function pointer; no cheat
// probe against a function pointer can register against this
// Category and expect non-trivial behavior).

template <>
struct concept_gate<Category::GradedWrapperViolation> {
    static constexpr bool defined = true;

    template <typename T>
    static constexpr bool admits_type =
        ::crucible::algebra::GradedWrapper<T>;

    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ═════════════════════════════════════════════════════════════════════
// Self-test — locks in the specialization
// ═════════════════════════════════════════════════════════════════════
//
// is_gate_defined_v reads `defined` — confirms the specialization
// bound.  Foundation invariant: this header IS load-bearing.

static_assert(is_gate_defined_v<Category::GradedWrapperViolation>,
    "GradedWrapperGate.h: specialization must mark gate as defined");

}  // namespace crucible::safety::diag
