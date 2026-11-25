// A copy of an ordered log would allocate with no capability at its call.  So
// the log does not copy.

#include <fixy/Mutation.h>

int main() {
    auto log = fixy::mint_ordered_append_only<int>();
    auto copy = log;
    return static_cast<int>(copy.size());
}
