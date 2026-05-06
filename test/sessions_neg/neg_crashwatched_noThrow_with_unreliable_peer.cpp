// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-064: a CrashWatchedHandle watches an unreliable peer through a
// OneShotFlag.  Declaring that same peer CrashClass::NoThrow is a type
// contradiction and must be rejected at the handle boundary.

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
    using P = proto::Send<int, proto::End>;

    OneShotFlag flag;
    proto::PermissionedSessionHandle<P, proto::EmptyPermSet, Channel> psh{
        Channel{}};

    proto::CrashWatchedHandle<
        P,
        Channel,
        DeadPeer,
        proto::CrashClass::NoThrow> bad{std::move(psh), flag};
    (void)bad;
    return 0;
}
