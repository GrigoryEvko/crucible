// The count of the indexes and the extent of the run are two arguments of
// the template of the root stage of enumerate().  A caller that deduces the
// template from the walk of a View<int const, 4> could name the stage of a
// dynamic extent over a run of 100 elements, and each step past the fourth
// would make an Index<4> past 4.  The constructor from a whole View takes a
// View of the count only, so the stage of a dynamic extent has none.

#include <foundation/core/Array.h>

#include <cstddef>

namespace fc = ::foundation::core;

template <template <class, std::size_t, class, std::size_t> class Stage, class Element, std::size_t Count, class Brand,
          std::size_t Extent>
[[nodiscard]] std::size_t count_forged(fc::Walk<Stage<Element, Count, Brand, Extent>> const& /*walk*/,
                                       fc::View<Element> run) noexcept {
    Stage<Element, Count, Brand, fc::dynamic_extent> const forged{run};
    return forged.size_();
}

inline fc::FixedArray<int, 100> const values{};

int main() {
    fc::View<int const> const run{values.view()};
    return static_cast<int>(
        count_forged(fc::View<int const, 4>{values.view().window<4>(0).expect("four")}.enumerate(), run));
}
