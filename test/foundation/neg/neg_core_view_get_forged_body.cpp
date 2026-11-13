// A unit that includes only foundation/core/Region.h gives element_at_, the
// body of get of a View, a body of its own with no bound check.  Each get
// would then give a raw pointer past the end of its View.  Region.h defines
// element_at_, so the body of the unit is a second definition, and the
// compile stops.

#include <foundation/core/Region.h>

#include <cstddef>

namespace foundation::core::detail {

template <::foundation::brand::IsBrand Brand, class T>
constexpr auto element_at_(T* first, std::size_t /*count*/, std::size_t position) noexcept {
    return first + position;
}

}  // namespace foundation::core::detail

int main() {}
