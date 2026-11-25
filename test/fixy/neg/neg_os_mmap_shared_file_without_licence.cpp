// Another process can make a regular file shorter while a shared mapping of
// it lives, and an access past the new end ends the process with SIGBUS.  So
// the shared mapping of a regular file needs the atom faults_on_truncate,
// which states that risk at the call (the end of a shared file of
// fixy/os/OwnedMmap.h).  This pack names no such atom.
//
// The descriptor is -1, and nothing here is ever run.

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
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<FileRegion>();
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Shared>(ctx, owner, file.ref(), 4096);
    return 0;
}
