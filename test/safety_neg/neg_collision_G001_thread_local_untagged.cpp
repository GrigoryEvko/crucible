// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule G001 (FIXY-V-243):
//
//     marks_thread_local_untagged<F>::value == true  ⇒  ill-formed
//
// Plain English: a thread_local grant (the future
// grant::global::thread_local_<>) declared without a TLSTag NTTP reds.
// An untagged thread_local cannot be distinguished in the federation
// cache key, nor audited for per-thread-singleton init-order hazards —
// it mirrors the safety::ThreadLocalRef<Tag, T> discipline (V-080) which
// REQUIRES a unique phantom tag.
//
// Mismatch class: thread_local grant without a TLSTag (marker-driven; the
// V-246 grant header specializes the trait when no tag NTTP is supplied).
//
// Concrete bug-class this catches: dropping G001 would let an untagged
// thread_local declaration compile, re-opening the Scenario B
// crucible_fallback.cpp schema-cache aliasing hazard.
//
// Expected diagnostic substring: "G001:"

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_g001 {
struct TlsMarker {};
using Bad = fn::Fn<TlsMarker>;
}  // namespace neg_collision_g001

namespace crucible::safety::fn::collision {
    template <> struct marks_thread_local_untagged<::neg_collision_g001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_g001::Bad the_fixture{};

int main() { return 0; }
