// A RunAddress lends the right to write its run, so mint_foreign_run_address
// refuses a pointer to data that only reads.

#include <foundation/core/DataAddress.h>

int main() {
    static unsigned char const constant = 7;
    static_cast<void>(::foundation::core::mint_foreign_run_address(&constant));
    return 0;
}
