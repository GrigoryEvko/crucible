// A map can park the caller on memory pressure, so the atoms of the pack lift
// to IO and Block, and the context of mint_locked_mmap must admit that row.
// This context owns IO and not Block, so the mint refuses it.
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
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
using Faults = fixy::atom::mmap::faults_on_truncate;
}  // namespace

int main() {
    IoOnlyCtx ctx{eff::testing::test()};
    fixy::fs::TemporaryFile const file{};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap<ReadOnly, Shared, Faults>(
        ctx, eff::testing::test().alloc, perm::mint_permission_root<FileRegion>(), file.ref(),
        foundation::core::ByteCount{4096});
    return 0;
}
