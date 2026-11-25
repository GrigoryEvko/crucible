// A SmallList of no inline slot holds each element on the heap, so the mint
// refuses an inline count of zero.

#include <foundation/core/SmallList.h>

#include <cstddef>

int main() {
    auto const list = ::foundation::core::mint_small_list<int, 0>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{8});
    return static_cast<int>(list.size());
}
