// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule W001 (Phase C, FIXY-V-081):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t is safety::Wait<Strategy, U> where Strategy ⊑ Park
//   ⇒ ill-formed
//
// Plain English: a hot-path function MUST NOT wrap its return type
// (or any parameter type) in a Wait wrapper at Park strictness or
// blockier.  Wait<Park, T> involves std::condition_variable::wait /
// pthread_cond_wait — 1-5 µs latency on Linux futex-backed condvars.
// The hot-path latency budget is ≤ 40 ns intra-socket per CLAUDE.md
// §IX.  A 1-5 µs blocking wait exceeds that budget by ~25-125×.
//
// This fixture uses Wait<Park, int> — the tier directly named by the
// rule.  Pairs with neg_collision_W001_hotpath_wait_block.cpp (chain
// bottom), neg_collision_W001_hotpath_wait_acquirewait.cpp (futex-
// backed), and neg_collision_W001_hotpath_wait_umwaitc01.cpp (WAITPKG).
// Together they probe all four kernel-tier lattice positions in the
// rejected region per CLAUDE.md §IX (FIXY-FOUND-061 widening).
//
// Concrete bug-class this catches: a refactor that loosened the
// W001 gate — e.g. dropped `wait_strategy_of` detector OR changed
// `is_kernel_wait_v` to a permissive predicate — would silently
// let a hot-path function declare itself wrapped in Wait<Park>,
// then silently accept the kernel-trap on every call.  This fixture
// pins the rejection at the source-code declaration boundary, where
// the latency-budget contract belongs.
//
// Expected diagnostic substring: "W001:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/Wait.h>

namespace fn  = crucible::safety::fn;
namespace fx  = crucible::effects;
namespace sf  = crucible::safety;
using WS = crucible::algebra::lattices::WaitStrategy;

namespace neg_collision_w001_park {

// Hot-path function whose return type is Wait<Park, int> — the W001
// rejected combination.  No specialization of marks_hot_path is
// needed at file scope yet; the Fn instance triggers the rule via
// its type_t in CollisionRules::validate's static_assert when we
// turn the marks_hot_path opt-in on (see specialization below).
using Bad = fn::Fn<
    sf::Wait<WS::Park, int>,                   // 1  Type — triggers W001
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

}  // namespace neg_collision_w001_park

// Mark Bad as hot-path — required to fire W001 (rule guards
// marks_hot_path AND Wait<Park|Block>).  Specialization at file
// scope, like H001/H002/H003 fixtures.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_w001_park::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire W001.
[[maybe_unused]] neg_collision_w001_park::Bad the_fixture{};

int main() { return 0; }
