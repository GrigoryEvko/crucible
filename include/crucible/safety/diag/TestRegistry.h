#pragma once

// ── crucible::safety::diag::TestRegistry (FIXY-G9 + Followup C) ───────
//
// NTTP-keyed test registry for `Tested<TestId>` witness slots.  This
// shipping form carries per-test metadata (name, path, status,
// last_run_epoch) and a per-test status enumeration that
// `is_valid_witness_v<Tested<Id>>` consults.  Specializations land here
// as build artifacts when a registered test is discovered (test-runner
// integration is a follow-on; the schema + four canonical entries are
// what ship today).
//
// Per Followup C: `is_valid_witness_v` reads Active iff the test is
// neither stale, expired, nor revoked.  A witness can be revoked
// independently of its underlying test passing — e.g., a Tested<id>
// witness whose ID corresponds to a flaky test gets marked Revoked,
// and consumers demanding the Tested floor reject the witness even
// though the static_assert against the Tested type alone admits it.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   enum class WitnessStatus     — Active / Stale / Expired / Revoked
//
//   template <auto Id>
//   struct TestEntry              — per-test record:
//                                    * name        : string_view
//                                    * path        : string_view
//                                    * status      : WitnessStatus
//                                    * last_run_epoch : uint64_t
//
//   template <auto Id>
//   constexpr bool is_active_test_v
//                                — true iff TestEntry<Id>::status ==
//                                  WitnessStatus::Active.
//
//   namespace id { ... }          — stable u64 hash identifiers for
//                                    canonical worked-example tests.
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
//   InitSafe — every TestEntry field has NSDMI / explicit init.
//   DetSafe  — bit-identical across compiles for a given Id.
//
// ── References ──────────────────────────────────────────────────────
//
//   safety/witness/Witness.h     — Tested<auto TestId> witness type
//   safety/diag/CiRunRegistry.h  — companion CI-run registry
//   safety/witness/IsWitness.h   — is_valid_witness_v lookup

#include <cstdint>
#include <string_view>

namespace crucible::safety::diag {

// ── WitnessStatus — per-test lifecycle ──────────────────────────────
//
// Active   — passes on the current build/run; valid evidence.
// Stale    — passes but hasn't run recently; degraded evidence.
// Expired  — formerly Active, now invalidated (e.g., source changed
//            without re-run).
// Revoked  — manually flagged as untrustworthy (flaky test, false
//            positive); consumers MUST treat as invalid evidence.

enum class WitnessStatus : std::uint8_t {
    Active  = 0,
    Stale   = 1,
    Expired = 2,
    Revoked = 3,
};

[[nodiscard]] constexpr std::string_view witness_status_name(WitnessStatus s) noexcept {
    switch (s) {
        case WitnessStatus::Active:  return "Active";
        case WitnessStatus::Stale:   return "Stale";
        case WitnessStatus::Expired: return "Expired";
        case WitnessStatus::Revoked: return "Revoked";
        default:                     return std::string_view{"<unknown WitnessStatus>"};
    }
}

// ── TestEntry — per-test metadata record ───────────────────────────
//
// Primary template carries Active sentinel defaults so a Tested<Id>
// witness against an unregistered ID degrades to "valid Asserted-tier
// evidence" rather than synthesizing a false-positive Active claim.
// Per Followup C: a specialization (one per canonical worked-example
// test) overrides status / name / path / last_run_epoch.

template <auto Id>
struct TestEntry final {
    static constexpr auto              id_v             = Id;
    static constexpr std::string_view  name             {"<unregistered>"};
    static constexpr std::string_view  path             {""};
    static constexpr WitnessStatus     status           = WitnessStatus::Active;
    static constexpr std::uint64_t     last_run_epoch   = 0;
};

// ── is_active_test_v ───────────────────────────────────────────────
//
// True iff the TestEntry's status is Active.  Stale, Expired, and
// Revoked all return false.  The substrate gate against
// `cg::*_e<Tested<id>>` admission consults this through
// is_valid_witness_v<Tested<Id>>.

template <auto Id>
inline constexpr bool is_active_test_v =
    TestEntry<Id>::status == WitnessStatus::Active;

// Sentinel for "unnamed test" — used when an Asserted-witness binding
// is promoted to Tested without a specific test ID.  Distinct from
// any concretely-numbered test entry.
inline constexpr std::uint64_t UnnamedTestId = 0;

// ═════════════════════════════════════════════════════════════════════
// ── Canonical worked-example test IDs (Followup C minimal entries) ──
// ═════════════════════════════════════════════════════════════════════
//
// Four entries cover the canonical fixy worked-examples shipped today.
// Test IDs are stable u64 hashes derived from the test name.  Adding
// a new test = add one `inline constexpr auto X = ...;` line and one
// `template <> struct TestEntry<id::X>` specialization below.

namespace id {

inline constexpr std::uint64_t fixy_custom_optimizer    = 0xC051'0F71'C012'E901ULL;
inline constexpr std::uint64_t fixy_forge_phase         = 0xF067'E0CA'F034'AAE5ULL;
inline constexpr std::uint64_t fixy_mimic_backend_hook  = 0xA17C'01EE'CBA0'C8F1ULL;
inline constexpr std::uint64_t fixy_cipher_writer       = 0xC1AE'5FC0'1D5E'AB78ULL;

// Sentinel for the Followup C neg fixture — a test ID whose entry
// carries WitnessStatus::Revoked.  Consumers demanding Tested floor
// against this ID see is_valid_witness_v as false.
inline constexpr std::uint64_t fixy_revoked_demo        = 0xBAD0'BAD0'BAD0'BAD0ULL;

}  // namespace id

template <>
struct TestEntry<id::fixy_custom_optimizer> final {
    static constexpr auto              id_v             = id::fixy_custom_optimizer;
    static constexpr std::string_view  name             = "test_fixy_custom_optimizer";
    static constexpr std::string_view  path             =
        "examples/fixy/example_fixy_custom_optimizer.cpp";
    static constexpr WitnessStatus     status           = WitnessStatus::Active;
    static constexpr std::uint64_t     last_run_epoch   = 1;
};

template <>
struct TestEntry<id::fixy_forge_phase> final {
    static constexpr auto              id_v             = id::fixy_forge_phase;
    static constexpr std::string_view  name             = "test_fixy_forge_phase";
    static constexpr std::string_view  path             =
        "examples/fixy/example_fixy_forge_phase.cpp";
    static constexpr WitnessStatus     status           = WitnessStatus::Active;
    static constexpr std::uint64_t     last_run_epoch   = 1;
};

template <>
struct TestEntry<id::fixy_mimic_backend_hook> final {
    static constexpr auto              id_v             = id::fixy_mimic_backend_hook;
    static constexpr std::string_view  name             = "test_fixy_mimic_backend_hook";
    static constexpr std::string_view  path             =
        "examples/fixy/example_fixy_mimic_backend_hook.cpp";
    static constexpr WitnessStatus     status           = WitnessStatus::Active;
    static constexpr std::uint64_t     last_run_epoch   = 1;
};

template <>
struct TestEntry<id::fixy_cipher_writer> final {
    static constexpr auto              id_v             = id::fixy_cipher_writer;
    static constexpr std::string_view  name             = "test_fixy_cipher_writer";
    static constexpr std::string_view  path             =
        "examples/fixy/example_fixy_cipher_writer.cpp";
    static constexpr WitnessStatus     status           = WitnessStatus::Active;
    static constexpr std::uint64_t     last_run_epoch   = 1;
};

// Revoked sentinel — used by the Followup C neg fixture to demonstrate
// the gate.
template <>
struct TestEntry<id::fixy_revoked_demo> final {
    static constexpr auto              id_v             = id::fixy_revoked_demo;
    static constexpr std::string_view  name             = "fixy_revoked_demo (synthetic)";
    static constexpr std::string_view  path             = "<synthetic — registry demo>";
    static constexpr WitnessStatus     status           = WitnessStatus::Revoked;
    static constexpr std::uint64_t     last_run_epoch   = 0;
};

// ── Self-tests ──────────────────────────────────────────────────────

namespace test_registry_self_test {

// Primary template defaults to Active (lattice-safe for unregistered).
static_assert(TestEntry<UnnamedTestId>::status == WitnessStatus::Active);
static_assert(is_active_test_v<UnnamedTestId>);

// Canonical specializations are Active.
static_assert(is_active_test_v<id::fixy_custom_optimizer>);
static_assert(is_active_test_v<id::fixy_forge_phase>);
static_assert(is_active_test_v<id::fixy_mimic_backend_hook>);
static_assert(is_active_test_v<id::fixy_cipher_writer>);

// Revoked sentinel is NOT active.
static_assert(TestEntry<id::fixy_revoked_demo>::status == WitnessStatus::Revoked);
static_assert(!is_active_test_v<id::fixy_revoked_demo>);

// Status names are populated.
static_assert(witness_status_name(WitnessStatus::Active)  == "Active");
static_assert(witness_status_name(WitnessStatus::Revoked) == "Revoked");

}  // namespace test_registry_self_test

}  // namespace crucible::safety::diag
