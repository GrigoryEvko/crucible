// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-3 (CLAUDE.md §XXI HS14): mint_endpoint<Substr, Dir>(ctx, h)
// requires the first parameter to satisfy IsExecCtx.  Passing a non-
// Ctx type (a bare int) is rejected by SubstrateFitsCtxResidency's
// IsExecCtx<Ctx> conjunct.
//
// Violation: passes int{42} as the Ctx parameter; int is not an
// ExecCtx specialization.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at SubstrateFitsCtxResidency or IsExecCtx.

#include <crucible/concurrent/Endpoint.h>

namespace conc = crucible::concurrent;
namespace saf  = crucible::safety;

struct UserTag {};

int main() {
    using Channel = conc::PermissionedSpscChannel<int, 64, UserTag>;

    Channel ch;
    auto whole = saf::mint_permission_root<conc::spsc_tag::Whole<UserTag>>();
    auto [pp, cp] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UserTag>,
        conc::spsc_tag::Consumer<UserTag>>(std::move(whole));
    auto handle = ch.producer(std::move(pp));

    auto bad = conc::mint_endpoint<Channel, conc::Direction::Producer>(
        int{42}, handle);  // IsExecCtx fails
    (void)bad;
    (void)cp;
    return 0;
}
