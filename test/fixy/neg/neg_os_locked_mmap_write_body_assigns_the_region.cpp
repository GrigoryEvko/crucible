// A write body of a LockedMmap gets a region that takes no assignment.  A body
// that puts a mapping of the same type into the lock brings the Views that the
// caller took of that mapping before the lock, and a discard zeroes the pages
// under them.  The region is pinned and gives no reference to its mapping, so
// the assignment does not compile.

#include <fixy/os/LockedMmap.h>

#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                eff::Effect::Block, eff::Effect::Nondet>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    MapCtx const ctx{eff::testing::test()};
    auto token = perm::mint_permission_root<MappedRegion>();
    auto plain = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, token, 4096).expect("one page").consume();
    auto locked = fixy::mmap::mint_locked_mmap_anon<WriteAnon, Anonymous>(
                      ctx, eff::testing::test().alloc, std::move(token), foundation::core::ByteCount{4096})
                      .expect("one page");
    bool const is_taken =
        locked->with_write(ctx, [&plain](auto& region) noexcept { region = std::move(plain); }).is_some();
    return is_taken ? 0 : 1;
}
