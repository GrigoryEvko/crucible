// advance_sole_writer_from checks that its caller holds the stored value,
// and then it stores with a plain release store.  That is sound only for the
// one thread that writes the counter: a second writer can store between the
// check and the store.  A counter of many writers has no such thread, so its
// type has no advance_sole_writer_from, as it has no peek_relaxed.  The call
// gives a proof of one writer, so only the many writers refuse it.

#include <fixy/Mutation.h>

#include <cstdint>

struct Owner {
    struct Role {};
    static void publish(fixy::AtomicMonotonic<std::uint64_t>& counter) noexcept {
        counter.advance_sole_writer_from(fixy::SoleWriterProof<Owner, Role>{}, 0, 1);
    }
};

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    Owner::publish(counter);
    return 0;
}
