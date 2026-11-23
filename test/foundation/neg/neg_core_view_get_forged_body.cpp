// A unit that includes only foundation/core/Region.h gives element_at_, the
// body of get of a View, a body of its own with no bound check.  Each get
// would then give a raw pointer past the end of its View.  element_at_ is a
// member of RegionRefDoor, and Region.h defines it in the class, so the body
// of the unit is a second definition, and the compile stops.

#include <foundation/core/Region.h>

#include <cstddef>

template <::foundation::brand::IsBrand Brand, class T>
constexpr auto foundation::core::RegionRefDoor::element_at_(T* first, std::size_t /*count*/,
                                                            std::size_t position) noexcept {
    return first + position;
}

int main() {}
