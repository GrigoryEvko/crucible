// The type of the root stage of view.windows<2>() is a part of the type of
// the walk, so a caller deduces it, and the stage has a public constructor
// from a View.  at_(position) reads no bound of its own, so a position past
// the last run would give a View<int const, 2> past the View.  at_ is
// private: only the map and the zip stages call it, at a position below
// size_() that they prove.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};
using Stage = decltype(stage_of(values.view().windows<2>()));

[[nodiscard]] static int read_past(std::size_t position) noexcept {
    return Stage{fc::View<int const>{values.view()}}.at_(position).at(fc::index<1>());
}

int main() { return read_past(std::size_t{100}); }
