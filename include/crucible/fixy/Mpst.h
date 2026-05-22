#pragma once

// ── fixy/Mpst.h — DEPRECATED shim → fixy/SessGlobal.h (FIXY-V-068) ─
//
// This header is a one-release-cycle deprecation shim.  V-068 renamed
// `fixy/Mpst.h` → `fixy/SessGlobal.h` to align with the `Sess<Foo>.h`
// naming convention used by V-059..V-066 session-surface carve-outs.
//
// ── Migration ──────────────────────────────────────────────────────
//
// Existing call sites:
//
//   #include <crucible/fixy/Mpst.h>          // OLD path (still works)
//
// New call sites should prefer:
//
//   #include <crucible/fixy/SessGlobal.h>    // NEW canonical path
//
// Namespace identity is preserved across both paths — every symbol
// continues to live under `crucible::fixy::sess::mpst::` exactly as
// before V-068; the only change is the include path.
//
// ── Removal schedule ──────────────────────────────────────────────
//
// One release cycle (cross-reference FIXY-U-123 retire-stale-fixture
// pattern).  At that point this shim deletes outright.  A grep guard
// over the codebase audits residual `#include <crucible/fixy/Mpst.h>`
// sites before deletion is permitted.

#include <crucible/fixy/SessGlobal.h>

// ═════════════════════════════════════════════════════════════════════
// ── Cross-binding sentinel battery (FIXY-V-068) ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Witnesses that this shim is a TYPE-IDENTICAL re-export of
// fixy/SessGlobal.h.  Any caller that includes both paths in the same
// TU must see identical types resolved through identical lookup —
// otherwise the V-068 rename silently broke the rename's premise that
// it is "pure cosmetic, namespace identity preserved".
//
// These sentinels fire at every consumer's include time, not only
// inside the dedicated reach test, so a regression in either path
// trips IMMEDIATELY at the offending TU's compile.

namespace crucible::fixy::sess::mpst::v068_shim_test {

// The shim DOES re-export the canonical surface (cardinality witness
// already lives in SessGlobal.h::u013_self_test::u013_surface_cardinality).
// What this sentinel claims is structural: the namespace `mpst::`
// reached through `<crucible/fixy/Mpst.h>` is one-and-the-same
// namespace as the one reached through `<crucible/fixy/SessGlobal.h>`.
// If the shim were to expose its OWN copy (e.g., via an inline
// namespace or a copy-paste of using-decls), the type identity below
// would break.

struct ShimProbe {};
using EndAlias_via_shim =
    ::crucible::fixy::sess::mpst::End_G;
using TransmissionAlias_via_shim =
    ::crucible::fixy::sess::mpst::Transmission<ShimProbe, ShimProbe,
                                                ShimProbe,
                                                ::crucible::fixy::sess::mpst::End_G>;

// The substrate type SessGlobal.h re-exports IS the same type the
// shim path resolves to.  Wave any future refactorer's "let me make
// the shim a real namespace" idea red.
static_assert(std::is_same_v<EndAlias_via_shim,
                             ::crucible::safety::proto::End_G>,
    "fixy/Mpst.h shim must resolve End_G to the same substrate type "
    "as fixy/SessGlobal.h.  If this red-lights, the shim was rewritten "
    "to re-declare types instead of re-include them — restore the "
    "single-include form.");

}  // namespace crucible::fixy::sess::mpst::v068_shim_test
