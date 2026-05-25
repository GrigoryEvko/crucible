// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule W001 (Phase C, widened FIXY-FOUND-061),
// AcquireWait variant:
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t is safety::Wait<AcquireWait, U>
//   ⇒ ill-formed
//
// AcquireWait represents `std::atomic<T>::wait` — futex-backed on
// Linux (4-byte aligned `__platform_wait_t`).  Per CLAUDE.md §IX
// latency-hierarchy table:
//
//     | `std::atomic::wait/notify` | 1-5 µs | … | BANNED on hot path |
//     | `futex(FUTEX_WAIT)`        | 1-5 µs | … | BANNED on hot path |
//
// W001's pre-FIXY-FOUND-061 predicate `is_park_or_blockier_v` =
// `leq(S, Park)` only caught {Block, Park}, leaving AcquireWait and
// UmwaitC01 as silent hot-path-legal — a direct CLAUDE.md §IX
// contradiction.  The widened predicate `is_kernel_wait_v` =
// `leq(S, UmwaitC01)` catches all four kernel-mediated tiers.
//
// HS14 #3 of 4 for W001 (FIXY-FOUND-061 widening).  Pairs with:
//   1. Wait<Park, T>         — condvar/futex tier
//   2. Wait<Block, T>        — chain-bottom worst-case tier
//   3. Wait<AcquireWait, T>  — atomic::wait / futex tier (this)
//   4. Wait<UmwaitC01, T>    — WAITPKG UMWAIT tier
//
// Concrete bug-class this catches: a future refactor that narrowed
// `is_kernel_wait_v` back to `leq(S, Park)` — restoring the
// pre-FIXY-FOUND-061 under-rejection — would silently let
// `Wait<AcquireWait>` slip through onto hot-path Fn instantiations.
// This fixture pins the rejection at the source-code declaration
// boundary, where the latency-budget contract belongs.
//
// Expected diagnostic substring: "W001:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/Wait.h>

namespace fn  = crucible::safety::fn;
namespace fx  = crucible::effects;
namespace sf  = crucible::safety;
using WS = crucible::algebra::lattices::WaitStrategy;

namespace neg_collision_w001_acquirewait {

using Bad = fn::Fn<
    sf::Wait<WS::AcquireWait, int>,            // 1  Type — triggers W001
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

}  // namespace neg_collision_w001_acquirewait

namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<
        ::neg_collision_w001_acquirewait::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_w001_acquirewait::Bad the_fixture{};

int main() { return 0; }
