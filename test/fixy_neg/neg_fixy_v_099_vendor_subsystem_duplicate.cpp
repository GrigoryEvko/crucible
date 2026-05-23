// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-099 HS14 fixture 2/2.  A Grants pack engaging the
// `DimensionAxis::SyscallSurface` axis TWICE — here BOTH Ioctl.h
// parametric grants in V-099 (`ioctl::vendor<IoctlVendor::nvidia_ctl>`
// AND `ioctl::subsystem<IoctlSubsystem::drm>`) — signals an authoring
// contradiction.  The binding claims both "this binding's ioctls live
// on the NVIDIA driver" AND "this binding's ioctls are DRM-portable"
// — which the resolver cannot disambiguate without a silent first-
// wins rule.  Reject.h's `UniqueEngagementPerAxis` concept (FIXY-
// AUDIT-A3) must fire on the duplicate SyscallSurface-routing.
//
// Mismatch class for HS14 audit: cross-grant duplicate-engagement
// (distinct from the NTTP-enum-type mismatch fixture's template-id-
// formation rejection — both class paths protect the SAME
// SyscallSurface axis but at orthogonal stages: NTTP-mismatch rejects
// BEFORE the grant reaches the pack; duplicate-engagement rejects
// AFTER all grants are individually well-formed but their pack as a
// whole engages SyscallSurface twice).
//
// Architectural intent: V-099 exposes TWO parametric grants
// (`vendor<V>` for per-device-file framing AND `subsystem<S>` for
// per-kernel-subsystem framing) precisely because the two cover
// orthogonal authoring use cases — "I'm Mimic-NVIDIA-locked" vs "I'm
// DRM-portable across vendors".  They MUST NOT be combined in one
// binding's Grants pack.  Both surfaces route through which_dim<> →
// SyscallSurface; the engagement walk catches the double-engage and
// emits the named FixyDuplicate_SyscallSurface diagnostic per
// Reject.h.
//
// Concrete bug-class this catches: a contributor reads V-099's doc-
// block, opts for `ioctl::vendor<IoctlVendor::nvidia_uvm>` for the
// general case, then on review adds
// `ioctl::subsystem<IoctlSubsystem::drm>` "for portability
// documentation" — silently lossy under any first-wins resolver rule,
// structurally rejected here.
//
// Expected diagnostic: "UniqueEngagementPerAxis" OR
// "FixyDuplicate_SyscallSurface" — the satisfaction-failure chain
// names either the multiplicity concept or the per-axis duplicate
// tag.

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/syscall/Ioctl.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace grs  = crucible::fixy::grant::syscall;
namespace gri  = crucible::fixy::grant::syscall::ioctl;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 29-element pack: 27 distinct non-SyscallSurface-non-FpMode axes
    // engaged with strict markers + a strict FpMode engagement + BOTH
    // ioctl::vendor<IoctlVendor::nvidia_ctl> (V-099 Ioctl.h) AND
    // ioctl::subsystem<IoctlSubsystem::drm> (V-099 Ioctl.h) covering
    // SyscallSurface twice.  The duplicate SyscallSurface engagement
    // is the load-bearing rejection cause; every OTHER axis is
    // uniquely engaged.
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
        gri::vendor<grs::IoctlVendor::nvidia_ctl>     /* SyscallSurface #1 */,
        gri::subsystem<grs::IoctlSubsystem::drm>      /* SyscallSurface #2 — duplicate */>(42);
    (void)bad;
    return 0;
}
