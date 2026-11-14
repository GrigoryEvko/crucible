// A Dirfd gives no reference to the descriptor that it holds.  So the sync of
// a descriptor takes no directory, and only sync<FsyncParentDir> flushes the
// entries of a directory: the call names a member that Dirfd does not have.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::Dirfd const dir{};
    [[maybe_unused]] auto flushed = fs::sync<fs::sync_op::Fsync>(ctx, dir.handle());
    return 0;
}
