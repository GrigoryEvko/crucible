// A count holds an integer that is not bool.  A bool has no addition, so the
// cell of a count refuses it.

#include <foundation/core/Atomic.h>

int main() {
    ::foundation::core::Atomic<bool, ::foundation::core::Counter> seen{};
    return seen.load_acquire() ? 1 : 0;
}
