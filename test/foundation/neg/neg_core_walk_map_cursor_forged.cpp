// The cursor of a map walk holds the cursor of the inner stage and the
// address of its stage, whose function it calls on each read.  Its type comes
// from begin() of a walk, so a caller deduces it.  A cursor from an address
// that the caller gives would read a null or an ended stage.  The
// constructor is private: only the stage of the map builds its cursors.

#include <foundation/core/Array.h>

namespace fc = ::foundation::core;

namespace {

struct Twice final {
    [[nodiscard]] constexpr int operator()(int const& value) const noexcept { return 2 * value; }
};

}  // namespace

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};

[[nodiscard]] static int read_forged() noexcept {
    auto walk = values.view().walk().map(Twice{});
    using Cursor = decltype(walk.begin());
    Cursor const forged{values.view().begin(), nullptr};
    return *forged;
}

int main() { return read_forged(); }
