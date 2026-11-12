// pack_concat_t takes a Pack only.  A plain type is no list of types.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::pack_concat_t<::foundation::core::Pack<int>, long> const value{};
    return static_cast<int>(sizeof(value));
}
