// mint_character_device takes Path<Sanitized>.  A path from outside carries
// tags::source::External until the sanitizer passed it, and no conversion
// joins the two tags.

#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/os/PathText.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {
// Admits IO and Block: the row of the call.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto device = fs::mint_character_device<fs::DeviceAccess::read_only>(
        ctx, fixy::mint_tagged<src::External>(foundation::os::PathText<fixy::kPathBytes>{}));
    return 0;
}
