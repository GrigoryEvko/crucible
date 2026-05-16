// ── neg_fixy_federation_stance_version_disjoint (FIXY-G13 HS14) ───────
//
// Pin temporal grade stability across federation: two peers with
// disjoint accept_versions windows fail at mint_federation_channel_
// versioned construction time.  Peer A accepts [1,2]; peer B accepts
// [4,5]; the meet is empty → static_assert fires in
// VersionedFederationChannel's body.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cs = crucible::fixy::stance;

namespace {

using DisjointA = cs::accept_versions<cs::BgWorkerTag, 1, 2>;
using DisjointB = cs::accept_versions<cs::BgWorkerTag, 4, 5>;

// Sanity: the predicate reports disjoint windows.
static_assert(!cf::federation_version_windows_compatible_v<DisjointA, DisjointB>);

// THE DISCIPLINE: instantiating VersionedFederationChannel<...> with
// an empty intersection fires the static_assert inside the channel's
// body.  sizeof forces instantiation.
using DisjointChannel = cf::VersionedFederationChannel<DisjointA, DisjointB>;
[[maybe_unused]] constexpr std::size_t kSize = sizeof(DisjointChannel);

}  // namespace

int main() { return 0; }
