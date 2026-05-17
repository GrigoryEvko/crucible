// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Bridge fixture #2: CrashWatchedHandle alias via fixy::
// rejects an unreliable peer declared CrashClass::NoThrow.
//
// Violation: a CrashWatchedHandle watches an unreliable peer through
// a OneShotFlag; declaring that same peer CrashClass::NoThrow is a
// type contradiction and must be rejected at the handle boundary.
// Routing through `fixy::bridge::CrashWatchedHandle` must reject
// identically.
//
// Expected diagnostic: CrashWatched_NoThrow_Rejected static_assert.

#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace fbridge = crucible::fixy::bridge;
namespace proto   = crucible::safety::proto;
namespace eff     = crucible::effects;
using crucible::safety::OneShotFlag;

struct DeadPeer {};
struct Survivor {};
struct Channel {};

namespace crucible::permissions {

template <>
struct survivor_registry<DeadPeer> {
    using type = inheritance_list<Survivor>;
};

}  // namespace crucible::permissions

int main() {
    using P = proto::Send<int, proto::End>;

    eff::HotFgCtx ctx{};
    OneShotFlag flag;
    auto psh = proto::mint_permissioned_session<P>(ctx, Channel{});

    fbridge::CrashWatchedHandle<
        P,
        Channel,
        DeadPeer,
        proto::CrashClass::NoThrow> bad{std::move(psh), flag};
    (void)bad;
    return 0;
}
