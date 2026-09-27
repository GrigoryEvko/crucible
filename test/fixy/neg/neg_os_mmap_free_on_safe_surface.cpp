// advice::Free lets the kernel discard the pages when memory is short,
// before the next write, and a read of a discarded page gives zeros.  So
// a reader of the region between the advice and the next write reads
// zeros or the old bytes, as it does after advice::DontNeed.  The plain
// advise surface refuses each advice that discards pages, and the
// release-aware surface takes the exclusive permission of the mapping.
//
// The mapping is the empty region, so nothing here maps memory.

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
    fixy::OwnedMmap<MappedRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous,
                    foundation::brand::brand_of_t<decltype(owner)>>
        mapping{};
    [[maybe_unused]] auto refused = fixy::mmap::advise<fixy::mmap::advice::Free>(ctx, mapping);
    return 0;
}
