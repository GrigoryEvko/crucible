// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-06 fixture: the ctx-bound overload
// `::crucible::safety::mint_permission_root<Tag, Ctx>(Ctx const&)`
// is also concept-deleted for any federation peer tag.  An ExecCtx
// argument is NOT a substitute for the federation admittance check
// — having a ctx in scope does not authorize cross-org admittance.
//
// Companion to neg_fixy_federation_root_mint_disallowed.cpp; together
// the two fixtures cover both `mint_permission_root` overloads and
// satisfy the HS14 ≥2 floor for fixy-CR-06.
//
// Expected diagnostic: GCC's "use of deleted function" naming
// `mint_permission_root` (ctx variant) and citing the fixy-CR-06
// reason string.

#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/FederationPermission.h>

namespace eff  = crucible::effects;
namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

struct NegRootMintCtx_OrgA {};

int main() {
    // Default ExecCtx — empty row, satisfies CtxAdmitsPermission for the
    // federation tag (FederatedPeer<Org> has Row<> permission_row).  The
    // deleted overload's concept ordering wins over the generic ctx-bound
    // `mint_permission_root<Tag, Ctx>(Ctx const&)` because
    // `is_federated_peer_tag_v<tag::FederatedPeer<OrgA>>` is true.  Must
    // NOT compile.
    constexpr auto ctx = eff::ExecCtx<>{};
    auto bad = saf::mint_permission_root<
        perm::tag::FederatedPeer<NegRootMintCtx_OrgA>>(ctx);
    (void)bad;
    return 0;
}
