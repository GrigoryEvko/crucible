// A read of a region on the erased brand, built from the singleton root of
// its tag.  On the erased brand every region of the tag is one type, so the
// deduction can pair a guard with any region of the tag.  The gate asks for
// a fresh brand.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>

// The region holds the singleton root of the tag, so the tag has a stable
// identity and external linkage.
namespace neg_shared_read_on_the_erased_brand_types {
struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_shared_read_on_the_erased_brand_types

using neg_shared_read_on_the_erased_brand_types::Cache;

namespace {
using BgCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Nondet>>;
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    BgCtx ctx{::foundation::effects::testing::bg()};

    ::fixy::SharedRegion<int, Cache> shared{
        ::fixy::OwnedRegion<int, Cache>::adopt(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                               ::foundation::permissions::mint_singleton_root<Cache>())};
    auto guard = shared.lend(ctx).expect("an idle region lends a share");
    auto read = ::fixy::mint_shared_read(ctx, guard, shared);
    (void)read;
    return 0;
}
