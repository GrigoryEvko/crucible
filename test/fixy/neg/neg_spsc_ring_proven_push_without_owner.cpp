// A ring with no owner has no proven form (THE PROVEN FORM of
// fixy/concurrent/SpscRing.h).  A caller that owns some other type makes a
// proof of its own and gives it to such a ring, so the ring would publish
// with no check for a writer that no type proves.

#include <fixy/concurrent/SpscRing.h>

struct SomeOwner {
    static bool push(fixy::concurrent::SpscRing<int, 8>& ring) noexcept {
        return ring.try_push_proven(fixy::SoleWriterProof<SomeOwner, fixy::concurrent::SpscProducerRole>{}, 1);
    }
};

int main() {
    fixy::concurrent::SpscRing<int, 8> ring;
    return SomeOwner::push(ring) ? 0 : 1;
}
