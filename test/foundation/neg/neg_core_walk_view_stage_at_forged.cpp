// The type of the root stage of view.walk() is a part of the type of the
// walk, so a caller deduces it, and the stage has a public constructor from
// a View.  at_(position) reads no bound of its own, so a position past the
// View would read past it.  at_ is private: only the map and the zip stages
// call it, at a position below size_() that they prove.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};
using Stage = decltype(stage_of(values.view().walk()));

[[nodiscard]] static int read_past(std::size_t position) noexcept { return Stage{values.view()}.at_(position); }

int main() { return read_past(std::size_t{100}); }
