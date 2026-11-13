// mint_file_at takes the atom pack of mint_file, and the gate asks for
// exactly one mode atom.  An empty pack names no mode, so the open would
// have no access mode, and the requires-clause refuses it.

#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of the call.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::Dirfd const dir{};
    [[maybe_unused]] auto r = fs::mint_file_at<>(ctx, dir, "objects");
    return 0;
}
