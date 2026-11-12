// pack_index_of_v takes a type that is in the Pack one time.  A type that is
// no part of the Pack has no index.

#include <foundation/core/Record.h>

int main() { return static_cast<int>(::foundation::core::pack_index_of_v<char, ::foundation::core::Pack<int, long>>); }
