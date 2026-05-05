// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-058 fixture #3 — DelegatedSession<P, InnerPS> where terminal P
// cannot carry the declared non-empty InnerPS.

#include <crucible/sessions/PermissionedSession.h>

#include <utility>

using namespace crucible::safety::proto;
using ::crucible::safety::mint_permission_root;

namespace {

struct WorkItem {};
struct CarrierResource {};
struct WorkerResource {};

using InnerProto = End;
using Payload    = DelegatedSession<InnerProto, PermSet<WorkItem>>;
using Carrier    = Delegate<Payload, End>;

void wire_delegate(CarrierResource&, WorkerResource&&) noexcept {}

}  // namespace

int main() {
    auto carrier = mint_permissioned_session<Carrier>(CarrierResource{});

    auto work = mint_permission_root<WorkItem>();
    auto delegated = mint_permissioned_session<InnerProto>(
        WorkerResource{}, std::move(work));

    // The declared inner endpoint is already terminal but still owns
    // WorkItem, so the handoff would preserve a close-time leak.
    [[maybe_unused]] auto next = std::move(carrier).delegate(
        std::move(delegated), wire_delegate);
    return 0;
}
