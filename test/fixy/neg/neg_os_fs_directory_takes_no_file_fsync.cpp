// The sync of a file takes the borrow of a file, and only
// sync<FsyncParentDir> flushes the entries of a directory.  The borrow of a
// directory is no borrow of a file, so the fsync of a file does not take it.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::DirectoryHandle const dir{};
    [[maybe_unused]] auto flushed = fs::sync<fs::sync_op::Fsync>(ctx, dir.ref());
    return 0;
}
