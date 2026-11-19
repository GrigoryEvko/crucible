// Only the owner of a counter makes the proof of its one writer (THE PROOF OF
// THE ONE WRITER of fixy/Mutation.h).  A type that is not the owner tries to
// make the proof of the producer of a ring of the owner, so it would publish
// with no check while a second producer writes the same index.

#include <fixy/concurrent/SpscRing.h>

struct Owner {};
struct OtherCaller {
    static bool push(fixy::concurrent::SpscRing<int, 8, Owner>& ring) noexcept {
        return ring.try_push_proven(fixy::concurrent::SpscRing<int, 8, Owner>::producer_proof{}, 1);
    }
};

int main() {
    fixy::concurrent::SpscRing<int, 8, Owner> ring;
    return OtherCaller::push(ring) ? 0 : 1;
}
