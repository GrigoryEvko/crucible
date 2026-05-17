// fixy_neg: mint_federation_channel rejects a non-IsExecCtx first
// argument.
//
// HS14 floor for fixy::sess::mint_federation_channel (fixy/Sess.h
// §B6 forwarder).  The forwarder threads `Ctx const&` into
// federation::mint_channel, whose own template parameter is constrained
// `IsExecCtx Ctx`.  Passing a plain `int` as the ctx slot fires the
// constraint-satisfaction failure inside federation::mint_channel.
//
// Note: we include only fixy/Sess.h (not FederationProtocol.h) to
// avoid the GCC 16.1.1 ICE in cp_fold_r on Refined.h via the
// federation path — fixy/Sess.h re-exports the federation symbols via
// namespace alias, so the call site resolves without the ICE-triggering
// transitive include chain.
//
// Expected diagnostic: "IsExecCtx" — constraint-satisfaction failure
// from federation::mint_channel's Ctx template parameter.

#include <crucible/fixy/Sess.h>

namespace fsess = crucible::fixy::sess;

struct FakeKey {};

int main() {
    int not_a_ctx = 0;
    // Plain int as ctx — fails IsExecCtx constraint.
    auto bad = fsess::mint_federation_channel<FakeKey>(
        not_a_ctx, 0, 0);
    (void)bad;
    return 0;
}
