// A unit defines Box itself.  A Box of its own would give the mint of a Hole
// storage that no allocation gave, and consume() of the Hole would free it.
// Box and Hole name each other, and the header that declares each of them
// defines it too, so the Box of the unit is a second definition, and the
// compile stops.

#include <foundation/core/Ref.h>

namespace foundation::core {

template <BoxPayload T>
class Box {
public:
    [[nodiscard]] void* vacate_() noexcept { return nullptr; }
};

}  // namespace foundation::core

int main() {}
