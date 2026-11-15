// An integer is no address that a foreign ABI gave, so a cast of a number
// does not become a RunAddress.  mint_foreign_run_address takes a pointer
// only.

#include <foundation/core/DataAddress.h>

int main() {
    static_cast<void>(::foundation::core::mint_foreign_run_address(0x1000UL));
    return 0;
}
