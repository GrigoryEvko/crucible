// The kernel ignores the memory policy of a shared range and gives success for
// the call.  So the region of a shared mapping sets no policy.

#include <fixy/os/LockedMmap.h>

#include <fixy/os/Fs.h>
#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                eff::Effect::Block, eff::Effect::Nondet>>;
using ReadWrite = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadWrite>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
}  // namespace

int main() {
    MapCtx const ctx{eff::testing::test()};
    auto memory = fixy::fs::mint_sealed_memory(ctx, fixy::fs::FileOffset{4096}).expect("one page").consume();
    auto locked = fixy::mmap::mint_locked_mmap<ReadWrite, Shared>(ctx, eff::testing::test().alloc,
                                                                  perm::mint_permission_root<MappedRegion>(),
                                                                  memory.ref(), foundation::core::ByteCount{4096})
                      .expect("a shared mapping of the memory");
    fixy::Option<bool> const is_set = locked->with_write(ctx, [ctx](auto& region) noexcept -> bool {
        return region.template set_memory_policy<fixy::NumaPolicy::bind>(ctx, fixy::NumaNodeMask{}).is_ok();
    });
    return is_set.is_some() ? 0 : 1;
}
