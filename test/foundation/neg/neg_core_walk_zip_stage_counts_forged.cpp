// The type of the stage of a zip is a part of the type of its walk, so a
// caller deduces it.  A zip reads its two stages at one position up to the
// count of the first, so a zip of a stage of four elements and a stage of
// two would read past the second View.  The constructor of a zip is
// private: only zip, whose type proves the two counts equal, and try_zip,
// which compares them, build one.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <class Stage>
Stage stage_of(fc::Walk<Stage> const& walk) noexcept;

template <class Stage>
Stage stage_of(fc::Result<fc::Walk<Stage>, fc::LengthMismatch> const& walk) noexcept;

inline fc::FixedArray<int, 4> const four{1, 2, 3, 4};
inline fc::FixedArray<int, 2> const two{5, 6};
using Inner = decltype(stage_of(fc::View<int const>{four.view()}.walk()));
using Stage =
    decltype(stage_of(fc::View<int const>{four.view()}.walk().try_zip(fc::View<int const>{two.view()}.walk())));

[[nodiscard]] static std::size_t count_forged() noexcept {
    return fc::Walk<Stage>{Stage{Inner{fc::View<int const>{four.view()}}, Inner{fc::View<int const>{two.view()}}}}
        .size();
}

int main() { return static_cast<int>(count_forged()); }
