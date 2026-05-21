// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-058 fixture #1 — Delegate<DelegatedSession<P, InnerPS>, K>
// attempted with a delegated PSH whose ActualInnerPS does not match
// the declared InnerPS.

#include <crucible/sessions/PermissionedSession.h>

#include <source_location>
#include <utility>

using namespace crucible::safety::proto;

namespace {

struct WorkItem {};
struct CarrierResource {};
struct WorkerResource {};

using InnerProto = Send<Transferable<int, WorkItem>, End>;
using Payload    = DelegatedSession<InnerProto, PermSet<WorkItem>>;
using Carrier    = Delegate<Payload, End>;

void wire_delegate(CarrierResource&, WorkerResource&&) noexcept {}

}  // namespace

int main() {
    auto carrier = detail::permissioned_session_with_loc_<
        Carrier, EmptyPermSet, CarrierResource>(
        CarrierResource{}, std::source_location::current());

    // ActualInnerPS is EmptyPermSet, but Payload declares
    // InnerPS=PermSet<WorkItem>.  delegate() must reject the handoff
    // before transport can fabricate a permission-bearing endpoint.
    auto delegated = detail::permissioned_session_with_loc_<
        InnerProto, EmptyPermSet, WorkerResource>(
        WorkerResource{}, std::source_location::current());

    [[maybe_unused]] auto next = std::move(carrier).delegate(
        std::move(delegated), wire_delegate);
    return 0;
}
