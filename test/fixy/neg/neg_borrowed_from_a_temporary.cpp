// std::span of a const element type accepts an rvalue owning range, so
// a Borrowed of a const element type built from a temporary vector
// would compile through the span constructor and dangle at the end of
// the statement.  The rvalue-range twin of that constructor is the
// better match for the temporary, and it is deleted with a reason the
// compiler repeats.

#include <fixy/Borrowed.h>

#include <vector>

namespace {
struct Pool {};
}  // namespace

int main() {
    fixy::Borrowed<int const, Pool> dangling{std::vector<int>{1, 2, 3}};
    return static_cast<int>(dangling.size());
}
