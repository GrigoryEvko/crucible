// A budget authority is not rebuilt from bytes.  It has no copy and no
// move, so it is not trivially copyable, and std::bit_cast refuses it.  An
// authority made that way would grant any budget without Init.

#include <fixy/Budgeted.h>

#include <array>
#include <bit>
#include <cstddef>

int main() {
    auto authority = std::bit_cast<fixy::BudgetAuthority>(std::array<std::byte, sizeof(fixy::BudgetAuthority)>{});
    return static_cast<int>(authority.grant(fixy::BitsBudgetBound{0}, fixy::PeakBytesBound{0}).bits().raw());
}
