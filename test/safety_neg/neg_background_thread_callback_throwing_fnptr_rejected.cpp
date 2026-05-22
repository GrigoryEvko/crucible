// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-086 HS14 fixture #1 of 2 for BackgroundThread::Region
// ReadyCallback::Fn:
// Assigning a NON-NOEXCEPT function-pointer to the typedef MUST be
// rejected — the noexcept tightening is load-bearing.
//
// Why this matters: the callback fires on the BG thread inside
// drain_for_one_iteration_.  If the callable threw, the unwinder
// would tear through Cipher persistence + tx_log commit + Deadline
// Watchdog observe, leaving the runtime in a half-applied state.
// With -fno-exceptions globally, throws cannot occur at runtime;
// the `noexcept` on the typedef pins this at the TYPE level so a
// future refactor that drops -fno-exceptions or wires in a
// non-noexcept callable reddens at the assignment site.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// ASSIGNMENT-CONVERSION half — a non-noexcept fn-ptr cannot
// convert to the noexcept-qualified typedef.  Sibling fixture
// `neg_background_thread_callback_type_identity_drift.cpp`
// exercises the TYPE-IDENTITY half (the typedef IS NOT the non-
// noexcept variant).
//
// Expected diagnostic: "invalid conversion|no known conversion|
// cannot convert|noexcept|RegionReadyCallback".

#include <crucible/BackgroundThread.h>

namespace c = crucible;

// A NON-noexcept callable matching the parameter list but lacking
// the noexcept specifier.  Pre-V-086 this would have converted to
// `RegionReadyCallback::Fn` silently; post-V-086 it MUST be
// rejected.
void throwing_callback(void* /*ctx*/, c::RegionNode* /*region*/) {
    // Body intentionally non-noexcept; the typedef MUST reject
    // this function pointer at assignment.
}

int main() {
    // STRAINING ASSIGNMENT: the load-bearing reject.  A
    // RegionReadyCallback value with .fn = throwing_callback would
    // (pre-V-086) silently compile because the typedef accepted
    // any `void(*)(void*, RegionNode*)`.  Post-V-086 the typedef
    // is `void(*)(void*, RegionNode*) noexcept` and the function
    // pointer above (lacking noexcept) MUST fail to convert.
    //
    // If this file compiles, the noexcept tightening regressed.
    c::BackgroundThread::RegionReadyCallback cb{
        .ctx = nullptr,
        .fn  = &throwing_callback,
    };
    (void)cb;
    return 0;
}
