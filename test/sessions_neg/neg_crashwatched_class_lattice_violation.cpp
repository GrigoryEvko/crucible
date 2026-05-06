// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-064: the watcher class and the protocol Stop_g grade are not
// locally widened through the lattice.  The class must be named at the
// exact Stop_g boundary so recovery code cannot silently observe a
// different crash contract than the protocol declared.

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
    using P = proto::Send<int, proto::Stop_g<proto::CrashClass::ErrorReturn>>;

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
