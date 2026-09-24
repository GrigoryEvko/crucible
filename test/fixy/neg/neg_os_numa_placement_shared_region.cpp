// The mint refuses a shared mapping.  mbind(2) returns success for a
// MAP_SHARED range, but the kernel ignores the policy of that range, so a
// proof over it would state a binding that the kernel does not keep.  The
// mint takes only a region whose share mode is share::Anonymous.
//
// The region is taken by reference, so nothing here maps memory.

#include <fixy/os/NumaPlacement.h>

namespace eff = foundation::effects;

namespace {
struct Region final {};
using SharedRegion = fixy::OwnedMmap<Region, fixy::mmap::prot::ReadWrite, fixy::mmap::share::Shared>;
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

[[maybe_unused]] void attempt(IoBlockCtx const& ctx, fixy::Linear<SharedRegion>&& region) {
    [[maybe_unused]] auto refused = fixy::numa::mint_numa_placement(ctx, std::move(region), fixy::NumaNodeId{0});
}
}  // namespace

int main() { return 0; }
