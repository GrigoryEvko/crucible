// A private map of a regular file shows the writes of another process and
// faults past an end that another process moved, so the region door needs the
// licence unchanged_file (the private map of a file of fixy/os/OwnedMmap.h).

#include <fixy/os/Fs.h>
#include <fixy/os/OwnedMmap.h>
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
    using PrivateRegion = fixy::OwnedMmap<Region, fixy::mmap::prot::ReadOnly, fixy::mmap::share::Private, Brand>;
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = PrivateRegion::mint_region(ctx, owner, file.ref(), 4096, fixy::fs::FileOffset{0});
    return 0;
}
