// A read of a region minted with a fresh brand and then erased.  The
// erasure consumes the brand, so the shared region is on the erased brand
// like one built through the erased door, and the gate refuses it too.

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
    static int storage[2] = {};
    BgCtx ctx{::foundation::effects::testing::bg()};

    auto branded = ::fixy::mint_owned_region(storage, std::size_t{2},
                                             ::foundation::permissions::mint_permission_root<Cache>());
    ::fixy::OwnedRegion<int, Cache> erased = std::move(branded);
    ::fixy::SharedRegion shared{std::move(erased)};
    auto guard = shared.lend(ctx);
    auto read = ::fixy::mint_shared_read(ctx, *guard, shared);
    (void)read;
    return 0;
}
