// A ViewMut of LoanKeys would keep a borrow of the keys past the frame that
// lent them.  ViewMut refuses a key of each brand as its element.

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>

struct KeptBrand final {};
struct FrameBrand final {};

int main() {
    return static_cast<int>(sizeof(::foundation::core::ViewMut<::foundation::core::LoanKey<KeptBrand>, FrameBrand>));
}
