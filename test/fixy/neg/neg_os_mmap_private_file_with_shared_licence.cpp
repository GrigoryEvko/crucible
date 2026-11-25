// The atom faults_on_truncate states the risk of the size of a shared file, and
// it says nothing of the bytes.  A private map of a regular file also shows the
// writes of another process, so that atom is no licence of it (the private map
// of a file of fixy/os/OwnedMmap.h).

#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Private = fixy::atom::mmap::with_share<fixy::mmap::share::Private>;
using Faults = fixy::atom::mmap::faults_on_truncate;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto const owner = foundation::permissions::mint_permission_root<FileRegion>();
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Private, Faults>(ctx, owner, file.ref(), 4096);
    return 0;
}
