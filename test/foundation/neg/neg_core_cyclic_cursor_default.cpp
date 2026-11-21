// A cyclic cursor holds a run of at least one element.  A cursor with no run
// would read nothing, so the type has no default constructor.

#include <foundation/core/CyclicCursor.h>

#include <cstdint>

int main() {
    foundation::core::CyclicCursor<std::uint64_t> cursor;
    return static_cast<int>(cursor.count());
}
