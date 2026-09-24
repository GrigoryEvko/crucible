// M012: monotonic x concurrent without an atomic representation.  Two
// threads stepping a monotonic counter through a non-atomic carrier
// lose updates, because the read, the compare and the write are three
// operations and nothing orders them.
//
// The rule needs all three premises, which is its whole content: the
// same pack with atom::repr<ReprKind::Atomic> is admitted, and
// test/fixy/test_collision.cpp holds that pair.
//
// The pack trips M012 alone.  The coroutine supplies the concurrency
// premise without an effect row, so no corpus entry reads it.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::mut_monotonic, ::fixy::atom::coroutine> refused{};
    return 0;
}
