// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture: mint_permission_share via fixy::perm alias
// rejects when the supplied Ctx::row does not admit the Tag's
// permission_row.
//
// Violation: NetworkBufferTag has permission_row = Row<Effect::IO>.
// HotFgCtx has row = Row<> which does NOT contain IO.  The ctx-bound
// `mint_permission_share(ctx, Permission<Tag>&&)` overload requires
// CtxAdmitsPermission<Tag, Ctx>, which fails for the (Tag, HotFgCtx)
// pair.  Routing through `fixy::perm::mint_permission_share` must
// reject identically.
//
// Expected diagnostic: CtxAdmitsPermission / constraints not satisfied.

#include <crucible/fixy/Perm.h>

namespace fperm = ::crucible::fixy::perm;
namespace ptag  = ::crucible::permissions::tag;
namespace eff   = ::crucible::effects;
namespace safe  = ::crucible::safety;

int main() {
    // NetworkBufferTag has Row<Effect::IO>.  Use the ctx-bound mint
    // for permission_row<Tag> != Row<>.  We pick a BgCompileCtx-like
    // setup but downgrade to HotFgCtx so the IO admission fails.
    eff::HotFgCtx ctx{};

    // Mint a root with the proper bg-compile ctx so we have a token
    // to share; then attempt to share it via a ctx that doesn't admit
    // IO.  The share-mint requires CtxAdmitsPermission<Tag, Ctx>.
    eff::BgCompileCtx good_ctx{};
    auto exc = fperm::mint_permission_root<ptag::NetworkBufferTag>(good_ctx);

    // Should FAIL: HotFgCtx does NOT admit Row<Effect::IO>.
    [[maybe_unused]] auto shared = fperm::mint_permission_share<
        ptag::NetworkBufferTag>(ctx, std::move(exc));
    return 0;
}
