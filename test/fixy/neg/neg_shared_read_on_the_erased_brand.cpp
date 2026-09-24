// A read of a region built through the erased door.  On the erased brand
// every region of the tag is one type, so the deduction would pair a guard
// with any region of the tag.  The gate asks for a fresh brand.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>

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

    ::fixy::SharedRegion<int, Cache> shared{::fixy::OwnedRegion<int, Cache>::wrap(
        storage, std::size_t{2}, ::foundation::permissions::mint_permission_root<Cache>())};
    auto guard = shared.lend(ctx);
    auto read = ::fixy::mint_shared_read(ctx, *guard, shared);
    (void)read;
    return 0;
}
