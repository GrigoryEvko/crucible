// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule P003 (FIXY-V-243):
//
//     marks_fork_worker<F>::value == true
//   ∧ F::type_t carries a ControlFlowPinned tier >= ThrowOnly
//   ⇒ ill-formed
//
// Plain English: a permission_fork worker body MUST NOT throw.  A throw
// inside a jthread fork body crosses no thread boundary; under
// -fno-exceptions it is std::terminate.  This is the catalog-level
// codification of the V-087 permission_fork-body static_assert (which
// rejected grant::ctrl::throws) — P003 generalizes it to any ControlFlow
// tier >= ThrowOnly carried on the worker's result type.
//
// Mismatch class: fork-worker marker × ControlFlow tier >= ThrowOnly.
// Uses the WRAPPER-TIER trigger path (ControlFlowPinned<ThrowOnly>) plus
// the fork-worker marker — distinct from L006's Linear×MayLongjmp and the
// pure-marker fixtures.
//
// Concrete bug-class this catches: dropping the `cf_at_or_above_v<
// ThrowOnly, type_t>` term from P003_OK would let a throwing fork body
// compile, re-opening the jthread-terminate path V-087 closed.
//
// Expected diagnostic substring: "P003:"

#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using CF = crucible::algebra::lattices::ControlFlow;

namespace neg_collision_p003 {
using Bad = fn::Fn<sf::ControlFlowPinned<CF::ThrowOnly, int>>;  // CF tier >= ThrowOnly
}  // namespace neg_collision_p003

// Mark Bad as a permission_fork worker body — required to fire P003
// (the rule guards marks_fork_worker AND a throwing ControlFlow tier).
namespace crucible::safety::fn::collision {
    template <> struct marks_fork_worker<::neg_collision_p003::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_p003::Bad the_fixture{};

int main() { return 0; }
