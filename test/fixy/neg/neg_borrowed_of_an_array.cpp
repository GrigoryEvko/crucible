// mint_borrowed takes a region, which holds the permission of its
// elements.  A FixedArray holds no permission, and its own view() is the
// door to a View of it, so mint_borrowed refuses it.

#include <fixy/OwnedRegion.h>
#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 2> elements{1, 2};
    [[maybe_unused]] auto borrow = ::fixy::mint_borrowed(elements);
    return 0;
}
