// The pool of a LockedMmap takes an exclusive of a fresh brand, as the pool of
// SharedLocked does, so a share of it is no share of the pool of another root.
// An exclusive on the erased brand is one type for each root of its tag, and
// the mint refuses it.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

// A tag of a singleton root has a stable identity, so it is in a named
// namespace.
namespace neg_os_locked_mmap {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
}  // namespace neg_os_locked_mmap

namespace {
using neg_os_locked_mmap::MappedRegion;
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap_anon<WriteAnon, Anonymous>(
        ctx, eff::testing::test().alloc, perm::mint_singleton_root<MappedRegion>(), foundation::core::ByteCount{4096});
    return 0;
}
