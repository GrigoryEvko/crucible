// A write body of a LockedMmap gets the region, and the region gives its mapping
// up only under the key of the end of its lock.  The constructor of the key is
// private to SharedLocked, so the body cannot build one.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

#include <type_traits>

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
    fixy::Option<bool> const is_taken = locked->with_write(ctx, [](auto& region) noexcept -> bool {
        using Region = std::remove_reference_t<decltype(region)>;
        drop(region.leave_lock(fixy::SharedLockEndKey<Region>{}));
        return true;
    });
    return is_taken.is_some() ? 0 : 1;
}
