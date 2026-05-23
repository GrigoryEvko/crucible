// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule D001 (FIXY-V-243):
//
//     marks_indirect_call_not_noexcept<F>::value == true  ⇒  ill-formed
//
// Plain English: a function carrying an indirect-call grant (the future
// grant::dispatch::indirect_call<FnPtrFamily>) whose callable RunFn is
// NOT noexcept reds.  A throwing indirect call across a -fno-exceptions
// boundary terminates the process.  This closes Agent 10 Scenario A —
// BackgroundThread::RegionReadyCallback::Fn was missing noexcept, so a
// throwing user callback would std::terminate the bg thread mid-drain.
//
// Mismatch class: indirect-call grant with non-noexcept callable
// (marker-driven; the V-245 grant header specializes this trait from the
// family's RunFn signature).
//
// Concrete bug-class this catches: a refactor that drops the D001 term
// from AllRulesOK / validate() would let a non-noexcept indirect-call
// surface compile clean, re-opening the Scenario A terminate path.
//
// Expected diagnostic substring: "D001:"

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_d001 {
struct CallbackMarker {};
using Bad = fn::Fn<CallbackMarker>;
}  // namespace neg_collision_d001

namespace crucible::safety::fn::collision {
    template <> struct marks_indirect_call_not_noexcept<::neg_collision_d001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_d001::Bad the_fixture{};

int main() { return 0; }
