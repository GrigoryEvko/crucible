// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The type of a counter of one writer names the proof of that writer (THE
// WRITER OF A COUNTER of fixy/Mutation.h), and only its Owner makes that
// proof.  Another class can make a proof of its own, but that proof names
// another owner, so it does not advance the counter.
//
// Expected diagnostic: no matching function for advance_sole_writer_from with
// the proof of the other owner.

#include <fixy/Mutation.h>

#include <cstdint>

struct CounterOwner {
    struct Role {};
};

struct OtherOwner {
    struct Role {};
    static void publish(fixy::AtomicMonotonic<std::uint64_t, fixy::SoleWriterProof<CounterOwner, CounterOwner::Role>>&
                            counter) noexcept {
        counter.advance_sole_writer_from(fixy::SoleWriterProof<OtherOwner, Role>{}, 0, 1);
    }
};

int main() {
    auto counter =
        fixy::mint_atomic_monotonic<std::uint64_t, fixy::SoleWriterProof<CounterOwner, CounterOwner::Role>>(0);
    OtherOwner::publish(counter);
    return 0;
}
