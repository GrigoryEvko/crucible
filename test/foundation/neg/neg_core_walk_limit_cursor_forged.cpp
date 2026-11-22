// The cursor of a limit of a filter walk holds two cursors of the inner
// stage and the count of the elements that it can still give.  Its type
// comes from begin() of a walk, so a caller deduces it.  A cursor of two
// cursors and a count that the caller gives would not come from one stage.
// The constructor is private: only the stage of the limit builds its
// cursors.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

namespace {

struct IsEven final {
    [[nodiscard]] constexpr bool operator()(int const& value) const noexcept { return value % 2 == 0; }
};

}  // namespace

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};

[[nodiscard]] static int read_forged() noexcept {
    auto filtered = values.view().walk().filter(IsEven{});
    auto limited = values.view().walk().filter(IsEven{}).limit(fc::ElementCount{1});
    using Cursor = decltype(limited.begin());
    Cursor const forged{filtered.begin(), filtered.end(), std::size_t{100}};
    return *forged;
}

int main() { return read_forged(); }
