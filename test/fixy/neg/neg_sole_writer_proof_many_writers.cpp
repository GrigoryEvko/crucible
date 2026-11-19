// A counter of many writers has no one writer for a proof to name (THE PROOF
// OF THE ONE WRITER of fixy/Mutation.h).  An owner makes a proof and gives it
// to such a counter, so a publish with no check would race the store of
// another writer.

#include <fixy/Mutation.h>

#include <cstdint>

struct Role {};
struct Owner {
    static void publish(fixy::AtomicMonotonic<std::uint64_t>& counter, fixy::Monotonic<std::uint64_t>& own) noexcept {
        counter.advance_sole_writer_by_proof(fixy::SoleWriterProof<Owner, Role>{}, own, 1);
    }
};

int main() {
    fixy::AtomicMonotonic<std::uint64_t> counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    fixy::Monotonic<std::uint64_t> own = fixy::mint_monotonic<std::uint64_t>(0);
    Owner::publish(counter, own);
    return 0;
}
