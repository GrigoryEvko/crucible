// The mapping that leaves a LockedMmap at the end of its lock is a plain
// OwnedMmap.  It has no pool, so no door asks if a reader reads it, and no door
// discards its pages.

#include <fixy/os/LockedMmap.h>

#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>

#include <utility>

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
    auto [mapping, exclusive] = locked->end_lock(ctx).expect("no reader is in the region");
    auto region = std::move(mapping).consume();
    foundation::linear::drop(std::move(exclusive));
    bool const is_discarded = region.advise_release_aware(ctx).is_ok();
    return is_discarded ? 0 : 1;
}
