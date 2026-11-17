// pack_index_of_v takes a type that is in the Pack one time.  A type that is
// in the Pack two times has two indexes, so it gets none.

#include <foundation/reflect/TypeList.h>

int main() {
    return static_cast<int>(::foundation::reflect::pack_index_of_v<int, ::foundation::reflect::Pack<int, long, int>>);
}
