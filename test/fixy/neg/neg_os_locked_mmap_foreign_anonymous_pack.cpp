// A map of a descriptor of a C library is a map of an object, so its pack names
// no anonymous share.  The gate of mint_locked_mmap_foreign refuses the pack.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap_foreign<WriteAnon, Anonymous>(
        ctx, eff::testing::test().alloc, perm::mint_permission_root<MappedRegion>(), fixy::mmap::ForeignDescriptor{3},
        foundation::core::ByteCount{4096});
    return 0;
}
