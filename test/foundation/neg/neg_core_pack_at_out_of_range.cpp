// pack_at_t takes an index below the count of types.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::pack_at_t<2, ::foundation::core::Pack<int, long>> const value{};
    return static_cast<int>(value);
}
