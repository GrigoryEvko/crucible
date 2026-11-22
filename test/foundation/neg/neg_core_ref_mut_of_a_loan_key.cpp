// A RefMut of a LoanKey would keep a borrow of the key past the frame that
// lent it.  RefMut refuses a key of each brand as its object.

#include <foundation/core/Frame.h>

struct KeptBrand final {};
struct FrameBrand final {};

int main() {
    return static_cast<int>(sizeof(::foundation::core::RefMut<::foundation::core::LoanKey<KeptBrand>, FrameBrand>));
}
