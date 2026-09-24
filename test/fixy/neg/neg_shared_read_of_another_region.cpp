// The composition's own refusal.  The share is genuinely outstanding on
// the first region, the context genuinely admits the tag's row, and the
// read is taken against the second region of the same tag.  Every axis
// alone says yes.  The guard and the region name one Brand parameter,
// so the pair is refused by deduction.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace {
struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = ::foundation::effects::ExecCtx<::foundation::effects::Bg,
                                             ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;
}  // namespace

int main() {
    static int storage_a[2] = {};
    static int storage_b[2] = {};
    BgCtx ctx{::foundation::effects::testing::bg()};

    auto region_a = ::fixy::mint_owned_region(storage_a, std::size_t{2},
                                              ::foundation::permissions::mint_permission_root<Cache>());
    auto region_b = ::fixy::mint_owned_region(storage_b, std::size_t{2},
                                              ::foundation::permissions::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared_a{std::move(region_a)};
    ::fixy::SharedRegion shared_b{std::move(region_b)};

    auto guard_a = shared_a.lend(ctx);
    auto read = ::fixy::mint_shared_read(ctx, *guard_a, shared_b);
    (void)read;
    return 0;
}
