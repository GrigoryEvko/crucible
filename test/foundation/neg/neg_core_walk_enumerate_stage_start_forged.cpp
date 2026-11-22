// The type of the root stage of enumerate() is a part of the type of its
// walk, so a caller deduces it with no name of namespace detail.  A stage
// from a run and a start that the caller gives would make an Index<4> past
// 4 at each step, and at() of a FixedArray<int, 4> would read past its last
// element with no check.  The constructor from a run and a start is
// private: only a limit or a skip of a stage gives a later start.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};
using Stage = decltype(stage_of(values.enumerate()));

[[nodiscard]] static std::size_t count_forged(std::size_t start) noexcept {
    return fc::Walk<Stage>{Stage{values.view(), start}}.size();
}

int main() { return static_cast<int>(count_forged(std::size_t{4})); }
