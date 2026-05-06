// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-064: an Abort-grade crash watcher must terminate in an
// Abort-grade Stop_g when the protocol carries an explicit Stop
// continuation.  A weaker Stop_g<Throw> means the strong recovery path
// is not present at the protocol boundary.

#include <crucible/bridges/CrashTransport.h>

#include <utility>

namespace proto = crucible::safety::proto;
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
    using P = proto::Send<int, proto::Stop_g<proto::CrashClass::Throw>>;

    OneShotFlag flag;
    proto::PermissionedSessionHandle<P, proto::EmptyPermSet, Channel> psh{
        Channel{}};

    proto::CrashWatchedHandle<
        P,
        Channel,
        DeadPeer,
        proto::CrashClass::Abort> bad{std::move(psh), flag};
    (void)bad;
    return 0;
}
