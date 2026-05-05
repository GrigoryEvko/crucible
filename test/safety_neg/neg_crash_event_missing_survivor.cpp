// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-045: user-facing crash handlers must not accept a CrashEvent
// whose SurvivorTags pack omits any tag from survivors_t<PeerTag>.
// on_crash() enforces the equality before invoking the handler.

#include <crucible/bridges/CrashTransport.h>

#include <expected>
#include <utility>

namespace proto = crucible::safety::proto;

struct DeadPeer {};
struct SurvivorA {};
struct SurvivorB {};
struct Channel {};

namespace crucible::permissions {

template <>
struct survivor_registry<DeadPeer> {
    using type = inheritance_list<SurvivorA, SurvivorB>;
};

}  // namespace crucible::permissions

int main() {
    using BadExpected =
        std::expected<int, proto::CrashEvent<DeadPeer, Channel, SurvivorA>>;

    BadExpected* bad = nullptr;
    proto::on_crash(*bad, [](auto) {});
    return 0;
}
