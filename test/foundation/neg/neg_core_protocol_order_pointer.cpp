// order of the Protocol family refuses a pointer.  The order of two
// addresses changes from run to run, so an order of them breaks the
// bit-exact replay.  equal of the family takes a pointer, as an address.
// The template argument gives the part its pointer type.  The walk reads the
// annotation of the instance, so equal takes it, and only the pointer part
// refuses the order.

#include <foundation/core/Equatable.h>

template <class Part>
struct[[= ::foundation::core::derive_protocol{}]] HoldsAddress {
    Part address{};
};

static_assert(::foundation::core::Equatable<HoldsAddress<int const*>>);

int main() {
    HoldsAddress<int const*> const value{};
    return std::is_eq(::foundation::core::order(value, value)) ? 0 : 1;
}
