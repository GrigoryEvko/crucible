// A map of a character device can insert its pages at the map and have no
// fault door, and a discard of such a page ends the process at the next read
// (the test of a foreign object of fixy/os/LockedMmap.h).  So the map of a
// file takes no borrow of a character device.

#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
struct DeviceRegion final {
    using permission_row = eff::Row<>;
};
// Admits IO and Block: the row of a mapping.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Private = fixy::atom::mmap::with_share<fixy::mmap::share::Private>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<DeviceRegion>();
    fs::CharacterDevice<fs::DeviceAccess::read_only> const device{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Private>(ctx, owner, device.ref(), 4096);
    return 0;
}
