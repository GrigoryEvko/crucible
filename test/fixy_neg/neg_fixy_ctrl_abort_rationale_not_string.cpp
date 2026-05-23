// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-244 HS14 fixture 2/3 — abort rationale must be a string literal.
//
// `grant::ctrl::abort<Rationale>`'s template parameter is the fixed-string
// NTTP type `ctrl::rationale` (placeholder form, deduced via CTAD-for-NTTP,
// P1907R1).  A non-string NTTP value (here `42`) has no `rationale`
// constructor to deduce from, so `abort<42>` is rejected at template-id
// formation — the rationale CANNOT be an arbitrary integer / enum.
//
// Why this matters: the rationale is the audit payload.  Allowing a bare
// integer would let `abort<0>` masquerade as a documented abort site while
// carrying no human-readable reason — exactly the silent-abort the grant
// exists to forbid.
//
// Mismatch class for HS14 audit: NTTP TYPE-CONVERSION failure (int → the
// rationale fixed-string type) — distinct from the missing-argument arity
// path (fixture 1) and the IsGrantTag cv-purity path (fixture 3).
//
// Expected diagnostic: a GCC class-template-argument-deduction /
// conversion error naming the rationale type or an int→class conversion.

#include <crucible/fixy/grant/Ctrl.h>

namespace ctrl = crucible::fixy::grant::ctrl;

// 42 cannot deduce a `ctrl::rationale<N>` — no matching constructor.
using Bad = ctrl::abort<42>;

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
