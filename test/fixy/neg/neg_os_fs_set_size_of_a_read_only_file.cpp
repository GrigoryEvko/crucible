// A file of a read-only open holds no write right (the rights of
// fixy/os/Handle.h).  A change of the size changes the bytes of the file, so
// set_file_size asks for the write right and takes no borrow of such a file.

#include <fixy/os/Fs.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::FileOf<fs::read_only> const file{};
    [[maybe_unused]] auto cut = fs::set_file_size(ctx, file.ref(), fs::FileOffset{0});
    return 0;
}
