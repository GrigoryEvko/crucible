// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-019: HS14 floor for mint_atomic_session<Proto>(cell).
//
// The requires-clause's second conjunct is
// safety::proto::is_well_formed_v<Proto>.  A free `Continue` at
// top level (no enclosing Loop) is the canonical malformed
// protocol — its is_well_formed specialization requires a
// non-void LoopCtx, which the bridge invokes with default void.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_atomic_session'"

#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/sessions/Session.h>

#include <atomic>

namespace safety = ::crucible::safety;
namespace proto  = ::crucible::safety::proto;

// Minimal AtomicMachineCell that satisfies the FIRST conjunct so the
// requires-failure isolates the Proto well-formedness check.
struct DummyCell {
    using state_type = int;
    std::atomic<int> value{0};
    state_type load(std::memory_order order = std::memory_order_acquire) const noexcept {
        return value.load(order);
    }
};

using MalformedProto = proto::Continue;  // free Continue, no enclosing Loop

int main() {
    DummyCell cell{};
    auto bad = safety::mint_atomic_session<MalformedProto>(cell);
    (void)bad;
    return 0;
}
