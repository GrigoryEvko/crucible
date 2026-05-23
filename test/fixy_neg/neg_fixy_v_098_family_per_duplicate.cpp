// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-098 HS14 fixture 2/2.  A Grants pack engaging the
// `DimensionAxis::SyscallSurface` axis TWICE — here BOTH a family-tier
// grant (`family_file_mutation`, Family.h) AND a per-syscall grant
// (`per<SyscallId::write>`, Per.h) — signals an authoring contradiction.
// The binding claims both "everything in the FileMutation tier is
// acceptable" AND "this specific syscall ID is what gets invoked" —
// which the resolver cannot disambiguate without a silent first-wins
// rule.  Reject.h's `UniqueEngagementPerAxis` concept (FIXY-AUDIT-A3)
// must fire on the duplicate SyscallSurface-routing.
//
// Mismatch class for HS14 audit: duplicate-engagement (distinct from
// the NTTP-enum-type mismatch fixture's template-id-formation
// rejection — both class paths protect the SAME engagement walk but
// at orthogonal stages: NTTP-mismatch rejects BEFORE the grant reaches
// the pack; duplicate-engagement rejects AFTER all grants are
// individually well-formed but their pack as a whole engages
// SyscallSurface twice).
//
// Architectural intent: V-098's design choice to expose BOTH a
// per-family coarse grant (Family.h) AND a parametric per-syscall grant
// (Per.h) covers two distinct use cases — declaring "this binding's
// syscalls all fall in tier X" vs "this binding invokes EXACTLY this
// specific syscall" — but they MUST NOT be combined in one binding's
// Grants pack.  Both surfaces route through which_dim<> →
// SyscallSurface; the engagement walk catches the double-engage and
// emits the named FixyDuplicate_SyscallSurface diagnostic per Reject.h.
//
// Concrete bug-class this catches: a contributor reads Family.h's
// doc-block, opts for `family_file_mutation` for the general case, then
// on review adds `per<SyscallId::pwrite>` "for documentation" —
// silently lossy under any first-wins resolver rule, structurally
// rejected here.
//
// Expected diagnostic: "UniqueEngagementPerAxis" OR
// "FixyDuplicate_SyscallSurface" — the satisfaction-failure chain
// names either the multiplicity concept or the per-axis duplicate tag.

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace grs  = crucible::fixy::grant::syscall;
using D        = crucible::fixy::dim::DimensionAxis;
using SI       = grs::SyscallId;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 29-element pack: 27 distinct non-SyscallSurface-non-FpMode axes
    // engaged with strict markers + a strict FpMode engagement + BOTH
    // family_file_mutation (Family.h) AND per<SyscallId::pwrite>
    // (Per.h) covering SyscallSurface twice.  The duplicate
    // SyscallSurface engagement is the load-bearing rejection cause;
    // every OTHER axis is uniquely engaged.
    auto bad = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        strict<D::Synchronization>, strict<D::Regime>,
        strict<D::FpMode>,
        strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>,
        strict<D::GlobalState>, strict<D::Stdio>,
        grs::family_file_mutation        /* SyscallSurface #1 */,
        grs::per<SI::pwrite>             /* SyscallSurface #2 — duplicate */>(42);
    (void)bad;
    return 0;
}
