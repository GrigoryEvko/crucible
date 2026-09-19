#pragma once

// The wrapper detectors sit below diagnostics in the layering, so their
// own headers cannot name Category or the gate. Wiring them here inverts
// that dependency and keeps every specialization in one place.
//
// Each gate admits exactly what its detector claims. A probe asserts the
// gate rejects its cheat, and a cheat is a lookalike that the detector
// must not claim, so forwarding the positive detection is the right
// polarity.

#include <crucible/safety/_Diagnostic.h>
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

template <>
struct concept_gate<Category::NumericalTierMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_numerical_tier_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::ConsistencyMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_consistency_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::LifetimeViolation> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_opaque_lifetime_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::DetSafeLeak> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_det_safe_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::CipherTierViolation> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_cipher_tier_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::ResidencyHeatViolation> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_residency_heat_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::VendorBackendMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_vendor_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::CrashClassMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_crash_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::BudgetExceeded> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_budgeted_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::EpochMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_epoch_versioned_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::NumaPlacementMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_numa_placement_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <>
struct concept_gate<Category::RecipeSpecMismatch> {
    static constexpr bool defined = true;
    template <typename T>
    static constexpr bool admits_type = ::crucible::safety::extract::is_recipe_spec_v<T>;
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

namespace detail {

[[nodiscard]] consteval bool all_wrapper_axis_gates_defined() noexcept {
    return is_gate_defined_v<Category::NumericalTierMismatch> && is_gate_defined_v<Category::ConsistencyMismatch>
        && is_gate_defined_v<Category::LifetimeViolation> && is_gate_defined_v<Category::DetSafeLeak>
        && is_gate_defined_v<Category::CipherTierViolation> && is_gate_defined_v<Category::ResidencyHeatViolation>
        && is_gate_defined_v<Category::VendorBackendMismatch> && is_gate_defined_v<Category::CrashClassMismatch>
        && is_gate_defined_v<Category::BudgetExceeded> && is_gate_defined_v<Category::EpochMismatch>
        && is_gate_defined_v<Category::NumaPlacementMismatch> && is_gate_defined_v<Category::RecipeSpecMismatch>;
}

static_assert(all_wrapper_axis_gates_defined(),
              "A wrapper-axis category has no concept_gate specialization. Every wrapper detector needs one "
              "specialization wiring admits_type to its own trait. Restore the missing specialization, or drop the "
              "category from the check above if the wrapper is retired.");

}  // namespace detail

}  // namespace crucible::safety::diag
