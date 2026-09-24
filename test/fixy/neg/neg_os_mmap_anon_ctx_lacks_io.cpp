// The atoms of an anonymous mapping lift to IO and Block, because the
// mapping call can park the caller on page-table and memory pressure.  The
// context must admit that row.  This context owns Block and not IO, so the
// mint refuses it at the row clause of its gate.
//
// Nothing here is ever run.

#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct AnonRegion final {
    using permission_row = eff::Row<>;
};
using BlockOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Block>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    BlockOnlyCtx ctx{eff::testing::test()};
    auto const owner = perm::mint_permission_root<AnonRegion>();
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, 4096);
    return 0;
}
