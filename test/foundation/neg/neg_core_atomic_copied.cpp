// The address of an Atomic is its identity: another thread reaches the
// cell through it.  A copy would make a second cell that no other thread
// sees, so the copy is refused.

#include <foundation/core/Atomic.h>

int main() {
    ::foundation::core::Atomic<int> original{1};
    ::foundation::core::Atomic<int> duplicate{original};
    return duplicate.load_acquire();
}
