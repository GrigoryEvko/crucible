// A character device has no size and is no file.  Its borrow has a kind of its
// own, so a door of a file, such as file_size, takes no borrow of it.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::CharacterDevice<fs::DeviceAccess::read_only> const device{};
    [[maybe_unused]] auto size = fs::file_size(ctx, device.ref());
    return 0;
}
