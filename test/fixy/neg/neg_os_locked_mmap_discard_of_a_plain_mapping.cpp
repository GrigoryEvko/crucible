// Two mappings of one mint site have one type.  A door that took a plain
// mapping and an exclusive of its tag and brand let the exclusive of the first
// mapping discard the pages of the second, while a reader held a share of the
// pool of the second.  So no such door exists.  The release-aware advice is a
// member of LockedMmap, which holds the mapping and its pool in one object.

#include <fixy/os/LockedMmap.h>

#include <foundation/Linear.h>
#include <foundation/permissions/SharedPermission.h>

#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {

struct MappedRegion final {
    using permission_row = eff::Row<>;
};
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;

// The second run discards the pages of its mapping with the token of the
// first run.
template <class... First>
void discard_across_mappings(First&&... first) {
    IoBlockCtx ctx{eff::testing::test()};
    auto token = perm::mint_permission_root<MappedRegion>();
    auto region = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, token, 4096).expect("one page").consume();
    if constexpr (sizeof...(First) == 0) {
        discard_across_mappings(token);
    } else {
        perm::SharedPermissionPool pool{std::move(token)};
        fixy::mmap::advise_release_aware(ctx, region, std::forward<First>(first)...);
    }
}

}  // namespace

int main() {
    discard_across_mappings();
    return 0;
}
