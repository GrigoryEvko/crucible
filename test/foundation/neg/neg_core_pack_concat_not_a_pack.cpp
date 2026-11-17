// pack_concat_t takes a Pack only.  A plain type is no list of types.

#include <foundation/reflect/TypeList.h>

int main() {
    ::foundation::reflect::pack_concat_t<::foundation::reflect::Pack<int>, long> const value{};
    return static_cast<int>(sizeof(value));
}
