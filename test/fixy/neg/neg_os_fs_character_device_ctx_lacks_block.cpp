// mint_character_device opens a device node, and the open can wait for the
// device.  So the mint asks for a context that admits Block as well as IO, and
// a context that holds IO alone is refused at the requires-clause.

#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>

#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    fixy::Path<src::Sanitized> const path =
        fixy::sanitize_path(fixy::mint_tagged<src::External>(foundation::core::TextView{"/dev/null"}))
            .expect("the path is clean");
    [[maybe_unused]] auto device = fs::mint_character_device<fs::DeviceAccess::read_only>(ctx, path);
    return 0;
}
