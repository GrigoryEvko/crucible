// An executable region needs the trusted_jit atom, which states that the
// caller audited the bytes that will run.  The door calculates the PROT_*
// word from the type, so an executable page comes only from a region
// typed prot::Exec, and that type asks for the licence.

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
    using ExecRegion = fixy::OwnedMmap<Region, fixy::mmap::prot::Exec, fixy::mmap::share::Private, Brand>;
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = ExecRegion::mint_region(ctx, owner, file.ref(), 4096, fixy::fs::FileOffset{0});
    return 0;
}
