// A file outside Ref.h writes an explicit specialization of address_of.  The
// rules of the base refuse such a specialization, and this probe writes it in
// an opt-out region.  Address has no constructor from an integer alone and
// grants address_of no friend, so the body of the specialization cannot
// build an Address of an object that has no such address.

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>

struct ForgeBrand {};

using ForgedRef = foundation::core::Ref<int, ForgeBrand>;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged address_of shows that Address has no constructor from an integer")
template <>
foundation::core::Address foundation::core::address_of<int, ForgeBrand>(ForgedRef /*ref*/) noexcept {
    return foundation::core::Address{0x1001UL};
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
