// A mapping spelled without a brand is on the erased identity, and every
// mapping of its tag on that identity is one type.  A proof about one of
// them is then a proof about all of them.  An erased permission agrees
// with an erased mapping, so the brand clause alone cannot refuse it.
// Before the release gate compares the brands, it asks that the mapping
// has a brand, and it refuses this call on that clause.
//
// The permission here has a brand of its own, so that only the clause
// under test can fail first.  The mapping is the empty region, so
// nothing here maps memory.

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
    fixy::OwnedMmap<MappedRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous> mapping{};
    [[maybe_unused]] auto refused = fixy::mmap::advise_release_aware<fixy::mmap::advice::DontNeed>(ctx, mapping, owner);
    return 0;
}
