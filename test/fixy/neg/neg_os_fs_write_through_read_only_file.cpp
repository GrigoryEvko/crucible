// A file of a read-only open holds no write right (the rights of
// fixy/os/Handle.h).  So the full write, which asks for the write right,
// takes no borrow of it, and the error is at the call and not in the kernel.

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
    [[maybe_unused]] auto written = fs::write_full(ctx, file.ref(), foundation::core::TextView{"bytes"});
    return 0;
}
