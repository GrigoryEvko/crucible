// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-232 HS14 fixture #1 of 2 — distinct mismatch class:
// "back-filling narrower provenance retroactively from External".
//
// Violation: invoking `Tagged<T, source::External>::retag<
// source::FromUserPath>()` to claim that arbitrary External bytes
// actually originated from CLI input.  This is a lie about the
// audit trail — External bytes could have come from any external
// source (network, FFI, file, env, ...); pretending they specifically
// came from CLI would let per-source policy (V-233) draw the wrong
// conclusion (e.g. "this looks user-typed, so apply the FromUserPath
// sanitize policy" when actually it came from a network payload).
//
// V-022's fail-closed primary template fires here — no
// retag_policy<source::External, source::FromUserPath> specialization
// exists (V-232 deliberately ships none), so the `RetagAllowed`
// concept on `Tagged::retag<>()` rejects the call at compile time.
//
// Sister fixture: neg_path_source_userpath_to_envpath_retag.cpp
// exercises lateral cross-narrowing (FromUserPath → FromEnvPath),
// a structurally distinct mismatch class.

#include <crucible/safety/source/Path.h>
#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

int main() {
    // Construct a Tagged with the existing source::External tag —
    // V-031's inaugural provenance for arbitrary external bytes.
    ns::Tagged<int, ns::source::External> external{0};

    // Re-tighten External → FromUserPath: claim the bytes came
    // specifically from CLI input.  V-232 does NOT admit this
    // transition; the requires-clause on Tagged::retag fires.
    auto narrowed =
        std::move(external).retag<ns::source::FromUserPath>();
    (void)narrowed;
    return 0;
}
