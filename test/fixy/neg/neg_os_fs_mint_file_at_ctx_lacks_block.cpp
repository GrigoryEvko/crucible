// mint_file_at opens a path under a directory with the gate of mint_file.
// The open can park until the disk answers, so the gate asks for a context
// that admits Block as well as IO.  A context that holds IO alone is
// refused at the requires-clause.

#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace atom_fs = fixy::atom::fs;

namespace {
// Admits IO but NOT Block: the row of the cold init context.
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::init()};
    fs::Dirfd const dir{};
    [[maybe_unused]] auto r = fs::mint_file_at<atom_fs::mode<fs::open_mode::ReadOnly>>(ctx, dir, "objects");
    return 0;
}
