// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-019: HS14 floor for mint_atomic_session<Proto>(cell).
//
// The requires-clause's first conjunct is AtomicMachineCell<Cell>.
// Passing a bare int (no state_type member, no load(memory_order)
// member) fails the concept and the function is not viable.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_atomic_session'"

#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/sessions/Session.h>

namespace safety = ::crucible::safety;
namespace proto  = ::crucible::safety::proto;

using DummyProto = proto::End;

int main() {
    int not_a_cell = 0;
    auto bad = safety::mint_atomic_session<DummyProto>(not_a_cell);
    (void)bad;
    return 0;
}
