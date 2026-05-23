// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule G002 (FIXY-V-249):
//
//     marks_thread_local_atomic<F>::value == true  ⇒  ill-formed
//
// Plain English: a grant::global::thread_local_<Tag> PAIRED with an atomic
// memory-order wrapper is a category error — an atomic op on a per-thread
// object orders against no peer (one instance per thread). This is the
// Scenario E hazard (bench_smoke.cpp:78 shipped `static thread_local
// std::atomic<uint64_t>`).
//
// Mismatch class #1 of 2: PRODUCTION path — the marker fires through
// Fn<>'s own static_assert(ValidComposition<Fn>), the same gate that
// rejects a direct `Fn<X, BadCombo...>` construction.
//
// Expected diagnostic substring: G002 / thread_local / atomic.

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_g002 {
struct TlsAtomicMarker {};
using Bad = fn::Fn<TlsAtomicMarker>;
}  // namespace neg_collision_g002

namespace crucible::safety::fn::collision {
    template <> struct marks_thread_local_atomic<::neg_collision_g002::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_g002::Bad the_fixture{};

int main() { return 0; }
