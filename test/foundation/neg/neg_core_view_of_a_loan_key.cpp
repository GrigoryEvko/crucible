// A View of LoanKeys would keep a borrow of the keys past the frame that lent
// them.  View refuses a key of each brand as its element.

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>

struct KeptBrand final {};

int main() {
    ::foundation::core::View<::foundation::core::LoanKey<KeptBrand> const> const kept{};
    return static_cast<int>(kept.size());
}
