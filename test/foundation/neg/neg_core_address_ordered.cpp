// An address changes from run to run, so an order of two addresses gives a
// different answer in a different run.  Address has no order.

#include <foundation/core/Ref.h>

int main() {
    int first = 1;
    int second = 2;
    ::foundation::core::Address const low = ::foundation::core::address_of(::foundation::core::to(first));
    ::foundation::core::Address const high = ::foundation::core::address_of(::foundation::core::to(second));
    return low < high ? 0 : 1;
}
