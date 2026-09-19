// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule W001 (Phase C, FIXY-V-081), Block variant:
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t is safety::Wait<Block, U>  (chain bottom)
//   ⇒ ill-formed
//
// This fixture probes the bottom of the WaitLattice (Block strategy).
// Block ⊑ UmwaitC01 in the chain, so `is_kernel_wait_v<Block>` is
// TRUE — the rule fires.  Block represents the worst case: `poll`,
// `epoll_wait`, blocking `read()` — strategies that may block
// indefinitely on a kernel-driven readiness event.  Putting one on
// the hot path is structurally even worse than Park (condvar wakes
// on signal; blocking-syscall wakes on I/O readiness — different
// failure mode but same latency-budget violation).
//
// HS14 #2 of 4 for W001 (FIXY-FOUND-061 widening) — pairs with:
//   1. Wait<Park, T>         — condvar/futex tier
//   2. Wait<Block, T>        — chain-bottom worst-case tier (this)
//   3. Wait<AcquireWait, T>  — atomic::wait / futex tier
//   4. Wait<UmwaitC01, T>    — WAITPKG UMWAIT tier
//
// All four cover structurally distinct lattice tiers in the
// kernel-tier rejected region; a refactor that accidentally narrowed
// `is_kernel_wait_v` to a subset would silently let the excluded
// tier slip through — these fixtures catch that.
//
// Expected diagnostic substring: "W001:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/_Wait.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;
using WS = crucible::algebra::lattices::WaitStrategy;

namespace neg_collision_w001_block {

using Bad = fn::Fn<sf::Wait<WS::Block, int>,  // 1  Type — chain bottom
                   fn::pred::True,  // 2  Refinement
                   fn::UsageMode::Linear,  // 3  Usage
                   fx::Row<>,  // 4  EffectRow
                   fn::SecLevel::Public,  // 5  Security
                   fn::proto::None,  // 6  Protocol
                   fn::lifetime::Static,  // 7  Lifetime
                   fn::source::FromInternal,  // 8  Source
                   fn::trust::Verified,  // 9  Trust
                   fn::ReprKind::Opaque,  // 10 Repr
                   fn::cost::Constant,  // 11 Cost
                   fn::precision::Exact,  // 12 Precision
                   fn::space::Bounded<sizeof(int)>,  // 13 Space
                   fn::OverflowMode::Trap,  // 14 Overflow
                   fn::MutationMode::Immutable,  // 15 Mutation
                   fn::ReentrancyMode::NonReentrant,  // 16 Reentrancy
                   fn::size_pol::Sized<sizeof(int)>,  // 17 Size
                   /*Version=*/1,  // 18 Version
                   fn::stale::Fresh  // 19 Staleness
                   >;

}  // namespace neg_collision_w001_block

namespace crucible::safety::fn::collision {
template <>
struct marks_hot_path<::neg_collision_w001_block::Bad> : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_w001_block::Bad the_fixture{};

int main() { return 0; }
