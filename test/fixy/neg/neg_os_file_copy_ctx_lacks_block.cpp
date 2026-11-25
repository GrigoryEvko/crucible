// A copy of a file waits for the disk, so its context admits IO and Block.

#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<FileRegion>();
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused =
        fixy::mmap::mint_file_copy(ctx, owner, file.ref(), foundation::core::ByteCount{4096});
    return 0;
}
