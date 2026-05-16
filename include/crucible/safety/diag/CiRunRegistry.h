#pragma once

// ── crucible::safety::diag::CiRunRegistry (FIXY-G9 + Followup C) ─────
//
// NTTP-keyed CI-run registry for `CrossValidated<CiRunId>` witness
// slots.  Cross-vendor numerics CI / cross-platform validation runs
// register their identity here; downstream witness consumers
// reference run IDs as NTTPs.  This shipping form carries per-run
// metadata (name, ci_run_url, status, expiry_epoch).
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <auto Id>
//   struct CiRunEntry             — per-CI-run record:
//                                    * name         : string_view
//                                    * ci_run_url   : string_view
//                                    * status       : WitnessStatus
//                                    * expiry_epoch : uint64_t
//
//   template <auto Id>
//   constexpr bool is_valid_ci_run_v
//                                — true iff CiRunEntry<Id>::status is
//                                  Active (mirrors TestRegistry).
//
// ── Discipline ──────────────────────────────────────────────────────
//
// CiRunId is an opaque NTTP — typically a u64 hash of (CI run name,
// fleet target, recipe set, timestamp_bucket).  See TestRegistry.h
// for the same general discipline.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — NTTP-templated.
//   DetSafe  — bit-identical across compiles.
//
// ── References ──────────────────────────────────────────────────────
//
//   safety/witness/Witness.h     — CrossValidated<auto Id> witness type
//   safety/diag/TestRegistry.h   — companion test-entry registry
//   safety/witness/IsWitness.h   — is_valid_witness_v lookup

#include <crucible/safety/diag/TestRegistry.h>

#include <cstdint>
#include <string_view>

namespace crucible::safety::diag {

// ── CiRunEntry — per-CI-run metadata record ────────────────────────
//
// Primary template carries Active sentinel defaults so a
// CrossValidated<Id> witness against an unregistered ID degrades to
// "valid Tested-tier evidence" without synthesizing a false-positive
// Active claim at the higher tier.

template <auto Id>
struct CiRunEntry final {
    static constexpr auto             id_v           = Id;
    static constexpr std::string_view name           {"<unregistered>"};
    static constexpr std::string_view ci_run_url     {""};
    static constexpr WitnessStatus    status         = WitnessStatus::Active;
    static constexpr std::uint64_t    expiry_epoch   = 0;
};

template <auto Id>
inline constexpr bool is_valid_ci_run_v =
    CiRunEntry<Id>::status == WitnessStatus::Active;

// Sentinel for "unnamed CI run" — used when an internal promotion
// to CrossValidated does not pin a specific run.
inline constexpr std::uint64_t UnnamedCiRunId = 0;

// ═════════════════════════════════════════════════════════════════════
// ── Canonical CI run IDs (Followup C minimal entries) ──────────────
// ═════════════════════════════════════════════════════════════════════

namespace ci_id {

inline constexpr std::uint64_t fixy_cross_vendor_smoke    = 0xCFE0'55AA'5750'C001ULL;
inline constexpr std::uint64_t fixy_aarch64_x86_pairwise  = 0xA4'1A'AC'EA'5750'A887ULL;

// Sentinel for Followup C neg fixture — Revoked status.
inline constexpr std::uint64_t fixy_revoked_ci_demo       = 0xBAD0'C1BAD0'C1BADULL;

}  // namespace ci_id

template <>
struct CiRunEntry<ci_id::fixy_cross_vendor_smoke> final {
    static constexpr auto             id_v           = ci_id::fixy_cross_vendor_smoke;
    static constexpr std::string_view name           = "fixy_cross_vendor_smoke";
    static constexpr std::string_view ci_run_url     =
        "internal://ci/fixy_cross_vendor_smoke/latest";
    static constexpr WitnessStatus    status         = WitnessStatus::Active;
    static constexpr std::uint64_t    expiry_epoch   = 0;
};

template <>
struct CiRunEntry<ci_id::fixy_aarch64_x86_pairwise> final {
    static constexpr auto             id_v           = ci_id::fixy_aarch64_x86_pairwise;
    static constexpr std::string_view name           = "fixy_aarch64_x86_pairwise";
    static constexpr std::string_view ci_run_url     =
        "internal://ci/fixy_aarch64_x86_pairwise/latest";
    static constexpr WitnessStatus    status         = WitnessStatus::Active;
    static constexpr std::uint64_t    expiry_epoch   = 0;
};

template <>
struct CiRunEntry<ci_id::fixy_revoked_ci_demo> final {
    static constexpr auto             id_v           = ci_id::fixy_revoked_ci_demo;
    static constexpr std::string_view name           = "fixy_revoked_ci_demo (synthetic)";
    static constexpr std::string_view ci_run_url     = "<synthetic>";
    static constexpr WitnessStatus    status         = WitnessStatus::Revoked;
    static constexpr std::uint64_t    expiry_epoch   = 0;
};

// ── Self-tests ──────────────────────────────────────────────────────

namespace ci_run_registry_self_test {

static_assert(CiRunEntry<UnnamedCiRunId>::status == WitnessStatus::Active);
static_assert(is_valid_ci_run_v<UnnamedCiRunId>);

static_assert(is_valid_ci_run_v<ci_id::fixy_cross_vendor_smoke>);
static_assert(is_valid_ci_run_v<ci_id::fixy_aarch64_x86_pairwise>);

static_assert(CiRunEntry<ci_id::fixy_revoked_ci_demo>::status == WitnessStatus::Revoked);
static_assert(!is_valid_ci_run_v<ci_id::fixy_revoked_ci_demo>);

}  // namespace ci_run_registry_self_test

}  // namespace crucible::safety::diag
