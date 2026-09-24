// A budget stamp is not rebuilt from bytes.  Its copy is user-provided, so
// it is not trivially copyable, and std::bit_cast refuses it.

#include <fixy/Budgeted.h>

#include <array>
#include <bit>
#include <cstddef>

int main() {
    auto const stamp = std::bit_cast<fixy::BudgetStamp>(std::array<std::byte, 16>{});
    return static_cast<int>(stamp.bits().raw());
}
