// A map of a descriptor of a C library can park the caller, as the other maps
// do.  This context owns IO and not Block, so mint_locked_mmap_foreign refuses
// it.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap_foreign<ReadOnly, Shared>(
        ctx, eff::testing::test().alloc, perm::mint_permission_root<MappedRegion>(), fixy::mmap::ForeignDescriptor{3},
        foundation::core::ByteCount{4096});
    return 0;
}
