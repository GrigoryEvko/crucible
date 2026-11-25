// The copy of a file is read-only, so a region of a protection that writes
// takes no copy (the private map of a file of fixy/os/OwnedMmap.h).

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
    using WritableRegion = fixy::OwnedMmap<Region, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous, Brand>;
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = WritableRegion::mint_file_copy(
        ctx, owner, file.ref(), foundation::core::ByteCount{4096}, fixy::fs::FileOffset{0});
    return 0;
}
