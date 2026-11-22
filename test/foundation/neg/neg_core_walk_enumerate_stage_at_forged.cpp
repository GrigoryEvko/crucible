// The type of the root stage of enumerate() is a part of the type of the
// walk, so a caller deduces it, and the stage has a public constructor from
// a whole View of its count.  A position past the View would give an
// Index<4> past 4, and at() of a FixedArray<int, 4> would read past its last
// element.  at_(key, position) takes the key that only the map and the zip
// stages build, and each proves the position below size_().

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};
using Stage = decltype(stage_of(values.enumerate()));

[[nodiscard]] static int read_past(std::size_t position) noexcept {
    auto const [index, element] = Stage{values.view()}.at_(fc::detail::StageKey{}, position);
    return values.at(index) + *element;
}

int main() { return read_past(std::size_t{100}); }
