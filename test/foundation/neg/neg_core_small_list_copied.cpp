// A copy of a SmallList past its inline slots would allocate with no
// allocation capability at its call, so a list does not copy.

#include <foundation/core/SmallList.h>

#include <cstddef>

int main() {
    auto const list = ::foundation::core::mint_small_list<int, 4>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    auto const copy = list;
    return static_cast<int>(copy.size());
}
