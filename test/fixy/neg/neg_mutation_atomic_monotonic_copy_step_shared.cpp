// The checked publish that steps a copy of the counter is sound only for the
// one thread that writes the counter and keeps the copy.  A counter of many
// writers has no such thread, so its type has no such publish, as it has no
// advance_sole_writer_from of two values.  The call gives a proof of one
// writer, so only the many writers refuse it.

#include <fixy/Mutation.h>

#include <cstdint>

struct Owner {
    struct Role {};
    static void publish(fixy::AtomicMonotonic<std::uint64_t>& counter, fixy::Monotonic<std::uint64_t>& copy) noexcept {
        counter.advance_sole_writer_from(fixy::SoleWriterProof<Owner, Role>{}, 0, 1, copy);
    }
};

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    auto copy = fixy::mint_monotonic<std::uint64_t>(0);
    Owner::publish(counter, copy);
    return 0;
}
