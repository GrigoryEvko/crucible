// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A proof of quiescence names the counter, but each thread that holds the
// counter can make one, so the proof does not tell which thread owns the
// counter.  Two bodies of run_joined that each make a proof and call bump
// would race, as the plant of BackgroundThread::set_numerics did with no
// proof.  The proof carries no authority and is not bound to a thread, so it
// is no proof of a door (THE PROOF OF A DOOR of foundation/Transfer.h).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the door bump with the proof of quiescence.

#include <fixy/os/OwnedThread.h>
#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    void bump(::fixy::thread::QuiescenceProof<Counter> const& /*proof*/) noexcept { ++count_; }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
