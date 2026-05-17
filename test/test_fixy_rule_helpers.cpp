// ── test_fixy_rule_helpers — pack-level helper sentinel (B7) ───────
//
// Positive-compile sentinel for the pack-level helpers re-exported
// under `fixy::rule::pack::` per FIXY-AUDIT-B7.  Five helpers:
//
//   1. no_linear_region_alias_v<Fs...>  — backs R017 / L005
//   2. frame_axis_consistent_v<Fs...>   — backs R018 / F001
//   3. is_linear_in_region_v<F>         — per-Fn helper
//   4. same_region_tag_v<Tag1, Tag2>    — region-tag identity
//   5. region_tag_of_t<L>               — lifetime → region-tag carrier
//
// Each helper is exercised on a known-coherent pack (positive case)
// and a known-incoherent pack (negative case) so the trip behavior
// is witnessed for both branches.

#include <crucible/fixy/Rules.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace sfn  = crucible::safety::fn;

// ─── Forward declarations + L004 trait specializations ────────────
//
// L004 (Linear × lifetime::In<Tag> needs Permission) is the per-Fn
// rule that defaults the binding to "unprotected" and requires the
// author to opt out by specializing `marks_lifetime_region_unprotected
// <MyFn>` to false.  The trait specializations below thread the
// proof-token assertion so the Fn instantiations are accepted by
// ValidComposition, allowing us to exercise the pack-level helpers
// `no_linear_region_alias_v` / `frame_axis_consistent_v` /
// `is_linear_in_region_v` against well-formed Fns.
//
// Pattern: alias the Fn instantiation, then specialize
// marks_lifetime_region_unprotected to false on that exact spelling.

using LinearA = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    crucible::effects::Row<>,
    sfn::SecLevel::Classified,
    sfn::proto::None,
    sfn::lifetime::In<7>,  // region tag 7
    crucible::safety::source::FromInternal,
    crucible::safety::trust::Verified,
    sfn::ReprKind::Opaque,
    sfn::cost::Unstated,
    sfn::precision::Exact,
    sfn::space::Zero,
    sfn::OverflowMode::Trap,
    sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant,
    sfn::size_pol::Unstated,
    1u,
    sfn::stale::Fresh>;

using LinearB = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    crucible::effects::Row<>,
    sfn::SecLevel::Classified,
    sfn::proto::None,
    sfn::lifetime::In<11>,  // region tag 11 — DIFFERENT
    crucible::safety::source::FromInternal,
    crucible::safety::trust::Verified,
    sfn::ReprKind::Opaque,
    sfn::cost::Unstated,
    sfn::precision::Exact,
    sfn::space::Zero,
    sfn::OverflowMode::Trap,
    sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant,
    sfn::size_pol::Unstated,
    1u,
    sfn::stale::Fresh>;

// LinearAClone — same region tag 7 as LinearA.  Pairing
// {LinearA, LinearAClone} aliases on region 7 → rejection.
using LinearAClone = sfn::Fn<long,  // different Type, same region
    sfn::pred::True,
    sfn::UsageMode::Linear,
    crucible::effects::Row<>,
    sfn::SecLevel::Classified,
    sfn::proto::None,
    sfn::lifetime::In<7>,  // region tag 7 — SAME as LinearA
    crucible::safety::source::FromInternal,
    crucible::safety::trust::Verified,
    sfn::ReprKind::Opaque,
    sfn::cost::Unstated,
    sfn::precision::Exact,
    sfn::space::Zero,
    sfn::OverflowMode::Trap,
    sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant,
    sfn::size_pol::Unstated,
    1u,
    sfn::stale::Fresh>;

// PublicFn — same scaffold as LinearA but Security pinned to Public
// (rather than Classified).  Pairing {LinearA, PublicFn} has
// inconsistent Security axes → frame_axis_consistent_v rejects.
using PublicFn = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    crucible::effects::Row<>,
    sfn::SecLevel::Public,  // PUBLIC — diverges from LinearA's Classified
    sfn::proto::None,
    sfn::lifetime::In<13>,
    crucible::safety::source::FromInternal,
    crucible::safety::trust::Verified,
    sfn::ReprKind::Opaque,
    sfn::cost::Unstated,
    sfn::precision::Exact,
    sfn::space::Zero,
    sfn::OverflowMode::Trap,
    sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant,
    sfn::size_pol::Unstated,
    1u,
    sfn::stale::Fresh>;

// ─── L004 trait specializations — assert proof-token plumbed ──────
//
// Each Linear-in-region Fn instantiation specializes
// marks_lifetime_region_unprotected to false_type so the L004 rule
// recognizes the binding as Permission-protected.  Without this
// opt-out, ValidComposition rejects Linear × In<Tag>.

namespace crucible::safety::fn::collision {
template <> struct marks_lifetime_region_unprotected<::LinearA>      : std::false_type {};
template <> struct marks_lifetime_region_unprotected<::LinearB>      : std::false_type {};
template <> struct marks_lifetime_region_unprotected<::LinearAClone> : std::false_type {};
template <> struct marks_lifetime_region_unprotected<::PublicFn>     : std::false_type {};
}  // namespace crucible::safety::fn::collision

// ─── 1. no_linear_region_alias_v — coherent + alias ──────────────

static_assert(fixy::rule::pack::no_linear_region_alias_v<>,
    "Empty pack must accept — no aliasing possible.");

static_assert(fixy::rule::pack::no_linear_region_alias_v<LinearA>,
    "Single-binding pack must accept — no aliasing possible.");

static_assert(fixy::rule::pack::no_linear_region_alias_v<LinearA, LinearB>,
    "Two Linears in DIFFERENT regions must accept.");

static_assert(!fixy::rule::pack::no_linear_region_alias_v<LinearA, LinearAClone>,
    "Two Linears in the SAME region (tag 7) must reject — alias.");

// ─── 2. frame_axis_consistent_v — coherent + drift ───────────────

static_assert(fixy::rule::pack::frame_axis_consistent_v<>,
    "Empty pack must accept — vacuously consistent.");

static_assert(fixy::rule::pack::frame_axis_consistent_v<LinearA>,
    "Single-binding pack is consistent with itself.");

static_assert(fixy::rule::pack::frame_axis_consistent_v<LinearA, LinearB>,
    "LinearA + LinearB agree on Security (both Classified).");

static_assert(!fixy::rule::pack::frame_axis_consistent_v<LinearA, PublicFn>,
    "LinearA (Classified) + PublicFn (Public) disagree on Security.");

// ─── 3. is_linear_in_region_v — per-Fn helper ────────────────────

static_assert(fixy::rule::pack::is_linear_in_region_v<LinearA>,
    "LinearA is Linear in lifetime::In<7> — is_linear_in_region must accept.");

// ─── 4. same_region_tag_v — type-level identity ──────────────────

using TagA      = fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<7>>;
using TagAClone = fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<7>>;
using TagB      = fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<11>>;

static_assert(fixy::rule::pack::same_region_tag_v<TagA, TagAClone>,
    "Identical region tags must compare equal.");

static_assert(!fixy::rule::pack::same_region_tag_v<TagA, TagB>,
    "Distinct region tags must compare unequal.");

// ─── 5. region_tag_of_t — non-region lifetime returns void ────────

static_assert(std::is_void_v<
    fixy::rule::pack::region_tag_of_t<sfn::lifetime::Static>>,
    "region_tag_of_t<lifetime::Static> must be void (non-region sentinel).");

static_assert(!std::is_void_v<
    fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<7>>>,
    "region_tag_of_t<lifetime::In<7>> must be non-void.");

int main() { return 0; }
