// The atoms of an anonymous map lift to IO and Block, as the atoms of a map of
// a file do.  This context owns IO and not Block, so mint_locked_mmap_anon
// refuses it.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap_anon<WriteAnon, Anonymous>(
        ctx, eff::testing::test().alloc, perm::mint_permission_root<MappedRegion>(), foundation::core::ByteCount{4096});
    return 0;
}
