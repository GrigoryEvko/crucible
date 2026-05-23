// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule G002 (FIXY-V-249), second mismatch class.
//
// Mismatch class #2 of 2: CONCEPT-ISOLATION path — assert G002_OK<F>
// directly on a carrier whose marks_thread_local_atomic is specialized
// true. This proves G002_OK is itself load-bearing (rejects independent of
// the full ValidComposition chain), so a refactor that drops G002 from the
// AllRulesOK conjunction is caught here even if an earlier rule would have
// masked it in the production path.
//
// Distinct from neg_collision_G002_thread_local_atomic.cpp, which fires
// through Fn<>'s instantiation-time static_assert; here the failure is the
// standalone concept on a non-Fn probe type.
//
// Expected diagnostic substring: G002.

#include <crucible/safety/CollisionCatalog.h>

#include <type_traits>

namespace csc = crucible::safety::fn::collision;

namespace neg_collision_g002_direct {
struct Probe {};
}  // namespace neg_collision_g002_direct

namespace crucible::safety::fn::collision {
    template <> struct marks_thread_local_atomic<::neg_collision_g002_direct::Probe>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

static_assert(csc::G002_OK<::neg_collision_g002_direct::Probe>,
              "G002: per-thread atomic is nonsensical (thread_local × atomic)");

int main() { return 0; }
