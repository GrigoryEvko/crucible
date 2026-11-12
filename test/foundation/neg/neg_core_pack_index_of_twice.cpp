// pack_index_of_v takes a type that is in the Pack one time.  A type that is
// in the Pack two times has two indexes, so it gets none.

#include <foundation/core/Record.h>

int main() {
    return static_cast<int>(::foundation::core::pack_index_of_v<int, ::foundation::core::Pack<int, long, int>>);
}
