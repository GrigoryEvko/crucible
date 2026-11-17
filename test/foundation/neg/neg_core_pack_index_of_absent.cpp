// pack_index_of_v takes a type that is in the Pack one time.  A type that is
// no part of the Pack has no index.

#include <foundation/reflect/TypeList.h>

int main() {
    return static_cast<int>(::foundation::reflect::pack_index_of_v<char, ::foundation::reflect::Pack<int, long>>);
}
