// A reader holds a share lent by the pool of a region, and the pool holds
// the exclusive.  An anonymous mapping takes the identity of the
// exclusive permission it is minted with, and a share is not that
// permission.  The mint takes a Permission and nothing else, so the share
// does not deduce, and the call is refused.
//
// The fixture is compiled and never run, so the pool never ends with the
// share still out.

#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct AnonRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    perm::SharedPermissionPool pool{perm::mint_permission_root<AnonRegion>()};
    auto guard = pool.lend();
    auto const share = guard->token();
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, share, 4096);
    return 0;
}
