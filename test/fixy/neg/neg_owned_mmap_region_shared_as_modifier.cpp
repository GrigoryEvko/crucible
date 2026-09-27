// A region typed share::Anonymous claims a private anonymous range, and
// fixy::numa::mint_numa_placement reads that claim off the type.  The
// door of the region calculates its MAP_* word from the type, so no
// caller gives it MAP_SHARED.  A primary share tag is part of the type and
// is not a modifier, so a shared range cannot pass for an anonymous one.

#include <fixy/OwnedMmap.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>

namespace eff = foundation::effects;

namespace {
struct Region final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<Region>();
    using Brand = std::remove_cvref_t<decltype(owner)>::brand_type;
    using AnonRegion = fixy::OwnedMmap<Region, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous, Brand>;
    [[maybe_unused]] auto refused = AnonRegion::mint_region<fixy::mmap::share::Shared>(ctx, owner, -1, 4096, 0);
    return 0;
}
