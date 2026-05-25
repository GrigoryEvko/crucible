// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F001 — pack-level (FIXY-FOUND-066):
//
//     ∀ Fi, Fj ∈ pack: security_v(Fi) == security_v(Fj)
//
// Plain English: every Fn in a frame manifesto must agree on the
// Security axis.  A frame composing a Classified Fn alongside a Public
// Fn declares two disjoint information-flow domains as one binding —
// the type system must reject this contradiction.
//
// Gap closed by F001 HS14 negative-compile fixture (FIXY-FOUND-066):
// F001 is a PACK-LEVEL rule (`pack::frame_axis_consistent_v<Fs...>`),
// not single-Fn — the per-Fn `F001_OK<F>` concept is tautologically
// true.  Production frame composition sites must spell the pack
// predicate explicitly.  This fixture witnesses that the predicate
// fires as a HARD compile error in production-shape code, complementing
// the inverted-direction positive-correctness test in
// test_fixy_rule_helpers.
//
// Expected diagnostic substring: "F001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Rules.h>
#include <crucible/safety/Fn.h>

namespace sfn = crucible::safety::fn;
namespace fx = crucible::effects;

// LinearA — Classified-Security.
using LinearA = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    fx::Row<>,
    sfn::SecLevel::Classified,                         // ← Classified
    sfn::proto::None,
    sfn::lifetime::In<7>,
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

// PublicFn — same scaffold as LinearA, but Security pinned to Public.
// F001 rejects the pair {LinearA (Classified), PublicFn (Public)}: the
// security axis disagrees across the frame.
using PublicFn = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    fx::Row<>,
    sfn::SecLevel::Public,                             // ← Public diverges from LinearA's Classified
    sfn::proto::None,
    sfn::lifetime::In<13>,                             // different region — avoid L005
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

// Opt out of L004 so Linear × In<Tag> doesn't fire on either binding.
namespace crucible::safety::fn::collision {
template <> struct marks_lifetime_region_unprotected<::LinearA>  : std::false_type {};
template <> struct marks_lifetime_region_unprotected<::PublicFn> : std::false_type {};
}  // namespace crucible::safety::fn::collision

// THIS is the F001 violation: assert frame_axis_consistent_v holds for
// a security-disagreeing pack.  It does not — the predicate returns
// false — so the static_assert fires F001's compile error.
static_assert(
    crucible::fixy::rule::pack::frame_axis_consistent_v<LinearA, PublicFn>,
    "F001: frame composes Fns with disagreeing Security axes — LinearA "
    "carries Classified, PublicFn carries Public; declaring both in one "
    "frame would merge two disjoint information-flow domains. Pick one "
    "security level for the frame, or split into per-domain frames.");

int main() { return 0; }
