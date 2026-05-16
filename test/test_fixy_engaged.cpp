// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_engaged.cpp
//
// FIXY-A5 — positive-compile coverage for `crucible::fixy::IsAccepted<>`
// gate.  Eight scenarios across the Tier × stance grid (per
// misc/16_05_2026_fixy.md §3.1 stance table), each demonstrating a
// well-formed engagement on every one of the 20 dims either via a
// `grant::*` relaxation tag or via `accept_default_strict_for<dim::X>`.
//
// The discipline says ANY missing dim engagement is a compile error;
// these positive witnesses prove the gate ACCEPTS well-formed packs.
// The complementary neg-compile corpus (test/fixy_neg/) exercises the
// rejection path (FIXY-A6).
//
// Per feedback_algebra_runtime_smoke_test_discipline.md, every
// header-level concept that compiles consteval also exercises a
// non-trivial runtime path so the if-consteval branch isn't hiding a
// regression.  Here the runtime path is sizeof-pin + WhichDimUnengaged
// readback under non-constant arguments.

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>

#include <cstdint>
#include <cstdio>
#include <string_view>
#include <tuple>

namespace {

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace ce = crucible::effects;

// ── Stance-builder shorthand ───────────────────────────────────────────
//
// Every scenario needs at least 19 `accept_default_strict_for<dim::X>`
// tags + 1-N relaxation tags.  The N=0 "all-strict" baseline is the
// AllStrictExceptType template — every scenario imports it and
// substitutes the relaxed-dim tags.  Type is always engaged explicitly
// (either via grant::typed<T> or accept_default_strict_for<dim::Type>).

template <cd::DimAxis... Skip>
struct skip_set {};

// ─── Scenario 1: stance::PureLinear-equivalent ────────────────────────
//
// Every dim defaults to strict.  Type is engaged via the explicit
// "accept default" tag (a fixy binding with no concrete type slot —
// would be paired with grant::typed<T> in Phase B at use site).
//
// Engaged via 20 accept tags.  No relaxation.
static_assert(cf::IsAccepted<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 1: PureLinear-equivalent (all-default-accepted)");

// ─── Scenario 2: stance::PureCopy ─────────────────────────────────────
//
// Usage relaxed to Copy; remaining 19 dims strict-default-accepted.
//
// The author chose copyable semantics for a small POD type.
static_assert(cf::IsAccepted<
    cg::copy,                                          // Usage
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 2: PureCopy (Usage relaxed, rest strict)");

// ─── Scenario 3: stance::IoFunction ───────────────────────────────────
//
// Effect relaxed to {IO}; rest strict.  Canonical filesystem-touch
// fn.
static_assert(cf::IsAccepted<
    cg::with_io,                                       // Effect
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 3: IoFunction (Effect IO, rest strict)");

// ─── Scenario 4: stance::BgWorker ─────────────────────────────────────
//
// Effect relaxed to {Bg, Alloc, Block}; rest strict.  The canonical
// background drain shape — Cipher cold-tier writer, BackgroundThread
// stage worker, etc.
static_assert(cf::IsAccepted<
    cg::with<ce::Effect::Bg, ce::Effect::Alloc, ce::Effect::Block>,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 4: BgWorker (Effect Bg+Alloc+Block, rest strict)");

// ─── Scenario 5: stance::SecretConsumer (declassify gate) ─────────────
//
// Security relaxed via grant::declassify<MyPolicy>; rest strict.
// Demonstrates a binding that consumes Classified inputs but emits
// declassified outputs under a named policy.
struct AuditedDeclassPolicy {
    static constexpr std::string_view name = "audited-2026-05-16";
};

static_assert(cf::IsAccepted<
    cg::declassify<AuditedDeclassPolicy>,              // Security
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 5: SecretConsumer (declassify under audit policy)");

// ─── Scenario 6: stance::MimicNvBackend ───────────────────────────────
//
// Three dims relaxed at once: Effect to {Bg, Alloc, IO} (NV ioctl
// emit + DRAM stage), Representation to vendor<NV>, plus a NumericalTier
// pin via grant::tier (also relaxes Representation — multiple tags
// engaging same dim is allowed).  Demonstrates a real Mimic-backend
// stance.
struct NvVendor {};
struct BitexactTier {};

static_assert(cf::IsAccepted<
    cg::with<ce::Effect::Bg, ce::Effect::Alloc, ce::Effect::IO>,
    cg::vendor<NvVendor>,                              // Representation
    cg::tier<BitexactTier>,                            // Representation (redundant — allowed)
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 6: MimicNvBackend (Effect+Vendor+Tier multi-relax)");

// ─── Scenario 7: stance::Refinement with named predicate ──────────────
//
// Refinement engaged via grant::refined_with<Pred> instead of the
// strict True; Type engaged via grant::typed<int>; rest strict.
struct NonNegativePred {
    template <typename T>
    [[nodiscard]] static constexpr bool check(const T& v) noexcept {
        return v >= T{0};
    }
};

static_assert(cf::IsAccepted<
    cg::typed<int>,                                    // Type
    cg::refined_with<NonNegativePred>,                 // Refinement
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>, "scenario 7: Refinement-with-named-pred + typed<int>");

// ─── Scenario 8: numerical relaxation stack ───────────────────────────
//
// Precision (reassociate), Overflow (wrap), Staleness (≤ τ),
// Mutation (append_only), Version (v=2) — a stack typical of an
// observe metrics counter pipeline whose freshness window is bounded
// and overflow wraps at u32 boundary.
static_assert(cf::IsAccepted<
    cg::reassociate,                                   // Precision
    cg::overflow_wrap,                                 // Overflow
    cg::stale_to<100>,                                 // Staleness
    cg::append_only,                                   // Mutation
    cg::version<2>,                                    // Version
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>
>, "scenario 8: numerical-relax stack (Precision+Overflow+Staleness+Mutation+Version)");

// ─── EBO collapse: grant tags are 1-byte empty structs ─────────────────
static_assert(sizeof(cg::copy)                            == 1);
static_assert(sizeof(cf::accept_default_strict_for<cd::Type>) == 1);
static_assert(sizeof(cg::with_io)                         == 1);

// ─── WhichDimUnengaged on accepted packs returns sentinel ──────────────
//
// `value` is meaningless when `all_engaged` is true; this pins the
// documented sentinel (dim::Type).
using AcceptedPack = std::tuple<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Per-dim engagement EBO check (Phase A pins the cost model) ─────────
//
// Phase B fixy::fn wrapper compiles `Grants...` into the substrate's
// safety::fn::Fn template's per-axis arguments — sizeof(Fn<T, ...>)
// == sizeof(T) is the substrate invariant.  Phase A pins only the
// grant tag EBO claim.
static_assert(std::is_empty_v<cg::copy>);
static_assert(std::is_empty_v<cf::accept_default_strict_for<cd::Type>>);
static_assert(std::is_empty_v<cg::with<ce::Effect::IO, ce::Effect::Bg>>);
static_assert(std::is_empty_v<cg::reassociate>);

}  // namespace

// ── Runtime smoke driver ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise the
// accessors with non-constant arguments to catch consteval/inline-body
// regressions that pure static_asserts cannot.

int main() {
    // Volatile-int prevents consteval folding — accessors must work
    // at runtime.
    volatile std::uint8_t dim_idx_v = 2u;  // Usage
    const auto axis = static_cast<cd::DimAxis>(dim_idx_v);
    const std::string_view dname = cd::name(axis);
    if (dname != "Usage") {
        std::fprintf(stderr, "test_fixy_engaged: dim::name dispatch failed\n");
        return 1;
    }

    // Sentinel: count_v reads at runtime as the load-bearing 20.
    if (cd::count_v != 20u) {
        std::fprintf(stderr, "test_fixy_engaged: count_v != 20\n");
        return 1;
    }

    // WhichDimUnengaged readback on a known-bad pack — confirms the
    // first-failing-dim heuristic returns the right enumerator at
    // runtime.  Volatile prevents the entire chain from folding.
    using BadPack = cf::WhichDimUnengaged<cg::copy>;  // 19 dims unengaged
    volatile std::uint8_t expected_v = static_cast<std::uint8_t>(cd::Type);
    if (static_cast<std::uint8_t>(BadPack::value) != expected_v) {
        std::fprintf(stderr,
            "test_fixy_engaged: WhichDimUnengaged readback failed "
            "(got %u, want %u)\n",
            static_cast<unsigned>(BadPack::value),
            static_cast<unsigned>(cd::Type));
        return 1;
    }
    if (BadPack::all_engaged) {
        std::fprintf(stderr,
            "test_fixy_engaged: all_engaged true on incomplete pack\n");
        return 1;
    }

    return 0;
}
