// A file outside Ref.h writes an explicit specialization of the template
// address_of for a brand of its own.  While a class befriends such a
// template, the body of the specialization reaches the private members of
// the class, as it reached the constructor of Address from an integer.  The
// rule specialization-home refuses the specialization in each build, because
// a function template of the tree gets an explicit specialization only in its
// own file.  The body reaches no private member, so the fixture reads only
// the rule.

#include <foundation/core/Ref.h>

struct ProbeBrand {};

template <>
[[noreturn]] foundation::core::Address foundation::core::address_of<int, ProbeBrand>(Ref<int, ProbeBrand>) noexcept {
    for (;;) {}
}

int main() { return 0; }
