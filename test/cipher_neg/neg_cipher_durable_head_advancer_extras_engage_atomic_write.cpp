// head_advance_stance pins the commit to RenameAt2NoReplace.  An extra
// that engages the atomicity axis is refused at the stance rule.  The
// extra here names Rename, and any other atomicity tag is the same test.

#include <crucible/cipher/CipherDurable.h>

#include <fixy/os/Path.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>

#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace durable = crucible::cipher::durable;
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
    [[maybe_unused]] auto r = durable::mint_head_advancer<atom_fs::atomic_write<fs::atomicity::Rename>>(
        ctx, sanitized("/tmp/cipher-neg-head"));
    return 0;
}
