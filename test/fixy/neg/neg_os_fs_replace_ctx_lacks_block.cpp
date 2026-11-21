// replace_file carries its own requires-clause, CtxFitsReplace, so a context
// without Block is refused at its signature.  The write, the two syncs and the
// rename of a replace each wait on the disk.

#include <fixy/os/Path.h>
#include <fixy/os/Fs.h>

#include <cstddef>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace src = fixy::tags::source;

namespace {
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;

[[maybe_unused]] fixy::Path<src::Sanitized> sanitized(foundation::core::TextView raw) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(raw)).expect("the path is clean");
}
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    [[maybe_unused]] auto r =
        fs::replace_file(ctx, sanitized("/tmp/fixy-neg-replace"), foundation::core::View<std::byte const>{});
    return 0;
}
