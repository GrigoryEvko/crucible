// The mint refuses the foreground context.  Its row is empty, so it owns
// neither IO nor Block, and a binding that moves pages must not run on
// the hot path.
//
// The context and the region are taken by reference, so nothing here
// builds one.

#include <fixy/os/NumaPlacement.h>

namespace eff = foundation::effects;

namespace {
struct Region final {};
using AnonRegion = fixy::NumaBindableRegion<Region, fixy::mmap::prot::WriteCopy>;
using ForegroundCtx = eff::ExecCtx<>;

[[maybe_unused]] void attempt(ForegroundCtx const& ctx, fixy::Linear<AnonRegion>&& region) {
    [[maybe_unused]] auto refused = fixy::numa::mint_numa_placement(ctx, std::move(region), fixy::NumaNodeId{0});
}
}  // namespace

int main() { return 0; }
