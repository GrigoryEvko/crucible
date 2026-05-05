// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-049: a delegated protocol that can emit before finishing
// requires the carrier continuation to expose a crash-recovery branch
// for the unreliable recipient.  Recv<Ack, End> has no such branch.

#include <crucible/sessions/SessionDelegate.h>

using namespace crucible::safety::proto;

namespace {

struct Recipient {};
struct Msg {};
struct Ack {};

using Delegated = Send<Msg, End>;
using CarrierK  = Recv<Ack, End>;

consteval bool probe() {
    assert_delegated_crash_propagates<Delegated, Recipient, CarrierK>();
    return true;
}

static_assert(probe());

}  // namespace
