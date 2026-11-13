// A foreign ABI can write each bit pattern, and a bool has two values, so
// mint_foreign_view refuses a run of bool.

#include <foundation/core/Region.h>

int main() {
    auto const flags = ::foundation::core::mint_foreign_view(static_cast<bool const*>(nullptr), 0);
    return static_cast<int>(flags.size());
}
