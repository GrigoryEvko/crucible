// A borrow of a temporary owning range dangles at the end of the
// statement.  An rvalue range that is not a borrowed range selects the
// deleted twin of mint_borrowed, so the call names a deleted function.
// A span rvalue is a borrowed range and is admitted; that case is a
// cell of the header's self-test.

#include <fixy/Borrowed.h>

#include <vector>

namespace {
struct Owner {};
}  // namespace

int main() {
    auto borrow = ::fixy::mint_borrowed<Owner>(std::vector<int>{1, 2, 3});
    (void)borrow;
    return 0;
}
