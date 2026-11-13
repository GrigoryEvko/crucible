// A file outside Ref.h writes an explicit specialization of the template
// mint_box.  While a class befriends such a template, the body of the
// specialization reaches the private members of the class, as it reached the
// constructor of Box from a pointer.  The rule specialization-home refuses
// the specialization in each build, because a function template of the tree
// gets an explicit specialization only in its own file.  The body reaches no
// private member, so the fixture reads only the rule.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

template <>
[[noreturn]] foundation::core::Box<int>
foundation::core::mint_box<int, foundation::effects::Alloc>(foundation::effects::Alloc) noexcept {
    for (;;) {}
}

int main() { return 0; }
