// A file of an append open holds the write and the sync rights, and no read
// right and no seek right (the rights of fixy/os/Handle.h).  A read at an
// offset asks for the read and the seek rights, so read_full_at takes no
// borrow of such a file.  A reader of the file opens it again for reading.

#include <fixy/os/Fs.h>
#include <foundation/core/Array.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
// Admits IO and Block: the row of each call of the file surface.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using WriteAppend = fixy::atom::fs::mode<fs::open_mode::WriteAppend>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    fs::FileOf<WriteAppend> const file{};
    foundation::core::FixedArray<std::byte, 4> buffer{};
    buffer.with_mut([&ctx, &file](auto run) noexcept {
        [[maybe_unused]] auto read = fs::read_full_at(ctx, file.ref(), std::move(run), fs::FileOffset{0});
    });
    return 0;
}
