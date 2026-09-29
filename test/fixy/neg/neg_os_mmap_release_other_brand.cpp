// Two regions of one tag are two instances.  Each root mint brands its
// permission with a fresh brand, and the mapping takes the brand of the
// permission it was minted with.  A permission of the second instance
// says nothing about the readers of the first, so the release gate
// compares the brand of the permission with the brand of the mapping,
// and it refuses the call although the two tags agree.
//
// A mapping that carried no brand would let this call compile.
//
// The mapping is the empty region at the brand of its own permission,
// so nothing here maps memory.

#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<MappedRegion>();
    auto const sibling = perm::mint_permission_root<MappedRegion>();
    fixy::OwnedMmap<MappedRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous,
                    foundation::brand::brand_of_t<decltype(owner)>>
        mapping{};
    [[maybe_unused]] auto refused =
        fixy::mmap::advise_release_aware<fixy::mmap::advice::DontNeed>(ctx, mapping, sibling);
    return 0;
}
