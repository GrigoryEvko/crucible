// mint_character_device_at opens a device node beneath a directory, and the
// open can wait for the device.  So the mint asks for a context that admits
// Block as well as IO, and a context that holds IO alone is refused at the
// requires-clause.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    fs::DirectoryHandle const dir{};
    [[maybe_unused]] auto device = fs::mint_character_device_at<fs::DeviceAccess::read_only>(ctx, dir.ref(), "null");
    return 0;
}
