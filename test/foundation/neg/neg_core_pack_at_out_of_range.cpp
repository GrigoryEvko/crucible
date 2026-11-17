// pack_at_t takes an index below the count of types.

#include <foundation/reflect/TypeList.h>

int main() {
    ::foundation::reflect::pack_at_t<2, ::foundation::reflect::Pack<int, long>> const value{};
    return static_cast<int>(value);
}
