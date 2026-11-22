// A pop of an MPSC ring is a merge: the schedule of the producers decides the
// order of the items.  So the pop of the raw ring takes the bare tag of
// Nondet, as the pop of the permissioned channel does, and a pop with the
// proof of the consumer and no tag does not compile.
//
// Expected diagnostic: try_pop has no candidate with one argument.

#include <fixy/concurrent/MpscRing.h>

namespace {

struct Owner final {
    [[nodiscard]] static constexpr ::fixy::SoleWriterProof<Owner, ::fixy::concurrent::MpscConsumerRole>
    consumer() noexcept {
        return {};
    }
};

}  // namespace

int main() {
    ::fixy::concurrent::MpscRing<int, 8, Owner> ring;
    return ring.try_pop(Owner::consumer()).is_some() ? 0 : 1;
}
