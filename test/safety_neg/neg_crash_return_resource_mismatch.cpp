// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-045: wrap_crash_return must recover exactly the inner handle's
// resource_type.  A mismatched resource would make CrashEvent lie about
// the recovered runtime state.

#include <crucible/bridges/CrashTransport.h>

#include <utility>

namespace proto = crucible::safety::proto;

struct DeadPeer {};
struct Survivor {};
struct Channel {};
struct WrongResource {};

namespace crucible::permissions {

template <>
struct survivor_registry<DeadPeer> {
    using type = inheritance_list<Survivor>;
};

}  // namespace crucible::permissions

int main() {
    using P = proto::Send<int, proto::End>;
    proto::PermissionedSessionHandle<P, proto::EmptyPermSet, Channel> psh{
        Channel{}};

    auto bad = proto::wrap_crash_return<DeadPeer>(
        std::move(psh),
        proto::detach_reason::TransportClosedOutOfBand{},
        WrongResource{});
    (void)bad;
    return 0;
}
