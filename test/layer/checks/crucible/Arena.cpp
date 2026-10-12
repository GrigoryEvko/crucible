// The compile-time checks of crucible/Arena.h.

#include <crucible/Arena.h>

#include <cstddef>

namespace crucible {

// The fields of an Arena come first and fit in one cache line, and the table
// of blocks follows them.  Each allocation reads only the first three fields.
static_assert(sizeof(Arena) <= 64 + Arena::MAX_BLOCKS * (sizeof(std::byte*) + sizeof(size_t)),
              "the fields of an Arena must fit in one cache line beside its table of blocks");

}  // namespace crucible
