// A cyclic cursor is never at the end of its run, so its constructor from a
// first element and a count is private: its one door, cycle_from, checks that
// the cursor of the run is not at its end.  A forged run of no element does
// not compile.

#include <foundation/core/CyclicCursor.h>

#include <cstddef>
#include <cstdint>

int main() {
    foundation::core::CyclicCursor<std::uint64_t> cursor{nullptr, std::size_t{0}};
    return static_cast<int>(cursor.count());
}
