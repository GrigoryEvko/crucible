// The type of the stage of a zip is a part of the type of its walk, so a
// caller deduces it.  at_(key, position) of a zip gives the position to its
// two stages with no bound of its own, so a position past the two Views
// would read past them.  Only a map and a zip build the key, and each proves
// the position below size_().  The function below takes the stage by
// reference, because the constructor of a zip is private too.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};
using Stage = decltype(stage_of(values.view().walk().zip(values.view().walk())));

[[maybe_unused, nodiscard]] static int read_past(Stage const& stage, std::size_t position) noexcept {
    auto const [first, second] = stage.at_(fc::detail::StageKey{}, position);
    return *first + *second;
}

int main() { return 0; }
