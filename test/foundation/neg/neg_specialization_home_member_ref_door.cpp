// A file outside Ref.h writes an explicit specialization of the member
// template RefDoor::lend.  The specialization is a member of RefDoor, so its
// body can build the key and give a Ref with a brand that it names, such as
// the brand of an arena.  The rule specialization-home refuses the
// specialization in each build, because a function template of the tree gets
// an explicit specialization only in its own file.  The body reaches no
// private member, so the fixture reads only the rule.

#include <foundation/core/Ref.h>

struct ArenaBrand {};

template <>
constexpr foundation::core::Ref<int, ArenaBrand>
foundation::core::detail::RefDoor::lend<ArenaBrand, int>(int&) noexcept {
    for (;;) {}
}

int main() { return 0; }
