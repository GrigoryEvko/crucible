// A Box of a LoanKey would carry the key past the frame that lent it, in an
// owner that a member of a class can hold.  Box refuses a key of each brand as
// its payload.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>

struct KeptBrand final {};

int main() { return static_cast<int>(sizeof(::foundation::core::Box<::foundation::core::LoanKey<KeptBrand>>)); }
