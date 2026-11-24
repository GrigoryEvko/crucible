// Negative compile test.  This file must fail to compile.
//
// A snapshot names its one writer in its type: each publish takes the proof
// of that writer, and only the owner of the proof makes it.  A publish with
// no proof would be a second writer that no type names, so it has no
// overload.  The same publish with the proof of the owner compiles.

#include <fixy/concurrent/AtomicSnapshot.h>
#include <foundation/SoleWriterProof.h>

#include <cstdint>

class SnapshotOwner;
struct PublishRole {};

int main() {
    ::fixy::concurrent::AtomicSnapshot<std::uint64_t, ::foundation::SoleWriterProof<SnapshotOwner, PublishRole>>
        snapshot{};
    snapshot.publish(std::uint64_t{1});
    return 0;
}
