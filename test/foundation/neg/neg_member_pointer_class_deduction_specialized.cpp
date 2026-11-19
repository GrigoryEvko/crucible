// The class of a pointer to member comes from the return type of a
// deduction.  This file tries to give a pointer to a member of Forged the
// class Honest: it writes an explicit specialization of the deduction that
// returns a pointer to Honest.  An explicit specialization keeps the return
// type of the template, so no template declaration matches it.

#include <foundation/reflect/TypeComponents.h>

namespace forged_member {
struct Forged {
    int value = 0;
};
struct Honest {
    int value = 0;
};
}  // namespace forged_member

template <>
forged_member::Honest* foundation::reflect::detail::member_pointer_parts_::class_of<int, forged_member::Forged>(
    int forged_member::Forged::*) noexcept;

int main() { return 0; }
