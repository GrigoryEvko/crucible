// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-049: Delegate<Stop, RecoveryProto> cannot be used to enter
// RecoveryProto.  This is stronger than checking one missing
// Permission<X> precondition in RecoveryProto: the carrier receives
// no continuation capability at all once the delegated endpoint is
// already Stop.

#include <crucible/sessions/SessionDelegate.h>

#include <utility>

using namespace crucible::safety::proto;

namespace {

struct Wire {};
struct Ack {};
struct NeedsPermission {};

using RecoveryProto = Recv<NeedsPermission, Recv<Ack, End>>;
using BadCarrier    = Delegate<Stop, RecoveryProto>;

void transport(Wire&, Wire&&) noexcept {}

[[maybe_unused]] void probe() {
    auto carrier = mint_session_handle<BadCarrier>(Wire{});
    auto stopped = mint_session_handle<Stop>(Wire{});

    (void)std::move(carrier).delegate(std::move(stopped), transport);
}

}  // namespace
