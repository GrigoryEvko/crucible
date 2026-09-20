// G002: thread-local x atomic representation.  Thread-local storage is
// private to one thread, so an atomic carrier inside it pays for
// synchronization nobody can observe: the lock prefix, the store-buffer
// drain and the cache-line transfer are all bought and none is needed.
//
// The pack trips G002 alone.  M012 also reads the atomic representation,
// but as the premise that RESCUES a monotonic concurrent binding rather
// than one that refuses, and this pack names no Mutation atom.
//
// The tag parameter is what discharged the old catalog's G001: the
// thread-local atom requires a tag naming the storage, so the untagged
// form that rule refused cannot be spelled at all.

#include <fixy/Fn.h>

namespace {
struct parser_scratch_tag final {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::global::thread_local_<parser_scratch_tag>,
                                ::fixy::atom::repr<::fixy::pole::ReprKind::Atomic>>
        refused{};
    return 0;
}
