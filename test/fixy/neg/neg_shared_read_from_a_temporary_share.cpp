// A guard that dies at the end of the statement proves a share that is
// already released by the time the read is used.  The deleted rvalue
// twin refuses it, because the lifetime annotation alone carries no
// weight on this compiler.

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

    auto read = ::fixy::mint_shared_read(ctx, *shared.lend(ctx), shared);
    (void)read;
    return 0;
}
