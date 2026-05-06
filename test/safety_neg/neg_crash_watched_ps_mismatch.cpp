// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-045: the PSH-aware CrashWatchedHandle constructor is typed by the
// exact consumer-side PermSet.  A PermissionedSessionHandle carrying
// EmptyPermSet cannot be inserted into a CrashWatchedHandle spelling
// PermSet<WrongPerm>.

#include <crucible/bridges/CrashTransport.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;
using crucible::safety::OneShotFlag;

struct DeadPeer {};
struct Survivor {};
struct WrongPerm {};
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

    proto::CrashWatchedHandle<
        P,
        Channel,
        DeadPeer,
        proto::CrashClass::Abort,
        void,
        proto::PermSet<WrongPerm>> bad{std::move(psh), flag};
    (void)bad;
    return 0;
}
