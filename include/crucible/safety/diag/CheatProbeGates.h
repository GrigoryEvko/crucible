#pragma once

// ═════════════════════════════════════════════════════════════════════
// safety/diag/CheatProbeGates.h — concept_gate specializations for
// the FOUND-D wrapper-detection traits (FOUND-E17).
// ═════════════════════════════════════════════════════════════════════
//
// Wires CheatProbe.h's `concept_gate<Category>` extension point to
// the corresponding `is_*_v<T>` wrapper-detection trait for each
// shipped wrapper.  Once wired, every `cheat_probe_type<Cheat,
// Category::X>` instantiation enforces that the X-axis detector
// continues to reject Cheat — a regression that weakens the detector
// fires the static_assert at build time.
//
// ── Coverage ────────────────────────────────────────────────────────
//
// One specialization per D-series detector currently shipped.
// Adding a new detector requires adding one specialization here and
// at least one cheat probe in test/test_cheat_probe_wrappers.cpp.
//
//   FOUND-D03  is_owned_region_v          ⇄ (no Category yet)
//   FOUND-D04  is_permission_v            ⇄ (no Category yet)
//   FOUND-D21  is_numerical_tier_v        ⇄ Category::NumericalTierMismatch
//   FOUND-D22  is_consistency_v           ⇄ Category::ConsistencyMismatch
//   FOUND-D23  is_opaque_lifetime_v       ⇄ Category::LifetimeViolation
//   FOUND-D24  is_det_safe_v              ⇄ Category::DetSafeLeak
//   FOUND-D30  is_cipher_tier_v           ⇄ Category::CipherTierViolation
//   FOUND-D30  is_residency_heat_v        ⇄ Category::ResidencyHeatViolation
//   FOUND-D30  is_vendor_v                ⇄ Category::VendorBackendMismatch
//   FOUND-D30  is_crash_v                 ⇄ Category::CrashClassMismatch
//   FOUND-D30  is_budgeted_v              ⇄ Category::BudgetExceeded
//   FOUND-D30  is_epoch_versioned_v       ⇄ Category::EpochMismatch
//   FOUND-D30  is_numa_placement_v        ⇄ Category::NumaPlacementMismatch
//   FOUND-D30  is_recipe_spec_v           ⇄ Category::RecipeSpecMismatch
//
// ── Why a separate header ───────────────────────────────────────────
//
// The wrapper detector headers must NOT depend on Category /
// concept_gate (they are read by the dispatcher infrastructure
// which lives below diagnostics in the layering).  This header
// pulls both into one place and inverts the dependency cleanly.
//
// ── Why the gate `admits_type` returns POSITIVE detection ───────────
//
// The CheatProbe harness asserts `!gate_defined || !admits`.  The
// "admits" predicate must therefore answer "would the X-axis trait
// CLAIM this T is one of mine?"  For wrapper detection that maps
// directly to `is_X_v<T>`: a cheat is a type that LOOKS like an X
// but is NOT (lookalike struct, sibling wrapper, etc.) — the
// detector is supposed to reject it (return false), and the cheat
// probe asserts it does.

#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/CheatProbe.h>

#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsRecipeSpec.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsVendor.h>

namespace crucible::safety::diag {

// ── FOUND-D21 NumericalTier ────────────────────────────────────────
template <>
struct concept_gate<Category::NumericalTierMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_numerical_tier_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D22 Consistency ──────────────────────────────────────────
template <>
struct concept_gate<Category::ConsistencyMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_consistency_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D23 OpaqueLifetime ──────────────────────────────────────
template <>
struct concept_gate<Category::LifetimeViolation> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_opaque_lifetime_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D24 DetSafe ──────────────────────────────────────────────
template <>
struct concept_gate<Category::DetSafeLeak> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_det_safe_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/CipherTier ──────────────────────────────────────────
template <>
struct concept_gate<Category::CipherTierViolation> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_cipher_tier_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/ResidencyHeat ──────────────────────────────────────
template <>
struct concept_gate<Category::ResidencyHeatViolation> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_residency_heat_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/Vendor ──────────────────────────────────────────────
template <>
struct concept_gate<Category::VendorBackendMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_vendor_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/Crash ──────────────────────────────────────────────
template <>
struct concept_gate<Category::CrashClassMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_crash_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/Budgeted ──────────────────────────────────────────
template <>
struct concept_gate<Category::BudgetExceeded> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_budgeted_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/EpochVersioned ─────────────────────────────────────
template <>
struct concept_gate<Category::EpochMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_epoch_versioned_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/NumaPlacement ─────────────────────────────────────
template <>
struct concept_gate<Category::NumaPlacementMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_numa_placement_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── FOUND-D30/RecipeSpec ─────────────────────────────────────────
template <>
struct concept_gate<Category::RecipeSpecMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type =
        ::crucible::safety::extract::is_recipe_spec_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

// ── Compile-time inventory of gates that are now defined ────────────
//
// The cheat-probe test TU consumes this to enumerate the categories
// it must register cheats for.  Adding a new gate above auto-extends
// this list (no manual maintenance) — but the test TU's cheat
// catalog must add a corresponding entry or the per-category
// coverage assertion in the test fires.

namespace detail {

// Returns true iff every category listed below has a gate defined.
// A failing static_assert here means the gate specialization above
// was deleted but not replaced — restore or delete the corresponding
// test entry.
[[nodiscard]] consteval bool all_d_series_gates_defined() noexcept {
    return is_gate_defined_v<Category::NumericalTierMismatch>
        && is_gate_defined_v<Category::ConsistencyMismatch>
        && is_gate_defined_v<Category::LifetimeViolation>
        && is_gate_defined_v<Category::DetSafeLeak>
        && is_gate_defined_v<Category::CipherTierViolation>
        && is_gate_defined_v<Category::ResidencyHeatViolation>
        && is_gate_defined_v<Category::VendorBackendMismatch>
        && is_gate_defined_v<Category::CrashClassMismatch>
        && is_gate_defined_v<Category::BudgetExceeded>
        && is_gate_defined_v<Category::EpochMismatch>
        && is_gate_defined_v<Category::NumaPlacementMismatch>
        && is_gate_defined_v<Category::RecipeSpecMismatch>;
}

static_assert(all_d_series_gates_defined(),
    "FOUND-E17: at least one D-series concept_gate specialization is "
    "missing from CheatProbeGates.h.  The 12 D-series detectors "
    "(D21-D24, D30 batch) must each have a concept_gate<Category::X> "
    "specialization wiring admits_type to the corresponding "
    "is_X_v<T> trait.  Restore the missing specialization OR delete "
    "the gate-defined check above if the wrapper is being retired.");

}  // namespace detail

}  // namespace crucible::safety::diag
