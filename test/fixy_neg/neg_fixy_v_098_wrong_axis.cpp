// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-098 HS14 fixture 1/2.  Parametric grant `per<SyscallId>` takes
// a non-type template parameter typed to `safety::syscall::SyscallId`
// (Per.h's catalog of 36 representative syscalls).  Substituting a value
// from a DIFFERENT axis-related enum (here: `safety::SyscallFamily`,
// V-097's chain-tier enum) MUST be rejected at template-id formation.
//
// This is the V-098 analogue of V-092's
// `neg_fixy_grant_fp_wrong_nttp_enum.cpp`: those reject mistyped
// FpMode-sub-axis enum values in `with_fp_rounding<...>`; this fixture
// rejects mistyped SyscallId-vs-SyscallFamily values in `per<...>`.
//
// Architectural intent: SyscallId (the per-syscall enum) and
// SyscallFamily (the chain-tier enum) are STRUCTURALLY DISTINCT types
// precisely so the type system catches "I meant per<SyscallId::write>
// but I typed SyscallFamily::FileMutation" at the SAME spelling site.
// Without per-axis typing the bug becomes "this binding was supposed
// to claim ONE specific syscall but actually claimed a tier-coarse
// family grant" — silently wrong-grade, undetectable until V-100's
// bridge surfaces a wrong row lift at the call site.
//
// Mismatch class for HS14 audit: NTTP-enum-type mismatch (distinct from
// the duplicate-engagement fixture #2 — both class paths protect the
// same axis (SyscallSurface) but at orthogonal stages: NTTP-mismatch
// rejects BEFORE the grant reaches the pack; duplicate-engagement
// rejects AFTER all grants are individually well-formed but their
// pack as a whole engages SyscallSurface twice).
//
// Expected diagnostic: GCC template-id-formation error noting the NTTP
// value cannot be converted to the expected enum type (SyscallId);
// the regex below matches either standard "cannot convert" wording or
// the relevant enum names.

#include <crucible/fixy/syscall/Per.h>

namespace fxgs = crucible::fixy::grant::syscall;
namespace cal  = crucible::algebra::lattices;

// Substituting a SyscallFamily value for the SyscallId NTTP on
// `per<...>` — the parametric grant's template-parameter type is
// `safety::syscall::SyscallId`, NOT `algebra::lattices::SyscallFamily`.
// Template-id formation rejects.
using Bad =
    fxgs::per<cal::SyscallFamily::FileMutation /* wrong enum type */>;

int main() {
    [[maybe_unused]] Bad b{};
    return 0;
}
