// A budget authority is not started over bytes.  No constructor of it is
// trivial, so it is not an implicit-lifetime type, and the checked lifetime
// start refuses it.

#include <fixy/Budgeted.h>
#include <foundation/Lifetime.h>

int main() {
    alignas(8) unsigned char bytes[sizeof(fixy::BudgetAuthority)]{};
    auto authorities = ::foundation::lifetime::start_as_array<fixy::BudgetAuthority>(bytes, 1);
    return static_cast<int>(authorities[0].grant(fixy::BitsBudgetBound{0}, fixy::PeakBytesBound{0}).bits().raw());
}
