// The policy mint refuses the foreground context.  The hot path owns
// neither IO nor Block, and mbind(2) can park the caller while it moves the
// pages of the region.
//
// The context, the region and the mask are taken by reference, so nothing
// here builds one.

#include <fixy/os/NumaPlacement.h>

namespace eff = foundation::effects;

namespace {
struct Region final {};
using AnonRegion = fixy::NumaBindableRegion<Region, fixy::mmap::prot::WriteCopy>;
using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(ForegroundCtx const& ctx, fixy::Linear<AnonRegion>&& region,
                              fixy::NumaNodeMask const& nodes) {
    [[maybe_unused]] auto refused =
        fixy::numa::mint_numa_policy<fixy::NumaPolicy::preferred>(ctx, std::move(region), nodes);
}
}  // namespace

int main() { return 0; }
