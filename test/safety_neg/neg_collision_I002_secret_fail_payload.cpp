// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule I002 — SecLevel::Secret arm (FIXY-FOUND-069 sub-HS14
// closure).
//
// Companion fixture to neg_collision_I002_classified_fail_payload.cpp
// (which covers the SecLevel::Classified arm via the default security
// level).  I002_OK gates on:
//
//   concept I002_OK = !(classified_v<F> && has_fail_v<F> &&
//                       !fail_error_secret_v<F>);
//
// `classified_v<F>` is a 2-arm OR-fold over the Security axis:
//
//   * F::security_v == SecLevel::Classified   (CLASSIFIED arm)
//   * F::security_v == SecLevel::Secret        (SECRET arm)
//
// The shipped fixture exercises the Classified arm (default security);
// THIS fixture exercises the Secret arm (explicit SecLevel::Secret).
// Both are "classified" to I002 — Secret data flowing through a Fail(E)
// path with a non-secret error payload leaks the classification through
// the error channel exactly as Classified data would.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `== SecLevel::Classified` term from
//     classified_v breaks the Classified path → caught by the original
//     fixture.
//   * A refactor that drops the `== SecLevel::Secret` term breaks the
//     Secret path → caught by THIS fixture.
//   * Without both, classified_v could silently degenerate to a
//     single-level rule and ship; Secret payloads (the strictest
//     classification) would then leak through Fail(E) unflagged — the
//     more dangerous of the two regressions.
//
// Mismatch class: SecLevel::Secret × marks_fail × non-secret error.
// Distinct from the Classified class because the classification signal
// comes from the Secret enumerator, not Classified.  I003 silent (no CT,
// no marks_fail_on_secret).  I004 silent (no async, no session protocol).
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the §6.8 validate() leg
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "I002:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_i002_secret {

// A Fn whose security level is SECRET (classified_v's Secret arm), with
// a Fail(E) path (marks_fail) and a NON-secret error payload
// (marks_fail_error_secret left unspecialized → fail_error_secret_v is
// false → !fail_error_secret_v is true).  No CT, no async, no session →
// I003 / I004 stay silent.  I002 alone catches the secret-leak-via-error
// shape.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow — empty
    fn::SecLevel::Secret,                      // 5  Security — SECRET (I002 Secret arm)
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_i002_secret

// Engage the Fail(E) path with a non-secret error payload: marks_fail
// supplies has_fail_v; marks_fail_error_secret is left default-false so
// !fail_error_secret_v holds.  Combined with SecLevel::Secret, I002 fires.
namespace crucible::safety::fn::collision {
    template <> struct marks_fail<::neg_collision_i002_secret::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_i002_secret::Bad the_fixture{};

int main() { return 0; }
