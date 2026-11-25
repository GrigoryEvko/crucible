// A character device of a read_only open holds no write right (the character
// device of fixy/os/Fs.h), so the full write takes no borrow of it.

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
    [[maybe_unused]] auto written = fs::write_full(ctx, device.ref(), foundation::core::TextView{"bytes"});
    return 0;
}
