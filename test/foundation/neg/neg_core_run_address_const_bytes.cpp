// A RunAddress lends the right to write its run, so mint_run_address
// refuses a View of bytes that only reads.

#include <foundation/core/Array.h>
#include <foundation/core/DataAddress.h>

int main() {
    ::foundation::core::FixedArray<unsigned char, 8> bytes{};
    ::foundation::core::View<unsigned char const> const read_only = bytes.view();
    static_cast<void>(::foundation::core::mint_run_address(read_only, 0));
    return 0;
}
