// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-092 HS14 fixture 1/2.  A parametric FpMode grant
// (`with_fp_rounding<Mode>`, `with_fp_ftz<Mode>`, ...) takes a
// non-type template parameter typed to a SPECIFIC FpMode sub-axis enum
// (FpRounding, FpFtz, FpContract, ...).  Substituting a value from a
// DIFFERENT FpMode sub-axis enum (e.g., passing an `FpFtz` value into
// `with_fp_rounding<...>`) MUST be rejected at template-id formation.
//
// This is the FpMode-specific analogue of the cv-ref / non-grant
// classes already covered by Grant.h's fixy-A4-033 / fixy-M-09
// fixtures: those reject mistyped types in the GRANTS pack; this
// fixture rejects a mistyped value in a parametric grant's NTTP
// before the grant ever reaches the pack.
//
// Architectural intent: the 11 per-sub-axis enums are intentionally
// distinct types (FpRounding ≠ FpFtz ≠ FpContract ≠ ...) precisely so
// the type system catches "I meant to pin Rounding but I typed an Ftz
// value" at the SAME spelling site.  Without per-axis typing the bug
// becomes "this kernel was supposed to be RoundToZero but actually
// got FlushToZero" — silently wrong-mode, not bit-distinct,
// undetectable until cross-vendor CI reddens days later.
//
// Mismatch class for HS14 audit: NTTP-enum-type mismatch (distinct
// from cv-ref-qualified-grant (fixy-A4-033) and non-grant-in-pack
// (FIXY-AUDIT-D2) — three orthogonal rejection paths in template
// argument deduction).
//
// Expected diagnostic: a GCC template-id-formation error noting that
// the NTTP value cannot be converted to the expected enum type
// (FpRounding); the regex below matches either the standard
// "cannot convert" wording or any of the relevant enum names.

#include <crucible/fixy/Fp.h>

namespace fxg = crucible::fixy::grant;
namespace sf  = crucible::safety;

// Substituting an FpFtz value for the FpRounding NTTP on
// with_fp_rounding<...> — the parametric grant's template-parameter
// type is `safety::FpRounding`, NOT `safety::FpFtz`.  Template-id
// formation rejects.
using Bad =
    fxg::with_fp_rounding<sf::FpFtz::FlushToZero /* wrong enum type */>;

int main() {
    [[maybe_unused]] Bad b{};
    return 0;
}
