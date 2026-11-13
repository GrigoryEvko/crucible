// The route that a friend declaration of OneOf in a sum slot gave: a unit
// that includes only Choice.h and defines a class template OneOf of its own
// read the private parts of a slot, such as the tag of an owning slot.  The
// rules of the base refuse such a definition, and this probe writes it in an
// opt-out region.  No friend of a slot names OneOf, so the read is refused.

#include <foundation/Quarantine.h>
#include <foundation/core/Choice.h>

struct Owner {
    int handle = 0;
    ~Owner() {}
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged OneOf shows that a sum slot names no OneOf as a friend")
namespace foundation::core {
template <class... Ts>
    requires OneOfAlternatives<Ts...>
class OneOf {
public:
    detail::SumSlot<Ts...> slot_;
    [[nodiscard]] unsigned char tag() const noexcept { return slot_.tag_; }
};
}  // namespace foundation::core
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    foundation::core::OneOf<Owner, int> forged{{foundation::core::detail::AlternativeAt<1>{}, 7}};
    return forged.tag();
}
