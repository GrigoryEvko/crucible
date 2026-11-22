// The cursor of a zip walk holds the address of its stage, its position and
// the count of its stage.  Its type comes from begin() of a walk, so a
// caller deduces it.  A cursor from a position that the caller gives would
// step past the end with no equal compare, and a null stage would be read.
// The constructor is private: only the stage of the zip builds its cursors.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};

[[nodiscard]] static int read_forged() noexcept {
    auto walk = values.view().walk().zip(values.view().walk());
    using Cursor = decltype(walk.begin());
    Cursor const forged{nullptr, std::size_t{100}};
    auto const [first, second] = *forged;
    return *first + *second;
}

int main() { return read_forged(); }
