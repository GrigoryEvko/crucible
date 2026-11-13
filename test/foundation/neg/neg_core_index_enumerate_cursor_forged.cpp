// foundation/core/Index.h names no friend that a different header defines.
// A unit that includes only Index.h therefore cannot define the cursor of an
// enumerate loop itself and build an Index at its count, which at() of a
// FixedArray would read past the last element with no check.  The rules of
// the base refuse a declaration in a namespace of the base outside the base,
// and this probe writes the forged cursor in an opt-out region, so the
// fixture shows the refusal of the language.

#include <foundation/Quarantine.h>
#include <foundation/core/Index.h>

#include <cstddef>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged enumerate cursor shows that Index names it as no friend")
namespace foundation::core::detail {

template <class Element, std::size_t Count, class Brand>
class EnumerateCursor {
public:
    static constexpr Index<Count> past_the_end() noexcept { return Index<Count>{Count}; }
};

}  // namespace foundation::core::detail
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

inline foundation::core::Index<4> const forged = foundation::core::detail::EnumerateCursor<int, 4, int>::past_the_end();

int main() { return 0; }
