// The warm writer opens a file, so its gate folds the stance atoms into
// CtxFitsFileMint and a context without Block is refused.

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
    IoOnlyCtx ctx{eff::testing::init()};
    [[maybe_unused]] auto r = durable::mint_warm_writer<>(ctx, sanitized("/tmp/cipher-neg-warm"));
    return 0;
}
