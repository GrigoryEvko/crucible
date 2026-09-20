// Without a guard there is no read.  The share is a parameter of the
// mint and nothing else produces a SharedRead, so a caller holding the
// region and the context still has no way in.

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

    auto region =
        ::fixy::mint_owned_region(storage, std::size_t{2}, ::foundation::permissions::mint_permission_root<Cache>());
    ::fixy::SharedRegion shared{std::move(region)};

    auto read = ::fixy::mint_shared_read(ctx, shared);
    (void)read;
    return 0;
}
