// sync<FsyncParentDir> flushes the entries of a directory, so it takes the
// borrow of a directory.  An fsync of a file flushes that file and no entry
// of its directory, so the gate refuses the borrow of a file.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::TemporaryFile const file{};
    [[maybe_unused]] auto flushed = fs::sync<fs::sync_op::FsyncParentDir>(ctx, file.ref());
    return 0;
}
