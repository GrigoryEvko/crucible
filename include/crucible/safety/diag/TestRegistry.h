#pragma once

// ── crucible::safety::diag::TestRegistry (FIXY-G9 scaffold) ───────────
//
// Minimal NTTP-keyed test registry for `Tested<TestId>` witness slots.
// Full registry tooling (test-runner integration, per-test status,
// build-time validation) is a follow-on; this header ships the type
// surface so witness consumers can reference test IDs as NTTPs today.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <auto Id>
//   struct TestEntry {
//       static constexpr auto id_v = Id;
//   };
//
// ── Discipline ──────────────────────────────────────────────────────
//
// TestId is an opaque NTTP — typically a u64 hash of the test's
// stable name + build position.  No collision discipline is enforced
// at the registry layer; consumers compare witness types by
// witness_tier_v (the proof-relevance lattice), not by ID equality.
// An ID collision degrades to "two bindings share Tested provenance",
// which is benign at the lattice level.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — NTTP-templated; Id values are non-convertible across
//              entries.
//   DetSafe  — bit-identical across compiles for a given Id.
//
// ── References ──────────────────────────────────────────────────────
//
//   safety/witness/Witness.h — Tested<auto TestId> witness type
//   safety/diag/CiRunRegistry.h — companion CI-run registry

#include <cstdint>

namespace crucible::safety::diag {

template <auto Id>
struct TestEntry final {
    static constexpr auto id_v = Id;
};

// Sentinel for "unnamed test" — used when an Asserted-witness binding
// is promoted to Tested without a specific test ID.  Distinct from
// any concretely-numbered test entry.
inline constexpr std::uint64_t UnnamedTestId = 0;

}  // namespace crucible::safety::diag
