// A discard of the pages of a mapping takes the exclusive permission of
// that mapping.  A permission of another region names another identity,
// and to hold it says nothing about the readers of this mapping.  The
// release gate compares the tag of the permission with the tag of the
// mapping, and it refuses the call.
//
// Before the gate read the tag, this call compiled and discarded the
// pages of a mapping that its caller had no proof over.
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
struct OtherRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<MappedRegion>();
    auto const other = perm::mint_permission_root<OtherRegion>();
    fixy::OwnedMmap<MappedRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous,
                    foundation::brand::brand_of_t<decltype(owner)>>
        mapping{};
    [[maybe_unused]] auto refused = fixy::mmap::advise_release_aware<fixy::mmap::advice::DontNeed>(ctx, mapping, other);
    return 0;
}
