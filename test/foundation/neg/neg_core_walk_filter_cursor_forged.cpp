// The cursor of a filter walk holds the cursors of the inner stage and the
// address of its stage, whose test it calls in its constructor and on each
// step.  Its type comes from begin() of a walk, so a caller deduces it.  A
// cursor from an address that the caller gives would read a null or an
// ended stage.  The constructor is private: only the stage of the filter
// builds its cursors.

#include <foundation/core/Array.h>

namespace fc = ::foundation::core;

namespace {

struct IsEven final {
    [[nodiscard]] constexpr bool operator()(int const& value) const noexcept { return value % 2 == 0; }
};

}  // namespace

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};

[[nodiscard]] static int read_forged() noexcept {
    auto walk = values.view().walk().filter(IsEven{});
    using Cursor = decltype(walk.begin());
    Cursor const forged{values.view().begin(), values.view().end(), nullptr};
    return *forged;
}

int main() { return read_forged(); }
