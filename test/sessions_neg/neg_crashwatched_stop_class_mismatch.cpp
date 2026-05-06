// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-064: a Throw-grade watcher may not observe a bare Stop
// continuation.  Bare Stop aliases Stop_g<Abort>; the mismatch must
// remain visible at compile time.

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
    using P = proto::Send<int, proto::Stop>;

    OneShotFlag flag;
    proto::PermissionedSessionHandle<P, proto::EmptyPermSet, Channel> psh{
        Channel{}};

    proto::CrashWatchedHandle<
        P,
        Channel,
        DeadPeer,
        proto::CrashClass::Throw> bad{std::move(psh), flag};
    (void)bad;
    return 0;
}
