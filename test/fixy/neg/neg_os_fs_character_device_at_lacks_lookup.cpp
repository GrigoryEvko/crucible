// mint_character_device_at looks its path up beneath a directory, so the
// borrow of the directory holds the lookup right.  A borrow with the sync
// right alone is refused at the requires-clause.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of the call.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::DirectoryHandle const dir{};
    [[maybe_unused]] auto device = fs::mint_character_device_at<fs::DeviceAccess::read_only>(
        ctx, dir.ref().restrict<fixy::os::RightSet::sync>(), "null");
    return 0;
}
