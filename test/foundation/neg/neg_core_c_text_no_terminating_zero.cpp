// A CText takes a string literal: its last byte is the terminating zero that
// a call of the kernel reads up to.  An array with no terminating zero is no
// literal, so it stops the build at the call.

#include <foundation/core/Text.h>

inline constexpr char letters[3] = {'a', 'b', 'c'};

int main() {
    constexpr ::foundation::core::CText path{letters};
    return static_cast<int>(path.size());
}
