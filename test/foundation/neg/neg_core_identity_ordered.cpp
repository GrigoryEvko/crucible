// The value of an identity depends on the order of the mints, which two
// threads can change from run to run.  An order of two identities then gives
// a different answer in a different run, so Identity has no order.

#include <foundation/core/Identity.h>

int main() {
    ::foundation::core::Identity const first = ::foundation::core::mint_identity();
    ::foundation::core::Identity const second = ::foundation::core::mint_identity();
    return first < second ? 0 : 1;
}
