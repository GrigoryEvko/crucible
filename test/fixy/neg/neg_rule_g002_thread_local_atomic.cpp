// G002: thread-local x atomic representation.  Thread-local storage is
// private to one thread, so an atomic carrier inside it pays for
// synchronization nobody can observe: the lock prefix, the store-buffer
// drain and the cache-line transfer are all bought and none is needed.
//
// The pack trips G002 alone.  M012 also reads the atomic representation,
// but as the premise that RESCUES a monotonic concurrent binding rather
// than one that refuses, and this pack names no Mutation atom.
//
// The thread-local atom requires a tag naming the storage, so an
// untagged thread-local form cannot be spelled at all.

#include <fixy/Fn.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {
struct parser_scratch_tag final {};
}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::global::thread_local_<fixture::parser_scratch_tag>,
                                ::fixy::atom::repr<::fixy::pole::ReprKind::Atomic>> refused{};
    return 0;
}
