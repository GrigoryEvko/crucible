// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-099 HS14 fixture 1/2.  Parametric grant `ioctl::vendor<IoctlVendor>`
// takes a non-type template parameter typed to
// `safety::syscall::IoctlVendor` (Ioctl.h's 12-element device-file
// catalog).  Substituting a value from a DIFFERENT V-099 enum (here:
// `safety::syscall::IoctlSubsystem`, the orthogonal kernel-subsystem
// catalog) MUST be rejected at template-id formation.
//
// This is the V-099 analogue of V-098's
// `neg_fixy_v_098_wrong_axis.cpp` (which rejects a SyscallFamily value
// substituted into `per<SyscallId>`).  The mismatch class is identical
// — NTTP enum-type mismatch caught at template-id formation, before
// any concept evaluates — but the witnessed surface is V-099's TWO
// parametric grants engaging the SAME axis (SyscallSurface): the
// cross-type substitution is structurally rejected even though both
// enums are V-099 syscall surfaces.
//
// Architectural intent: IoctlVendor (per-device-file enum) and
// IoctlSubsystem (per-kernel-subsystem enum) are STRUCTURALLY DISTINCT
// types precisely so the type system catches "I meant
// ioctl::vendor<IoctlVendor::nvidia_ctl> but I typed
// IoctlSubsystem::drm" at the SAME spelling site.  Without per-axis
// typing the bug becomes "this binding was supposed to claim a vendor
// device file but actually claimed a portable subsystem framing" —
// silently wrong identity, undetectable until V-100's bridge surfaces
// the wrong row lift at the call site.
//
// Mismatch class for HS14 audit: NTTP-enum-type mismatch (distinct
// from the cross-grant duplicate-engagement fixture #2 — both class
// paths protect the V-099 surface but at orthogonal stages: NTTP-
// mismatch rejects BEFORE the grant reaches the pack; cross-grant
// duplicate-engagement rejects AFTER all grants are individually well-
// formed but their pack as a whole engages SyscallSurface twice).
//
// Expected diagnostic: GCC template-id-formation error noting the NTTP
// value cannot be converted to the expected enum type (IoctlVendor);
// the regex below matches either standard "cannot convert" wording or
// the relevant enum names.

#include <crucible/fixy/syscall/Ioctl.h>

namespace fxgs = crucible::fixy::grant::syscall;
namespace fxgi = crucible::fixy::grant::syscall::ioctl;

// Substituting an IoctlSubsystem value for the IoctlVendor NTTP on
// `ioctl::vendor<...>` — the parametric grant's template-parameter
// type is `safety::syscall::IoctlVendor`, NOT `IoctlSubsystem`.
// Template-id formation rejects.
using Bad =
    fxgi::vendor<fxgs::IoctlSubsystem::drm /* wrong enum type */>;

int main() {
    [[maybe_unused]] Bad b{};
    return 0;
}
