// A copy of a discovery snapshot does not compile.  The vendor and model
// views of each identity name the texts of the facts of the snapshot that
// holds them, so the identities of a copy would view its source.

#include <crucible/topology/Discovery.h>

#include <fixy/Ctx.h>

namespace {

[[maybe_unused]] bool copies(::fixy::ColdInitCtx const& ctx) {
    auto const snapshot = crucible::topology::mint_discovery_snapshot(ctx);
    auto const copy = snapshot;
    return copy.node_count() == 0;
}

}  // namespace
