// A copy of a log would allocate with no capability at its call.  So a log does
// not copy.

#include <fixy/Mutation.h>

int main() {
    auto log = fixy::mint_append_only<int>();
    auto copy = log;
    return static_cast<int>(copy.size());
}
