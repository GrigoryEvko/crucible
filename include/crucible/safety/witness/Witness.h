#pragma once

// ── crucible::safety::witness — proof-relevance metasystem (FIXY-G9) ──
//
// The four-tier witness hierarchy that gives every grade a proof-
// relevance marker.  Before G9, every grant was implicitly
// `Asserted<UnnamedRationale>` — the substrate trusted the binding
// without recording evidence.  After G9, downstream consumers (Cipher
// hot-tier promotion, Federation peering, AdaptiveScheduler) can
// demand a witness FLOOR per axis, and the type system refuses
// admission for bindings whose witness tier is below the floor.
//
// ── The four witness types ──────────────────────────────────────────
//
//   Asserted<Rationale>       — binding asserts the property.  The
//                               Rationale phantom tag is grep-discoverable
//                               (UnnamedRationale is the default; named
//                               tags like `audit::ReviewedByOps` carry
//                               provenance for human review).
//
//   Tested<auto TestId>       — a registered test exercises the
//                               property.  TestId NTTP references
//                               safety/diag/TestRegistry.h entries.
//
//   CrossValidated<auto Id>   — a registered CI run (cross-vendor /
//                               cross-platform / pairwise-oracle)
//                               validates the property.  Id references
//                               safety/diag/CiRunRegistry.h entries.
//
//   FormallyVerified<P>       — a proof-certificate witnesses the
//                               property.  P is a phantom tag for the
//                               proof-cert reference (small-SMT slot,
//                               currently unused — reserved).
//
// ── The witness lattice ─────────────────────────────────────────────
//
// witness_leq_v<W1, W2> encodes Asserted ⊑ Tested ⊑ CrossValidated ⊑
// FormallyVerified.  Each tier admits every weaker tier as a subtype
// at the lattice level; consumer-side `WitnessAtLeast<W, Min>` gates
// reject anything strictly below Min.
//
// Reflexive on each tier; the relation is a total order on the
// four-tier ladder, so the implementation is a four-arm comparison
// against `witness_tier_v<W>`.
//
// ── PlatformBounded ─────────────────────────────────────────────────
//
// `PlatformBounded<W, Platforms...>` narrows W to specific arches.
// A binding with `PlatformBounded<Tested<id>, X86_64>` carries Tested
// witness ONLY on x86-64 targets; on an AArch64 consumer the witness
// degrades to its weaker base (Asserted) before the lattice compare,
// and a Tested-floor gate rejects the call.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe   — every witness type is an empty struct (sizeof == 1);
//                no member to leave uninitialized.
//   TypeSafe   — witness types are non-convertible across tiers; tier
//                discrimination is via stable witness_tier_v variable
//                template, not implicit conversion.
//   NullSafe   — zero state; no pointers.
//   MemSafe    — empty types; EBO-collapses to 0 bytes when used as
//                [[no_unique_address]] member of a grade carrier.
//   BorrowSafe — pure compile-time metadata; no aliasing concern.
//   ThreadSafe — all material is compile-time; no runtime state.
//   LeakSafe   — empty types; no resources.
//   DetSafe    — witness_leq_v is bit-identical across compiles.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  Witness types live entirely at the type level; the only
// runtime artifact is the per-axis byte the wire format carries
// (FIXY-G6 extension), and that byte is part of the serialized grade
// — never read on the hot path.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G9 witness metadata
//   safety/witness/IsWitness.h            — concept gates
//   safety/witness/Platform.h             — arch phantom tags
//   safety/diag/TestRegistry.h            — TestId NTTP backing
//   safety/diag/CiRunRegistry.h           — CiRunId NTTP backing
//   fixy/Witness.h                        — fixy-side consumer gates
//   fixy/WireGrade.h                      — wire format extension

#include <crucible/Platform.h>
#include <crucible/safety/witness/Platform.h>

#include <cstdint>
#include <type_traits>

namespace crucible::safety::witness {

// ═════════════════════════════════════════════════════════════════════
// ── UnnamedRationale — default phantom tag ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The default rationale carried by Asserted<>.  A binding without an
// explicit rationale ships with `Asserted<UnnamedRationale>`.  Named
// rationale tags (`audit::ReviewedByOps`, `pr_1234_review`, ...) carry
// the provenance literal so review can grep witness usage.

struct UnnamedRationale final {};

// ═════════════════════════════════════════════════════════════════════
// ── The four witness types ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each is an empty struct (sizeof == 1, EBO-collapsible to 0 bytes).
// The template parameter is a phantom tag carrying the proof-relevance
// identity (rationale, test ID, CI run ID, or proof certificate).

template <typename Rationale = UnnamedRationale>
struct Asserted final {
    using rationale_type = Rationale;
};

template <auto TestId>
struct Tested final {
    static constexpr auto test_id_v = TestId;
};

template <auto CiRunId>
struct CrossValidated final {
    static constexpr auto ci_run_id_v = CiRunId;
};

template <typename ProofCert>
struct FormallyVerified final {
    using proof_cert_type = ProofCert;
};

static_assert(sizeof(Asserted<>) == 1);
static_assert(sizeof(Tested<0>) == 1);
static_assert(sizeof(CrossValidated<0>) == 1);
static_assert(sizeof(FormallyVerified<UnnamedRationale>) == 1);

// ═════════════════════════════════════════════════════════════════════
// ── DefaultWitness — backwards-compat default ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Bare grants inherit `using witness_t = DefaultWitness;`.  Production
// code that wants stronger evidence uses the `cg::*_e<W>` evidenced
// variants (Grant.h's CRUCIBLE_FIXY_EVIDENCED_VARIANT macro).

using DefaultWitness = Asserted<UnnamedRationale>;

// ═════════════════════════════════════════════════════════════════════
// ── witness_tier_v — total-order tier comparison ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Asserted ⊑ Tested ⊑ CrossValidated ⊑ FormallyVerified.
// PlatformBounded<W, Platforms...> resolves to W's tier when the
// current arch is in Platforms..., else falls back to the floor
// (Asserted) — see specialization below.

namespace detail {

template <typename W>
inline constexpr std::uint8_t witness_tier_v_impl = 0;

template <typename R>
inline constexpr std::uint8_t witness_tier_v_impl<Asserted<R>> = 1;

template <auto Id>
inline constexpr std::uint8_t witness_tier_v_impl<Tested<Id>> = 2;

template <auto Id>
inline constexpr std::uint8_t witness_tier_v_impl<CrossValidated<Id>> = 3;

template <typename P>
inline constexpr std::uint8_t witness_tier_v_impl<FormallyVerified<P>> = 4;

}  // namespace detail

// ── PlatformBounded ─────────────────────────────────────────────────
//
// Narrows W to specific archs.  Forward-declared HERE so the tier
// specialization below can reference it.  Full body below.

template <typename W, typename... Platforms>
struct PlatformBounded final {
    using base_witness_type = W;
};

// ── platform_bounded_active_v ───────────────────────────────────────
//
// True iff current_arch_tag is one of Platforms... .

namespace detail {

template <typename... Platforms>
inline constexpr bool platform_bounded_active_v =
    (std::is_same_v<arch::current_arch_tag, Platforms> || ...);

// Out-of-band tier for inactive PlatformBounded — falls back to the
// Asserted floor (tier 1).  An inactive PlatformBounded with tier 1
// cannot satisfy a Tested-or-higher floor gate.
template <typename W, typename... Platforms>
inline constexpr std::uint8_t witness_tier_v_impl<PlatformBounded<W, Platforms...>> =
    platform_bounded_active_v<Platforms...>
        ? witness_tier_v_impl<W>
        : std::uint8_t{1};

}  // namespace detail

template <typename W>
inline constexpr std::uint8_t witness_tier_v = detail::witness_tier_v_impl<W>;

// ═════════════════════════════════════════════════════════════════════
// ── witness_leq_v — lattice ≼ relation ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// W1 ⊑ W2 iff witness_tier_v<W1> ≤ witness_tier_v<W2>.

template <typename W1, typename W2>
inline constexpr bool witness_leq_v =
    witness_tier_v<W1> <= witness_tier_v<W2>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

// Phantom tags for testing.
struct test_rationale {};
struct test_ci_run {};
struct test_proof_cert {};

// Tier table.
static_assert(witness_tier_v<Asserted<UnnamedRationale>> == 1);
static_assert(witness_tier_v<Asserted<test_rationale>> == 1);
static_assert(witness_tier_v<Tested<42>> == 2);
static_assert(witness_tier_v<CrossValidated<7>> == 3);
static_assert(witness_tier_v<FormallyVerified<test_proof_cert>> == 4);

// Reflexive.
static_assert(witness_leq_v<Asserted<>, Asserted<>>);
static_assert(witness_leq_v<Tested<0>, Tested<0>>);
static_assert(witness_leq_v<CrossValidated<0>, CrossValidated<0>>);
static_assert(witness_leq_v<FormallyVerified<int>, FormallyVerified<int>>);

// Hierarchy.
static_assert(witness_leq_v<Asserted<>, Tested<0>>);
static_assert(witness_leq_v<Tested<0>, CrossValidated<0>>);
static_assert(witness_leq_v<CrossValidated<0>, FormallyVerified<int>>);
static_assert(witness_leq_v<Asserted<>, FormallyVerified<int>>);

// Anti-hierarchy: stronger NOT ≼ weaker.
static_assert(!witness_leq_v<Tested<0>, Asserted<>>);
static_assert(!witness_leq_v<CrossValidated<0>, Tested<0>>);
static_assert(!witness_leq_v<FormallyVerified<int>, CrossValidated<0>>);
static_assert(!witness_leq_v<FormallyVerified<int>, Asserted<>>);

// PlatformBounded — active on current arch.
using AnyArch = arch::current_arch_tag;
static_assert(witness_tier_v<PlatformBounded<Tested<0>, AnyArch>> == 2,
    "PlatformBounded with current arch in pack must report W's tier.");

// PlatformBounded — inactive falls back to Asserted floor.
// We pick a single other arch tag that ISN'T current_arch_tag, so this
// specialization runs the inactive arm.
namespace pb_inactive_arch {
    // Choose an arch that's NOT current.  On x86 → AArch64; on AArch64 → X86_64.
#if defined(__x86_64__)
    using other = arch::AArch64;
#elif defined(__aarch64__)
    using other = arch::X86_64;
#elif defined(__riscv)
    using other = arch::X86_64;
#endif
}
static_assert(witness_tier_v<PlatformBounded<Tested<0>, pb_inactive_arch::other>> == 1,
    "PlatformBounded with current arch NOT in pack must fall back to "
    "Asserted floor (tier 1).");

// DefaultWitness round-trip.
static_assert(std::is_same_v<DefaultWitness, Asserted<UnnamedRationale>>);
static_assert(witness_tier_v<DefaultWitness> == 1);

}  // namespace self_test

}  // namespace crucible::safety::witness
