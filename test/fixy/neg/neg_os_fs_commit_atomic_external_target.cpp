// Both paths a commit takes are Path<Sanitized>.  The target is the
// one a reader will open by name afterwards, so an unsanitized target
// is refused with the same tag mismatch as an unsanitized open.

#include <fixy/os/Path.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>

#include <foundation/os/PathText.h>

#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace atom_fs = fixy::atom::fs;
namespace src = fixy::tags::source;

namespace {
// Admits IO and Block: the row every call here needs.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;

[[maybe_unused]] fixy::Path<src::Sanitized> sanitized(foundation::core::TextView raw) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(raw)).expect("the path is clean");
}
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto r = fs::commit_atomic<fs::atomicity::Rename>(
        ctx, sanitized("/tmp/fixy-neg-fs.tmp"),
        fixy::mint_tagged<src::External>(foundation::os::PathText<fixy::kPathBytes>{}));
    return 0;
}
