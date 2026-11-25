// The int of a C library is no ForeignDescriptor: the caller states at the call
// that the int is a descriptor that a C library opened.  So a length or an
// offset never goes to the map as a descriptor.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto refused = fixy::mmap::mint_locked_mmap_foreign<ReadOnly, Shared>(
        ctx, eff::testing::test().alloc, perm::mint_permission_root<MappedRegion>(), 3,
        foundation::core::ByteCount{4096});
    return 0;
}
