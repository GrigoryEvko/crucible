// A read body of a LockedMmap gives back a View of the pages.  The caller
// would read the pages after the share ends, while a discard can zero them.
// The door refuses a body whose result holds a borrow.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

#include <cstdint>

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
    auto escaped =
        locked->with_read(ctx, [](auto const& region) noexcept { return region.template view<std::uint8_t const>(); });
    return 0;
}
