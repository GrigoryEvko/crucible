// An address changes from run to run, so an order of two keys gives a
// different answer in a different run.  AddressKey has no order.

#include <foundation/core/Ref.h>

int main() {
    int first = 1;
    int second = 2;
    ::foundation::core::AddressKey const low = ::foundation::core::mint_address_key(&first);
    ::foundation::core::AddressKey const high = ::foundation::core::mint_address_key(&second);
    return low < high ? 0 : 1;
}
