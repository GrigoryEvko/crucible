// mint_foreign_text gives the one TextView of a foreign run, so a call that
// drops its result is refused.

#include <foundation/core/Region.h>

int main() {
    ::foundation::core::mint_foreign_text(static_cast<char const*>(nullptr), 0);
    return 0;
}
