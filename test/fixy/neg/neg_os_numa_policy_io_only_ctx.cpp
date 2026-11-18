// The policy mint refuses a context that owns IO but not Block.  mbind(2)
// moves the pages that the region holds, so it can park the caller, and the
// gate is the gate of the bind mint: the context must own IO and Block.
//
// The context, the region and the mask are taken by reference, so nothing
// here builds one.

#include <fixy/os/NumaPlacement.h>

namespace eff = foundation::effects;

namespace {
struct Region final {};
using AnonRegion = fixy::NumaBindableRegion<Region, fixy::mmap::prot::WriteCopy>;
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;

[[maybe_unused]] void attempt(IoOnlyCtx const& ctx, fixy::Linear<AnonRegion>&& region,
                              fixy::NumaNodeMask const& nodes) {
    [[maybe_unused]] auto refused =
        fixy::numa::mint_numa_policy<fixy::NumaPolicy::interleave>(ctx, std::move(region), nodes);
}
}  // namespace

int main() { return 0; }
