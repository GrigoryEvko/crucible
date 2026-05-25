// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule W001 (Phase C, widened FIXY-FOUND-061),
// UmwaitC01 variant:
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t is safety::Wait<UmwaitC01, U>
//   ⇒ ill-formed
//
// UmwaitC01 represents the Intel WAITPKG `UMWAIT` instruction
// (C0.1/C0.2 sub-states).  Per CLAUDE.md §IX latency-hierarchy table:
//
//     | `UMWAIT` (WAITPKG, C0.1/C0.2) | ~100-500 ns + wait time
//                                     | Low | Power-aware; expected
//                                     | wait 1-100 µs.  Not applicable
//                                     | on our hot path |
//
// 100-500 ns is roughly 2-12× the entire 40 ns intra-socket hot-path
// budget BEFORE any wait time.  Putting UmwaitC01 on the hot path is
// a direct §IX violation, NOT a valid replacement for spin-pause.
//
// W001's pre-FIXY-FOUND-061 predicate `is_park_or_blockier_v` =
// `leq(S, Park)` left UmwaitC01 silently hot-path-legal — a direct
// CLAUDE.md §IX contradiction.  The widened predicate
// `is_kernel_wait_v` = `leq(S, UmwaitC01)` catches all four
// kernel-mediated tiers including this WAITPKG strategy.
//
// HS14 #4 of 4 for W001 (FIXY-FOUND-061 widening).  Pairs with:
//   1. Wait<Park, T>         — condvar/futex tier
//   2. Wait<Block, T>        — chain-bottom worst-case tier
//   3. Wait<AcquireWait, T>  — atomic::wait / futex tier
//   4. Wait<UmwaitC01, T>    — WAITPKG UMWAIT tier (this)
//
// Concrete bug-class this catches: a future refactor that narrowed
// `is_kernel_wait_v` back to `leq(S, AcquireWait)` — leaving the
// top kernel-tier (UmwaitC01) silently hot-path-legal — would
// restore the §IX contradiction for WAITPKG-using code.  This
// fixture pins the rejection at the source-code declaration
// boundary, where the latency-budget contract belongs.
//
// Expected diagnostic substring: "W001:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/Wait.h>

namespace fn  = crucible::safety::fn;
namespace fx  = crucible::effects;
namespace sf  = crucible::safety;
using WS = crucible::algebra::lattices::WaitStrategy;

namespace neg_collision_w001_umwaitc01 {

using Bad = fn::Fn<
    sf::Wait<WS::UmwaitC01, int>,              // 1  Type — triggers W001
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_w001_umwaitc01

namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<
        ::neg_collision_w001_umwaitc01::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_w001_umwaitc01::Bad the_fixture{};

int main() { return 0; }
