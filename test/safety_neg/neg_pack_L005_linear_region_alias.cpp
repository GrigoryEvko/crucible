// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L005 — pack-level (FIXY-FOUND-066):
//
//     ∀ Fi, Fj ∈ pack, i ≠ j:
//       ¬(usage(Fi) == Linear ∧ usage(Fj) == Linear ∧
//         region_tag(Fi) == region_tag(Fj))
//
// Plain English: no two Linear-usage Fns in a frame may share the same
// `lifetime::In<Tag>`.  Linear values are exclusive owners of their
// region; two Linears tagged with the same region would alias, breaking
// the linearity invariant.
//
// Gap closed by L005 HS14 negative-compile fixture (FIXY-FOUND-066):
// L005 is a PACK-LEVEL rule — it cannot fire from a single Fn<>
// instantiation, so the per-Fn `L005_OK<F>` concept is tautologically
// true and dead in `AllRulesOK`/`first_failure`.  The rule's real
// enforcement is `pack::no_linear_region_alias_v<Fs...>` which production
// frame composition sites must spell explicitly.  `test_fixy_rule_helpers`
// asserts the predicate RETURNS false on violations via
// `static_assert(!no_linear_region_alias_v<...>)` — but that is a
// positive-direction test that the predicate's body is correct.  It
// does NOT witness that the rule fires as a HARD compile error in
// production-shape code: a regression dropping the rule body to
// `return true` would silently pass that inverted-assertion test
// (because `!true` is false, breaking the static_assert).  Actually
// the inverted assertion DOES catch body-deletion (it goes
// `static_assert(!true, ...)` which fails) — but it does NOT witness
// the production WILL_FAIL contract that real callers experience.
//
// This fixture pins the rule at the production-path discipline boundary
// by instantiating a `static_assert(pack_rule_v<violating-pack>, ...)`
// that COMPILES IFF the rule body is broken — under WILL_FAIL=TRUE the
// ctest reports PASS when compilation fails (the expected outcome).
//
// Expected diagnostic substring: "L005:".

#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Rules.h>
#include <crucible/safety/Fn.h>

namespace sfn = crucible::safety::fn;
namespace fx = crucible::effects;

// LinearA — Linear-usage, region tag 7, Classified.
using LinearA = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    fx::Row<>,
    sfn::SecLevel::Classified,
    sfn::proto::None,
    sfn::lifetime::In<7>,                              // ← region tag 7
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

// LinearAClone — IDENTICAL grade tuple as LinearA, same region tag 7.
// L005 fires on this pair: two Linears in region<7> alias.
using LinearAClone = sfn::Fn<int,
    sfn::pred::True,
    sfn::UsageMode::Linear,
    fx::Row<>,
    sfn::SecLevel::Classified,
    sfn::proto::None,
    sfn::lifetime::In<7>,                              // ← SAME region tag 7
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

// Opt the Linear bindings out of L004 (Linear × region without
// Permission proof) — the L004 marker is a separate enforcement that
// would otherwise red Linear+In<Tag> at single-Fn instantiation.
// We're isolating L005 here.
namespace crucible::safety::fn::collision {
template <> struct marks_lifetime_region_unprotected<::LinearA>      : std::false_type {};
template <> struct marks_lifetime_region_unprotected<::LinearAClone> : std::false_type {};
}  // namespace crucible::safety::fn::collision

// THIS is the L005 violation: assert that no_linear_region_alias_v
// holds for a pack containing two Linears in the same region.  It does
// NOT hold — the predicate returns false — so the static_assert fires
// L005's compile error.
static_assert(
    crucible::fixy::rule::pack::no_linear_region_alias_v<LinearA, LinearAClone>,
    "L005: two Linear-usage Fns sharing lifetime::In<7> alias the "
    "region — Linear values are exclusive owners; aliasing breaks the "
    "linearity invariant. Move ownership into a single binding, or pin "
    "the second Linear to a different region tag.");

int main() { return 0; }
