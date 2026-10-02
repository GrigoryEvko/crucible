// A floating value has two zeros with different bits, and a NaN that is not
// equal to itself.  A compare-and-swap compares bits, so the value gate
// refuses a floating type.

#include <foundation/core/Atomic.h>

int main() { return static_cast<int>(::foundation::core::Atomic<float>{}.load_acquire()); }
