// The type of the stage of a map is a part of the type of its walk, so a
// caller deduces it, and the stage has a public constructor from an inner
// stage and a function.  at_(position) of a map gives the position to the
// inner stage with no bound of its own, so a position past the View would
// read past it.  at_ is private: only a map and a zip call it, at a position
// below size_() that they prove.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

namespace {

struct Twice final {
    [[nodiscard]] constexpr int operator()(int const& value) const noexcept { return 2 * value; }
};

}  // namespace

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

inline fc::FixedArray<int, 4> const values{1, 2, 3, 4};
using Root = decltype(stage_of(values.view().walk()));
using Stage = decltype(stage_of(values.view().walk().map(Twice{})));

[[nodiscard]] static int read_past(std::size_t position) noexcept {
    return Stage{Root{values.view()}, Twice{}}.at_(position);
}

int main() { return read_past(std::size_t{100}); }
