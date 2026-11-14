// mint_file takes Path<Sanitized>.  A path minted at the trust
// boundary carries tags::source::External until it has been through the
// sanitizer, and there is no conversion between the two tags: the
// laundering is the only edge.

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
    [[maybe_unused]] auto r = fs::mint_file<atom_fs::mode<fs::open_mode::ReadOnly>>(
        ctx, fixy::mint_tagged<src::External>(foundation::os::PathText<fixy::kPathBytes>{}));
    return 0;
}
