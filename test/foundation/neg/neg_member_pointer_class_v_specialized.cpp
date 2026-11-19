// The walk of a type and the hash of a type read the class of a pointer to
// member.  This file tries to give a pointer to a member of Forged the
// class Honest: it specializes the variable spelling member_pointer_class_.
// The class comes from a deduction, and no variable gives it, so the
// specialization does not compile.

#include <foundation/Quarantine.h>
#include <foundation/reflect/TypeComponents.h>

#include <meta>

namespace forged_member {
struct Forged {
    int value = 0;
};
struct Honest {
    int value = 0;
};
}  // namespace forged_member

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr std::meta::info foundation::reflect::detail::member_pointer_class_<int forged_member::Forged::*> =
    ^^forged_member::Honest;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
