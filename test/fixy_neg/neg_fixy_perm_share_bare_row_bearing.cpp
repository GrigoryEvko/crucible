// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture: bare `mint_permission_share(Permission<T>&&)`
// (no-ctx form) routed through fixy::perm rejects when the tag carries
// a non-empty permission_row.
//
// Violation: bare-form share is gated by
//   static_assert(permission_row_empty_v<Tag>, ...)
// in the function body.  GpuMemoryTag has permission_row<Tag> =
// Row<Effect::Alloc>, which is NOT empty, so the static_assert fires.
// Routing through `fixy::perm::mint_permission_share` must reject
// identically.
//
// Expected diagnostic: permission_row<Tag> == Row<> /
// permission_row_empty_v.

#include <crucible/fixy/Perm.h>

namespace fperm = ::crucible::fixy::perm;
namespace ptag  = ::crucible::permissions::tag;
namespace eff   = ::crucible::effects;
namespace safe  = ::crucible::safety;

int main() {
    // Mint a GpuMemory token via the ctx-bound root (the bare form is
    // rejected for row-bearing tags at root mint too).
    eff::BgCompileCtx ctx{};
    auto exc = fperm::mint_permission_root<ptag::GpuMemoryTag>(ctx);

    // Should FAIL: bare share is only valid for permission_row<Tag>
    // == Row<>, but GpuMemoryTag has Row<Effect::Alloc>.
    [[maybe_unused]] auto shared = fperm::mint_permission_share(
        std::move(exc));
    return 0;
}
