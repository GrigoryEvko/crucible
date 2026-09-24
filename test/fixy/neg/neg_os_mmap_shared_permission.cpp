// A mapping takes the identity of the exclusive permission it is minted
// with, and a discard of its pages later asks for that exclusive.  A
// reader holds a guard on a share that the pool of the region lent it.
// The guard names the same region, but a mapping minted with it would
// name an identity whose exclusive the pool still holds.  The mint takes a
// Permission and nothing else, so the guard does not deduce, and the call
// is refused.
//
// The descriptor is -1.  The fixture is compiled and never run, so the
// pool never ends with the share still out.

#include <fixy/os/Mmap.h>

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
    perm::SharedPermissionPool pool{perm::mint_permission_root<FileRegion>()};
    auto guard = pool.lend();
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Shared>(ctx, *guard, -1, 4096);
    return 0;
}
