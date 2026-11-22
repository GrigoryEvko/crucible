// A function that fills a named snapshot and returns it by value does not
// compile, because the return moves the snapshot.  The vendor and model views
// of each identity name the texts of the facts of the snapshot that holds
// them, so the identities of a moved snapshot would view its source, which
// ends at the return.

#include <crucible/topology/Discovery.h>

#include <fixy/Ctx.h>

namespace {

[[maybe_unused]] crucible::topology::DefaultDiscoverySnapshot filled_snapshot(::fixy::ColdInitCtx const& ctx) {
    auto snapshot = crucible::topology::mint_discovery_snapshot(ctx);
    static_cast<void>(snapshot.add_node(crucible::topology::DiscoveryNodeFact{}));
    return snapshot;
}

}  // namespace
