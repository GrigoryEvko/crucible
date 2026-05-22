// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-232 HS14 fixture #2 of 2 — distinct mismatch class:
// "lateral cross-narrowing between sibling provenance lanes".
//
// Violation: invoking `Tagged<T, source::FromUserPath>::retag<
// source::FromEnvPath>()` to claim that a CLI-sourced path actually
// came from an environment variable.  The three narrower path tags
// — FromUserPath, FromEnvPath, FromConfigPath — are ORTHOGONAL
// siblings, not subtypes of one another; cross-narrowing without
// re-running the sanitize boundary would let per-source policy
// (V-233) draw the wrong conclusion about how trustworthy the
// path's origin is.
//
// V-022's fail-closed primary template fires here — no
// retag_policy<source::FromUserPath, source::FromEnvPath>
// specialization exists (V-232 deliberately ships none), so the
// `RetagAllowed` concept on `Tagged::retag<>()` rejects the call
// at compile time.
//
// Sister fixture: neg_path_source_external_to_userpath_retag.cpp
// exercises wide→narrow back-filling (External → FromUserPath), a
// structurally distinct mismatch class.

#include <crucible/safety/source/Path.h>
#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

int main() {
    // Construct a Tagged with the V-232 source::FromUserPath tag —
    // path originated from CLI/REPL input.
    ns::Tagged<int, ns::source::FromUserPath> user_sourced{0};

    // Cross-narrow FromUserPath → FromEnvPath: claim the bytes
    // came from getenv() instead of from CLI.  V-232 does NOT admit
    // this transition (provenance lanes are orthogonal); the
    // requires-clause on Tagged::retag fires.
    auto env_sourced =
        std::move(user_sourced).retag<ns::source::FromEnvPath>();
    (void)env_sourced;
    return 0;
}
