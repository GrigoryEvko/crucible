// A bitwise operation sets or keeps bits of an unsigned word.  The sign bit
// of a signed value gives a bitwise result no meaning as a number, so the
// operation is refused for an Atomic of a signed integer.

#include <foundation/core/Atomic.h>

int main() {
    ::foundation::core::Atomic<int> flags{0};
    return flags.fetch_or_acq_rel(4);
}
