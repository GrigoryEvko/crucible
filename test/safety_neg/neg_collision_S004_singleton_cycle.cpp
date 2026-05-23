// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule S004 (FIXY-V-243):
//
//     marks_singleton_init_cycle<F>::value == true  ⇒  ill-formed
//
// Plain English: the V-248 tag-graph closure walk over registered
// grant::global::singleton<Tag> annotations detected a cycle in the
// lazy-init dependency graph and flagged this Fn.  A cycle is the
// static-initialization-order fiasco in its subtlest form: the first
// thread to touch either singleton triggers a re-entrant initialization
// that observes a half-constructed peer.
//
// Mismatch class: singleton init-dependency cycle (marker-driven; V-248
// computes the cycle via pack::singleton_init_acyclic and specializes
// the trait for participating tags).  The reusable detector ships in
// V-243 and is exercised positively in test_fixy_v_243; this fixture
// proves the Fn-level rejection rail fires when the walk reports a cycle.
//
// Concrete bug-class this catches: dropping S004 would let a flagged
// init-cycle Fn compile, re-opening the Scenario D Meyers-singleton
// fiasco.
//
// Expected diagnostic substring: "S004:"

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_s004 {
struct SingletonMarker {};
using Bad = fn::Fn<SingletonMarker>;
}  // namespace neg_collision_s004

namespace crucible::safety::fn::collision {
    template <> struct marks_singleton_init_cycle<::neg_collision_s004::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_s004::Bad the_fixture{};

int main() { return 0; }
