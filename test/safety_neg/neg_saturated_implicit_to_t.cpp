// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::Saturated<T> (#1084 - Saturated piece).
//
// Premise: Saturated<T> → T conversion MUST be EXPLICIT.  The escape
// hatch `static_cast<T>(saturated)` is allowed; implicit `T x = sat;`
// is NOT.  This prevents callers from accidentally dropping the
// `was_clamped()` signal through implicit conversion.
//
// Without this rejection, code like:
//
//   Saturated<uint64_t> result = compute_storage_nbytes(...);
//   uint64_t total = result;     // implicit T conversion — drops flag
//   if (total > threshold) ...   // "did this saturate?" lost
//
// would silently pass.  The whole point of Saturated<T> is to surface
// the clamped signal at the call site; making the T conversion
// explicit forces every caller to decide what to do with the flag
// before stripping it.
//
// Implicit FROM T (the "fresh value, no clamping observed" case) IS
// allowed by design — that's how `Saturated<T> s = some_value;`
// works ergonomically.  The asymmetry is deliberate.
//
// Expected diagnostic: "no implicit conversion" / "cannot convert" /
// "explicit conversion required" / "no matching function for
// initialization" pointing at the implicit T = Saturated<T>
// assignment.

#include <crucible/safety/Saturated.h>

namespace saf = crucible::safety;

int main() {
    saf::Saturated<unsigned long> sat = saf::add_sat_checked<unsigned long>(10, 20);
    // Bridge fires: implicit Saturated<unsigned long> → unsigned long
    // is rejected by the explicit conversion operator.
    unsigned long bad = sat;   // ← compile error here
    (void)bad;
    return 0;
}
