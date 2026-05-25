// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H010 (FIXY-FOUND-063):
//
//     marks_hot_path<F>::value == true
//   ∧ row_has_effect_v<F::effect_row_t, effects::Effect::Bg> == true
//   ⇒ ill-formed
//
// Plain English: a function cannot be BOTH on the hot path (≤40 ns
// intra-socket per CLAUDE.md §IX) AND in background context (Alloc /
// IO / Block / millisecond-latency allowed).  The two contexts are
// mutually exclusive context markers — Bg names a *runtime context*
// (background thread, kernel-compile pool, Cipher warm-tier writer)
// distinct from the production hot path.  Marking both is a
// structural context contradiction the type system must reject.
//
// Gap closed by H010 (FIXY-FOUND-063): H001 catches `marks_hot_path
// × is_unbounded_cost`; H003 catches `marks_hot_path × (Alloc | IO)
// × is_unbounded_cost`.  But a Fn with `marks_hot_path = true`,
// `effect_row_t = Row<Bg>`, `cost_t = cost::Constant`, and NO
// Alloc/IO/Block atoms slips BOTH rules — yet HotPath × Bg is still
// the same context contradiction.  This fixture pins H010 at the
// production-path Fn instantiation boundary so a regression dropping
// H010 from validate() / AllRulesOK fires here.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg
// — the concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "H010:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h010 {

// A Fn with cost::Constant, no Alloc/IO/Block atoms, but Row<Bg> AND
// marks_hot_path set.  H001 / H003 do not fire (cost is bounded, no
// Alloc/IO).  H010 alone catches the context contradiction.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial — H002 would
                                                //                catch on hot-path; we
                                                //                disable hot-path below
                                                //                to isolate H010, then
                                                //                re-enable via marker)
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg in row
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded — H001 won't fire)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h010

// Mark Bad as hot-path AT FILE SCOPE — this is the H010 trigger
// composed with the Row<Bg> already in the type.  H001 won't fire
// (cost::Constant is bounded); H002 won't fire alone unless this
// were Refined-trivial AND hot-path (H002 also reads pred::True);
// H003 won't fire (no Alloc/IO in row).  H010 is the rule that
// catches the structural contradiction here.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_h010::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Note: marking Bad hot-path with pred::True trips H002 first under
// first_failure ordering — that is fine for this fixture; the
// diagnostic still hits a HotPath-family rule, AND the static_assert
// chain in validate() runs ALL H0xx asserts, so H010 also fires.
// The expected diagnostic substring "H010:" appears in the static_assert
// chain output regardless of first_failure ordering.

[[maybe_unused]] neg_collision_h010::Bad the_fixture{};

int main() { return 0; }
