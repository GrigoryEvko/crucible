// A file of a write-only open holds no map right (the source of a map of
// fixy/os/OwnedMmap.h), because a map reads the file.  So the map of a file
// takes no borrow of it, and the error is at the call and not in the kernel.

#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace fs = fixy::fs;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
// Admits IO and Block: the row of a mapping.
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
using WriteTruncate = fixy::atom::fs::mode<fs::open_mode::WriteTruncate>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<FileRegion>();
    fs::FileOf<WriteTruncate> const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Shared>(ctx, owner, file.ref(), 4096);
    return 0;
}
