// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-100 HS14 fixture 2/2.  Bridge.h's `lift_syscall_grant_row_t<G>`
// is a concept-gated alias template requiring `IsSyscallGrantTag<G>`.
// Passing a WELL-FORMED grant tag on the WRONG AXIS (here: V-092's
// `fp_strict_ieee` which routes to `DimensionAxis::FpMode`) must be
// rejected at the concept's SyscallSurface arm — the grant is
// structurally valid (passes IsGrantTag) but routes elsewhere.
//
// Architectural intent: a contributor adding a syscall row lift at a
// new call site might accidentally pass an FpMode (or any other axis')
// grant tag.  Without the SyscallSurface gate, the lift would silently
// instantiate via family_tier<G> — except family_tier<G> is undefined
// for non-syscall grants, producing a cryptic "incomplete type"
// diagnostic deep in the alias-template expansion.  IsSyscallGrantTag
// catches the bug at the API boundary with a named-concept message.
//
// Mismatch class for HS14 audit: off-axis grant rejected at the
// SyscallSurface arm of IsSyscallGrantTag (distinct from fixture #1's
// non-grant rejection at the IsGrantTag arm).  fp_strict_ieee passes
// the IsGrantTag arm cleanly (it IS a grant — final + grant_base +
// cv-ref-free per V-092 Fp.h line 236) but routes to FpMode (V-092's
// axis), not SyscallSurface (V-097's axis).
//
// Expected diagnostic: GCC concept-failure message mentioning
// `IsSyscallGrantTag` OR `SyscallSurface` OR `lift_syscall_grant_row_t`
// requires-clause failure; the regex below matches either the named
// concept or the SyscallSurface dimension name.

#include <crucible/fixy/syscall/Bridge.h>
#include <crucible/fixy/Fp.h>

namespace cb = crucible::fixy::syscall::bridge;
namespace cg = crucible::fixy::grant;

// Passing fp_strict_ieee — a well-formed FpMode-axis grant — to the
// SyscallSurface-gated lift.  IsGrantTag PASSES (it IS a grant);
// which_dim_v == FpMode FAILS the SyscallSurface arm; the alias
// requires-clause emits a named-concept satisfaction-failure
// diagnostic citing IsSyscallGrantTag.
using Bad = cb::lift_syscall_grant_row_t<cg::fp_strict_ieee /* wrong axis */>;

int main() {
    [[maybe_unused]] Bad* b = nullptr;
    return 0;
}
