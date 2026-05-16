#pragma once

// ── crucible::safety::diag::CiRunRegistry (FIXY-G9 scaffold) ─────────
//
// Minimal NTTP-keyed CI-run registry for `CrossValidated<CiRunId>`
// witness slots.  Cross-vendor numerics CI / cross-platform validation
// runs register their identity here; downstream witness consumers
// reference run IDs as NTTPs.  Full registry tooling (per-run status,
// fleet coverage matrix, expiry policy) is a follow-on; this header
// ships the type surface.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <auto Id>
//   struct CiRunEntry {
//       static constexpr auto id_v = Id;
//   };
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

#include <cstdint>

namespace crucible::safety::diag {

template <auto Id>
struct CiRunEntry final {
    static constexpr auto id_v = Id;
};

// Sentinel for "unnamed CI run" — used when an internal promotion
// to CrossValidated does not pin a specific run.
inline constexpr std::uint64_t UnnamedCiRunId = 0;

}  // namespace crucible::safety::diag
