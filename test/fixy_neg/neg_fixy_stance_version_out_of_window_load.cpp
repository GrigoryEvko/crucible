// ── neg_fixy_stance_version_out_of_window_load (FIXY-G13 HS14) ────────
//
// Pin temporal grade stability: a consumer with
// `accept_versions<BgWorkerTag, 2, 3>` cannot load a v1 artifact.
// The decoder returns `WireGradeError::StanceVersionUnsupported`.
//
// This is a RUNTIME failure on the wire-decode path; for HS14
// compile-time pinning, we instead exercise the type-level invariant:
// `accept_versions<Tag, Lo, Hi>` with `Lo > Hi` is rejected at the
// type-construction site by the static_assert in accept_versions.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>

namespace cs = crucible::fixy::stance;

namespace {

// Lo > Hi: accept_versions's static_assert fires at construction.
using BadWindow = cs::accept_versions<cs::BgWorkerTag, 5, 1>;
[[maybe_unused]] constexpr std::size_t kBadSize = sizeof(BadWindow);

}  // namespace

int main() { return 0; }
