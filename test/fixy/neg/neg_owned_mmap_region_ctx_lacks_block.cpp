// A mapping can park the caller on page-cache pressure, on a NUMA-remote
// page fault and on write-back, so the door of a region asks for a
// context that owns IO and Block.  A context with IO and no Block is
// refused.

#include <fixy/OwnedMmap.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>

namespace eff = foundation::effects;

namespace {
struct Region final {
    using permission_row = eff::Row<>;
};
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx const ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<Region>();
    using Brand = std::remove_cvref_t<decltype(owner)>::brand_type;
    using PrivateRegion = fixy::OwnedMmap<Region, fixy::mmap::prot::ReadOnly, fixy::mmap::share::Private, Brand>;
    [[maybe_unused]] auto refused = PrivateRegion::mint_region(ctx, owner, -1, 4096, 0);
    return 0;
}
