// The constructor of a LockedMmap takes a key that only the door of the mints
// builds.  So no code puts a mapping that it already holds, with the Views that
// it took of it, in a LockedMmap.

#include <fixy/os/LockedMmap.h>

#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
}  // namespace

int main() {
    auto exclusive = perm::mint_permission_root<MappedRegion>();
    using Locked = fixy::LockedMmap<MappedRegion, fixy::mmap::prot::WriteCopy, fixy::mmap::share::Anonymous,
                                    foundation::brand::brand_of_t<decltype(exclusive)>>;
    Locked forged{fixy::mmap::LockedMmapKey{}, std::move(exclusive), Locked::mapping_type{}};
    return 0;
}
