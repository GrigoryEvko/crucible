// An address changes from run to run, so an order of two addresses gives a
// different answer in a different run.  RunAddress has no order.

#include <foundation/core/Array.h>
#include <foundation/core/DataAddress.h>

int main() {
    ::foundation::core::FixedArray<unsigned char, 8> bytes{};
    auto const low = ::foundation::core::mint_run_address(bytes.view(), 0).expect("eight bytes");
    auto const high = ::foundation::core::mint_run_address(bytes.view(), 4).expect("eight bytes");
    return low < high ? 0 : 1;
}
