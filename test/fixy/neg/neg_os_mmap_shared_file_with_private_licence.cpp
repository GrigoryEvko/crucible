// The atom unchanged_file is the licence of a private map of a regular file.  A
// shared map of a file that another process makes shorter still ends the process
// with SIGBUS, and faults_on_truncate states that risk, so unchanged_file alone
// gives no permission of a shared map (the private map of a file of
// fixy/os/OwnedMmap.h).

#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
using Unchanged = fixy::atom::mmap::unchanged_file;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<FileRegion>();
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Shared, Unchanged>(ctx, owner, file.ref(), 4096);
    return 0;
}
