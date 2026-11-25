// A copy of a file reads the file at an offset, so it needs the read and the
// seek right of the handle.  A borrow with the map right alone gives no copy.

#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<FileRegion>();
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_file_copy(
        ctx, owner, file.ref().restrict<fixy::os::RightSet::map>(), foundation::core::ByteCount{4096});
    return 0;
}
