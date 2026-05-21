// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-058 fixture #2 — Accept<DelegatedSession<P, InnerPS>, K>
// attempted by a carrier PSH that already owns InnerPS.

#include <crucible/sessions/PermissionedSession.h>

#include <source_location>
#include <utility>

using namespace crucible::safety::proto;
using ::crucible::safety::mint_permission_root;

namespace {

struct WorkItem {};
struct CarrierResource {};
struct WorkerResource {};

using InnerProto = Send<int, End>;
using Payload    = DelegatedSession<InnerProto, PermSet<WorkItem>>;
using Carrier    = Accept<Payload, End>;

WorkerResource wire_accept(CarrierResource&) noexcept {
    return WorkerResource{};
}

}  // namespace

int main() {
    auto work = mint_permission_root<WorkItem>();
    static_cast<void>(work);
    auto carrier = detail::permissioned_session_with_loc_<
        Carrier, PermSet<WorkItem>, CarrierResource>(
        CarrierResource{}, std::source_location::current());

    // Carrier PS already contains WorkItem.  Accepting Payload would
    // create a delegated inner PSH that also contains WorkItem.
    [[maybe_unused]] auto pair = std::move(carrier).accept(wire_accept);
    return 0;
}
