// Negative-compile test. This file must fail to compile.
//
// A class that states refers{} is a reference to its target (rule 1 of
// foundation/Transfer.h).  This class takes a const first type argument, but
// its statement names a target that is not const, so it is a reference to an
// int that two threads cannot write.  The walk reads the target from the
// statement, and not from the first type argument, so the class is not
// Sendable.

#include <foundation/Transfer.h>

namespace borrow_target {

template <class First, class Target>
struct[[= ::foundation::transfer::refers{^^Target}]] WritesNamedTarget {
    Target& target;
};

}  // namespace borrow_target

static_assert(::foundation::transfer::Sendable<borrow_target::WritesNamedTarget<int const, int>>);

int main() { return 0; }
