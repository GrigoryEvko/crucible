// A region that dies at the end of the statement leaves the read pointing
// into storage that is gone.  The guard here is live and names the same
// brand as the temporary region, because one function makes both regions
// and one call site mints one brand.  Only the deleted rvalue twin refuses
// the call, because the lifetime annotation alone carries no weight on
// this compiler.

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

// Each call returns a region of the same brand.
auto region_over(int* storage) {
    return ::fixy::SharedRegion{
        ::fixy::mint_owned_region(storage, std::size_t{2}, ::foundation::permissions::mint_permission_root<Cache>())};
}
}  // namespace

int main() {
    static int kept[2] = {};
    static int dropped[2] = {};
    BgCtx ctx{::foundation::effects::testing::bg()};

    auto shared = region_over(kept);
    auto guard = shared.lend(ctx);
    auto read = ::fixy::mint_shared_read(ctx, *guard, region_over(dropped));
    (void)read;
    return 0;
}
