// The map of a file in a LockedMmap has the gate of mint_mmap.  A shared map
// of a regular file can fault past an end that another process moved, so the
// pack names the atom faults_on_truncate (the end of a shared file of
// fixy/os/OwnedMmap.h).  This pack names no such atom.
//
// The descriptor is -1, and nothing here is ever run.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

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
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap<ReadOnly, Shared>(
        ctx, eff::testing::test().alloc, perm::mint_permission_root<FileRegion>(), file.ref(),
        foundation::core::ByteCount{4096});
    return 0;
}
