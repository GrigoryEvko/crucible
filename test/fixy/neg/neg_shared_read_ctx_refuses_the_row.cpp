// The effect axis inside the composition.  The share is outstanding and
// the region is the right one, and a foreground context still cannot
// read a region whose tag says IO, because CtxAdmitsPermission is a
// conjunct of the mint's one concept.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace {
struct Spilled {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
using IoCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO>>;
using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
}  // namespace

int main() {
    static int storage[2] = {};
    IoCtx io{::foundation::effects::testing::bg()};
    FgCtx fg{};

    auto region =
        ::fixy::mint_owned_region(storage, std::size_t{2}, ::foundation::permissions::mint_permission_root<Spilled>(io));
    ::fixy::SharedRegion shared{std::move(region)};
    auto guard = shared.lend(io);

    auto read = ::fixy::mint_shared_read(fg, *guard, shared);
    (void)read;
    return 0;
}
