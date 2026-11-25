// The end of the lock of a LockedMmap fails while a reader or a writer is in
// the region.  The outcome depends on the other threads, so the context owns
// Nondet.  This context owns the rows of the mint and no Nondet.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using MapCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    MapCtx const ctx{eff::testing::test()};
    auto locked = fixy::mmap::mint_locked_mmap_anon<WriteAnon, Anonymous>(ctx, eff::testing::test().alloc,
                                                                          perm::mint_permission_root<MappedRegion>(),
                                                                          foundation::core::ByteCount{4096})
                      .expect("one page");
    static_cast<void>(locked->end_lock(ctx));
    return 0;
}
