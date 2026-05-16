#pragma once

// ── crucible::fixy — Rules.h (FIXY-B4) ─────────────────────────────────
//
// Re-export of the substrate's 12 §6.8 collision rule tags under the
// `fixy::rule::*` namespace.  When a `fixy::fn<Type, Grants...>`
// instantiation reaches the underlying `safety::fn::Fn<...>` body,
// the substrate's `ValidComposition<Fn>` concept fires the
// appropriate rule's diagnostic.  Re-exporting under fixy:: lets
// fixy-side callers reference the rule tags without crossing the
// `safety::fn::collision::` boundary.
//
// ── The 12 rules (fixy.md §6.8 Table A) ──────────────────────────────
//
//   I002 — classified value flows through Fail(E) without secret payload.
//   L002 — borrow combined with async suspension.
//   E044 — constant-time region combined with async scheduling.
//   I003 — constant-time function fails on secret-dependent condition.
//   M012 — monotonic mutation in concurrent context without atomic.
//   P002 — ghost data used by runtime code.
//   I004 — classified async session without CT discipline.
//   N002 — exact decimal type combined with wrap overflow.
//   L003 — borrow combined with unscoped spawn.
//   M011 — linear resource live across Fail without cleanup.
//   S010 — non-fresh staleness combined with constant-time.
//   S011 — ephemeral capability used in replay-required code.
//
// ── Reachability through `fixy::fn` (FIXY-B4 audit) ──────────────────
//
// Substrate's collision detectors live in `safety/CollisionCatalog.h`
// and check both AXIS values (Usage/Mutation/Overflow/Repr/etc., set
// directly via grant tags) AND MARKER traits (`marks_async`,
// `marks_ct`, `marks_fail`, `marks_runtime_ghost_use`, ...) that the
// substrate primary-templates to false_type.  Marker propagation from
// fixy grants to substrate marker traits requires partial
// specialization on `marks_X<safety::fn::Fn<...>>` — substrate-side
// scaffold; not in fixy slice.
//
//   Rule | Reachable via grants today | Gap
//   -----+----------------------------+----------------------------
//   I002 | NO  — needs marks_fail + marks_fail_error_secret
//   L002 | NO  — needs marks_async (grant::borrow gives borrow_capture)
//   E044 | NO  — needs marks_ct + marks_async
//   I003 | NO  — needs marks_ct + marks_fail + marks_fail_on_secret
//   M012 | YES — grant::monotonic_advance + grant::with<Bg>; SHIPPED
//   P002 | NO  — grant::ghost gives Usage=Ghost; needs marks_runtime_ghost_use
//   I004 | NO  — needs marks_async on classified session path
//   N002 | YES — grant::overflow_wrap + user-defined exact_decimal type
//   L003 | NO  — needs marks_unscoped_spawn
//   M011 | NO  — needs marks_fail + marks_linear_uncleaned_fail
//   S010 | NO  — needs marks_ct
//   S011 | NO  — needs marks_replay_required (capability replay tracking)
//
// Of the 12 rules, **2 are reachable today** purely through grant tags
// (M012 with a fixture; N002 needs a user-side exact_decimal type so
// the fixture lives test-side).  The remaining 10 await substrate
// marker-propagation work; their fixtures are documented gaps, not
// missing tests.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — substrate tag identity preserved via `using` aliases.
//   DetSafe  — bit-identical re-export across compiles.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase B
//   misc/fixy.md §24.2 collision rule table
//   safety/CollisionCatalog.h — substrate-side rule tags

#include <crucible/cog/CostProjection.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Witness.h>
#include <crucible/fixy/dim/Cost.h>
#include <crucible/fixy/dim/Termination.h>
#include <crucible/safety/CollisionCatalog.h>
#include <crucible/safety/diag/TestRegistry.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::rule {

namespace _collision = ::crucible::safety::fn::collision;

// ─── 12-rule re-export ─────────────────────────────────────────────
using I002 = _collision::I002_ClassifiedFailPayload;
using L002 = _collision::L002_BorrowAsync;
using E044 = _collision::E044_ConstantTimeAsync;
using I003 = _collision::I003_ConstantTimeFailOnSecret;
using M012 = _collision::M012_MonotonicConcurrentNoAtomic;
using P002 = _collision::P002_GhostRuntimeUse;
using I004 = _collision::I004_ClassifiedAsyncSession;
using N002 = _collision::N002_DecimalOverflowWrap;
using L003 = _collision::L003_BorrowUnscopedSpawn;
using M011 = _collision::M011_LinearFailNoCleanup;
using S010 = _collision::S010_StalenessConstantTime;
using S011 = _collision::S011_CapabilityReplay;

// ─── RuleCode enum re-export ───────────────────────────────────────
using RuleCode = _collision::RuleCode;

// ─── Catalog tuple re-export ───────────────────────────────────────
using Catalog = _collision::Catalog;

static_assert(std::tuple_size_v<Catalog> == 12,
    "fixy::rule::Catalog must mirror substrate's 12-rule catalog.");

// ─── Bijection self-tests ──────────────────────────────────────────
//
// Each fixy::rule::X is the same TYPE as the substrate's
// corresponding collision tag.  std::is_same_v pins the aliasing.

static_assert(std::is_same_v<I002, _collision::I002_ClassifiedFailPayload>);
static_assert(std::is_same_v<L002, _collision::L002_BorrowAsync>);
static_assert(std::is_same_v<E044, _collision::E044_ConstantTimeAsync>);
static_assert(std::is_same_v<I003, _collision::I003_ConstantTimeFailOnSecret>);
static_assert(std::is_same_v<M012, _collision::M012_MonotonicConcurrentNoAtomic>);
static_assert(std::is_same_v<P002, _collision::P002_GhostRuntimeUse>);
static_assert(std::is_same_v<I004, _collision::I004_ClassifiedAsyncSession>);
static_assert(std::is_same_v<N002, _collision::N002_DecimalOverflowWrap>);
static_assert(std::is_same_v<L003, _collision::L003_BorrowUnscopedSpawn>);
static_assert(std::is_same_v<M011, _collision::M011_LinearFailNoCleanup>);
static_assert(std::is_same_v<S010, _collision::S010_StalenessConstantTime>);
static_assert(std::is_same_v<S011, _collision::S011_CapabilityReplay>);

// ═════════════════════════════════════════════════════════════════════
// ── R016 — HotPath::Hot residency demands Tested witness floor ─────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-G9: bindings that flow through the hot-path / hot-residency
// admission gate must carry at least Tested witness on the Trust and
// Reentrancy axes.  Asserted-only witness on Trust would admit
// unaudited bindings to the foreground recording path; Asserted-only
// on Reentrancy would admit recursive helpers without test coverage.
//
// This is the fixy-side companion to substrate's `HotPathViolation`
// (Category 5).  The substrate fires on tier mismatch (Hot vs Warm vs
// Cold callee); R016 fires earlier — at the witness check — so a
// caller never sees a HotPath<Hot, ...> binding constructed from an
// Asserted-only grant in the first place.
//
// `R016_requires_witness_floor_v<F>` is the consumer-side predicate.
// HotPath admission templates can spell:
//
//   template <typename F>
//       requires rule::R016_requires_witness_floor_v<F>
//   void admit_hot_path(F&&);
//
// to enforce the floor at the call site.

namespace _w = ::crucible::safety::witness;

// Minimum tier per axis under R016.  Tested<sentinel> is the floor;
// any concretely-numbered Tested<id> or higher tier satisfies.
template <typename F>
inline constexpr bool R016_requires_witness_floor_v =
    ::crucible::fixy::IsFixyFn<F> &&
    ::crucible::fixy::FnWitnessAtLeast<F, ::crucible::fixy::dim::Trust,
        _w::Tested<::crucible::safety::diag::UnnamedTestId>> &&
    ::crucible::fixy::FnWitnessAtLeast<F, ::crucible::fixy::dim::Reentrancy,
        _w::Tested<::crucible::safety::diag::UnnamedTestId>>;

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-G10: Modality-class collision rules R017 / R018 ───────────
// ═════════════════════════════════════════════════════════════════════
//
// R017_LinearAlias — two Linear-modality grants on the same Lifetime
// region tag is illegal.  Linear modality (CSL) encodes one-shot
// consume-and-produce; two parallel consumers of the same Permission
// breaks linearity.  R017 fires at binding construction time, before
// R013's call-site check.
//
// R018_FrameDeclaresConsistency — a Frame-modality grant cannot
// coexist with a Declares-modality grant on the same axis in the
// same binding.  Frame says "this property is INVARIANT of the value";
// Declares says "the binding PRODUCES this property".  The two claims
// are categorically incompatible.

namespace detail {

// Per-grant predicate: does G engage Axis with modality class MC?
// Uses ::crucible::fixy::detail::engages_dim_v (where engages_dim_v
// actually lives — Reject.h moves it into the detail namespace).
template <typename G, ::crucible::fixy::dim::DimAxis Axis,
          ::crucible::fixy::ModalityClass MC>
inline constexpr bool grant_axis_modality_v = false;

template <typename G, ::crucible::fixy::dim::DimAxis Axis,
          ::crucible::fixy::ModalityClass MC>
    requires (::crucible::fixy::detail::engages_dim_v<G, Axis>)
inline constexpr bool grant_axis_modality_v<G, Axis, MC> =
    (::crucible::fixy::grant_traits<G>::modality_class_v == MC);

template <typename F>
struct fn_pack_traits;

template <typename T, typename... Grants>
struct fn_pack_traits<::crucible::fixy::fn<T, Grants...>> {
    template <::crucible::fixy::dim::DimAxis Axis,
              ::crucible::fixy::ModalityClass MC>
    static constexpr std::size_t count_axis_modality_v =
        (static_cast<std::size_t>(grant_axis_modality_v<Grants, Axis, MC>)
            + ... + std::size_t{0});

    // FIXY-G10 Followup A: per-RegionTag aliasing check.  Two Linear-
    // modality grants engaging different region tags compose cleanly
    // (CSL frame rule: disjoint permissions); ONLY same-tag pairs are
    // aliased and rejected.  Delegates to
    // ::crucible::fixy::same_region_tag_aliased_v over the pack.
    static constexpr bool same_region_aliased_v =
        ::crucible::fixy::same_region_tag_aliased_v<Grants...>;

    // FIXY-G10 Followup B: Frame×Declares consistency.  Delegates to
    // ::crucible::fixy::frame_declares_consistency_v over the pack.
    // True iff no axis has both Frame and Declares engagements.
    static constexpr bool frame_declares_ok_v =
        ::crucible::fixy::frame_declares_consistency_v<Grants...>;
};

}  // namespace detail

// R017 (Followup A): a binding is rejected iff two-or-more Linear-
// modality grants on dim::Lifetime engage the SAME RegionTag NTTP.
// Different-tag combinations (disjoint permissions per CSL frame rule)
// now compile cleanly.  Pre-followup R017 over-rejected; post-followup
// R017 catches exactly the structural alias.
template <typename F>
inline constexpr bool R017_no_linear_alias_v =
    ::crucible::fixy::IsFixyFn<F> &&
    !(detail::fn_pack_traits<std::remove_cvref_t<F>>::same_region_aliased_v);

// R018 (Followup B): a binding cannot have BOTH Frame and Declares on
// the same dim.  Pre-followup R018 was a placeholder that always
// returned true on well-formed fn<> instantiations (exactly-one
// engagement per dim is structurally maintained by fn<>'s
// reject-by-default discipline).  Post-followup R018 delegates to
// frame_declares_consistency_v, which scans the entire Grants pack —
// catching cases where a stance::compose intermediate violates the
// consistency invariant before reaching the final fn<> aggregator.
template <typename F>
inline constexpr bool R018_frame_declares_consistency_v =
    ::crucible::fixy::IsFixyFn<F> &&
    detail::fn_pack_traits<std::remove_cvref_t<F>>::frame_declares_ok_v;

// FIXY-G10 self-tests are exercised at the production call sites in
// test_fixy_modality.cpp + the worked example.

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-G11: R015 — Hot-residency cost discipline ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// R015 — HotPath::Hot residency requires bounded cost.  Bindings
// claiming Hot residency MUST carry a cost_polynomial<...> grant with
// finite leading coefficients; bare cost_unknown (with
// CostPolynomial<UINT64_MAX>) is rejected — the residency-tier
// promotion gate cannot admit a binding with no cost bound.
//
// HotPath/residency-tier identity is a substrate-level annotation
// (safety::HotPath<Tier, T>); at the fixy layer, R015 fires as a
// consumer-side predicate that downstream hot-tier admission
// templates spell as:
//
//   template <typename F>
//       requires rule::R015_hot_cost_bounded_v<F>
//   void admit_hot_path(F&&);

namespace detail {

// True iff F's cost polynomial is BOUNDED — every coefficient is
// strictly less than UINT64_MAX (saturation infinity).  An unbounded
// polynomial signifies "no cost claim made"; R015 rejects.
template <typename F>
struct cost_is_bounded {
    using poly = ::crucible::fixy::fn_cost_polynomial_t<F>;
private:
    [[nodiscard]] static consteval bool all_coeffs_bounded() noexcept {
        for (auto c : poly::coeffs) {
            if (c == UINT64_MAX) return false;
        }
        return true;
    }
public:
    static constexpr bool value = all_coeffs_bounded();
};

}  // namespace detail

// R015: hot-residency bindings need a bounded cost.
template <typename F>
inline constexpr bool R015_hot_cost_bounded_v =
    ::crucible::fixy::IsFixyFn<F> &&
    ::crucible::fixy::HasCostGrant<F> &&
    detail::cost_is_bounded<std::remove_cvref_t<F>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-G12: R014 / R019 / R020 — Bounded-resource discipline ─────
// ═════════════════════════════════════════════════════════════════════
//
// R014 — BgWorker + Observable requires bounded_alloc + wallclock_budget.
// A background-context binding that reports through the warden surface
// must declare both its memory ceiling AND its wallclock deadline.
// Otherwise the warden has nothing to bound the worker against;
// runaway BG workers cause OOM / cluster-wide deadline cascades.
//
// R019 — Hot-path bindings require terminating + bounded_alloc<≤4KB>
// + bounded_io<0>.  The hot path admits no syscalls and no growable
// allocations.
//
// R020 — Federation peer roles require terminating + wallclock_budget.
// A federation participant that can spin indefinitely poisons the
// MPST channel.

namespace detail {

template <typename F>
inline constexpr bool has_terminating_claim_v =
    ::crucible::fixy::is_terminating_v<F>;

template <typename F>
inline constexpr bool has_wallclock_budget_v =
    (::crucible::fixy::wallclock_budget_v<F> != UINT64_MAX);

template <typename F>
inline constexpr bool has_bounded_alloc_v =
    (::crucible::fixy::bounded_alloc_v<F> != UINT64_MAX);

template <typename F>
inline constexpr bool has_bounded_io_v =
    (::crucible::fixy::bounded_io_v<F> != UINT64_MAX);

}  // namespace detail

// R014: BG worker bindings with observability MUST carry both
// bounded_alloc and wallclock_budget.  Consumer-side gate; callers
// promoting a binding to "observable" wrap with this predicate.
template <typename F>
inline constexpr bool R014_bg_observable_bounded_v =
    ::crucible::fixy::IsFixyFn<F> &&
    detail::has_bounded_alloc_v<F> &&
    detail::has_wallclock_budget_v<F>;

// R019: Hot-path bindings — strictest bounded-resource profile.
//   * terminating engaged
//   * bounded_alloc ≤ 4 KB
//   * bounded_io == 0 (zero syscalls)
template <typename F>
inline constexpr bool R019_hot_path_resources_v =
    ::crucible::fixy::IsFixyFn<F> &&
    detail::has_terminating_claim_v<F> &&
    detail::has_bounded_alloc_v<F> &&
    (::crucible::fixy::bounded_alloc_v<F> <= 4096) &&
    detail::has_bounded_io_v<F> &&
    (::crucible::fixy::bounded_io_v<F> == 0);

// R020: Federation peer roles MUST carry terminating + wallclock_budget.
template <typename F>
inline constexpr bool R020_federation_peer_bounded_v =
    ::crucible::fixy::IsFixyFn<F> &&
    detail::has_terminating_claim_v<F> &&
    detail::has_wallclock_budget_v<F>;

// ═════════════════════════════════════════════════════════════════════
// ── Cross-axis cost-budget soundness check (G11 × G12) ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// When a binding engages BOTH wallclock_budget<N> AND cost_polynomial,
// the cost-budget soundness check verifies wallclock_budget ≥ predicted
// cost on a known Cog.  When the Cog is compile-time known via
// VendorCtx (or a similar context), the check fires at the call site;
// when the Cog is runtime-only, the check defers to warden-arm time.
//
// `cost_within_budget_v<F, KnownCog, InputSize>` ships the compile-
// time check: predicted_cost_v ≤ wallclock_budget_v.  A consumer
// demanding both gates writes:
//
//   template <typename F, cog::CogKind K, std::uint64_t N>
//       requires rule::cost_within_budget_v<F, K, N>
//   void admit_with_cog(F&&);

template <typename F, ::crucible::cog::CogKind K, std::uint64_t InputSize>
inline constexpr bool cost_within_budget_v = []() {
    if constexpr (!::crucible::fixy::HasCostGrant<F>) {
        return false;  // no cost claim — cannot verify budget
    } else if constexpr (!detail::has_wallclock_budget_v<F>) {
        return false;  // no budget claim — vacuous
    } else {
        constexpr std::uint64_t predicted =
            ::crucible::cog::predicted_cost_v<F, K, InputSize>;
        constexpr std::uint64_t budget =
            ::crucible::fixy::wallclock_budget_v<F>;
        return predicted <= budget;
    }
}();

}  // namespace crucible::fixy::rule
