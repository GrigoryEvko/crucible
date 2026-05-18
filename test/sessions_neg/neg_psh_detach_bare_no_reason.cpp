// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-025 HS14 fixture #1.  PersistedSessionHandle inherits the
// `void detach() && = delete("[DetachReason_Required]")` overload
// from SessionHandleBase (Session.h:1652).  A bare `.detach()` call
// — no DetachReason tag — must hit the deleted-overload diagnostic
// rather than dispatch to the new templated overload added in
// A2-025.  This pins the rvalue-only / typed-reason discipline that
// survived the unblock.
//
// Expected diagnostic family (one or more should match):
//   "DetachReason_Required"  |  "deleted"  |  "matching function"

#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

struct Resource {};

using P = proto::Send<int, proto::End>;

int main() {
    auto cipher = ::crucible::Cipher::open("/tmp/crucible_neg_psh_detach_bare");
    auto view   = cipher.mint_open_view();
    eff::TestRunnerCtx ctx{};
    auto h = proto::mint_persisted_session<P>(
        ctx,
        cipher,
        view,
        Resource{},
        proto::SessionTagId{1},
        proto::RoleTagId{1},
        proto::RoleTagId{2});

    // Missing DetachReason — must select the deleted bare-arg
    // overload, not the new templated detach<Reason>().
    std::move(h).detach();
    return 0;
}
