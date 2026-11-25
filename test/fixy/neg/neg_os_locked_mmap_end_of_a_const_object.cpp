// The end of the lock of a LockedMmap takes the mapping out of the object, so
// it changes the object.  A caller that holds the object as const cannot end
// its lock.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                eff::Effect::Block, eff::Effect::Nondet>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    MapCtx const ctx{eff::testing::test()};
    auto locked = fixy::mmap::mint_locked_mmap_anon<WriteAnon, Anonymous>(ctx, eff::testing::test().alloc,
                                                                          perm::mint_permission_root<MappedRegion>(),
                                                                          foundation::core::ByteCount{4096})
                      .expect("one page");
    auto const& object = *locked;
    static_cast<void>(object.end_lock(ctx));
    return 0;
}
