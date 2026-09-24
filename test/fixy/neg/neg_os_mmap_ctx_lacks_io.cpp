// A mapping of a file reads the file through the page cache, so the
// atoms of the mapping lift to IO and Block, and the context must admit
// that row.  This context owns Block and not IO, so the mint refuses it
// at the row clause of its gate.
//
// The descriptor is -1, and nothing here is ever run.

#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
using BlockOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Block>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
}  // namespace

int main() {
    BlockOnlyCtx ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<FileRegion>();
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Shared>(ctx, owner, -1, 4096);
    return 0;
}
