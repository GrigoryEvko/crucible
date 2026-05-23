// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-245 HS14 fixture 1/3 — the LOAD-BEARING recurses discipline.
//
// `grant::dispatch::recurses<MaxDepth>` carries NO default template
// argument: every recursion grant MUST declare a proven worst-case
// depth.  A bare `grant::dispatch::recurses<>` (no bound) is ill-formed
// — "wrong number of template arguments".
//
// This is the central premise of the recurses grant: a content-addressed
// DAG walk (MerkleDag::merkle_hash) has an implicit acyclic depth bound;
// surfacing it as recurses<32> makes the ceiling auditable.  Dropping the
// mandatory-bound discipline (adding a default) would let a recursion
// grant compile with no declared ceiling — the unbounded-stack hazard the
// grant exists to forbid (and the D002 composition rule guards).
//
// Mismatch class for HS14 audit: template-argument ARITY (missing a
// non-defaulted NTTP) — distinct from the virtual_call value-where-type
// (fixture 2) and the IsGrantTag cv-purity (fixture 3) paths.
//
// Expected diagnostic: a GCC template-argument-count error.

#include <crucible/fixy/grant/Dispatch.h>

namespace disp = crucible::fixy::grant::dispatch;

// Missing the mandatory MaxDepth bound — recurses has no default NTTP.
using Bad = disp::recurses<>;

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
