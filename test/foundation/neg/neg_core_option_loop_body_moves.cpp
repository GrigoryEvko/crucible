// The loop over a plain Option gives its body a reference to the payload.
// The body moves the Option out, which empties it, and the reference then
// names no payload.  The rule borrow-match reads the body of the loop as an
// arm and refuses the move.

#include <foundation/core/Choice.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    fc::Option<int> held = fc::Option<int>::some(3);
    int sum = 0;
    for (int const& value : held) {
        sum += std::move(held).value_or(0);
        sum += value;
    }
    return sum;
}
