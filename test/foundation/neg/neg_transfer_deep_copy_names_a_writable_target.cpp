// Negative-compile test. This file must fail to compile.
//
// A class that states refers_deep{} and copies from a const object holds a
// transfer property only when its statement names a const target (rule 1 of
// foundation/Transfer.h).  This class takes a const first type argument, but
// its statement names a target that is not const.  A copy of a const object
// of it gives a writer of that target, so the class is not Sendable.  The
// walk reads the target from the statement, and not from the first type
// argument.

#include <foundation/Transfer.h>

namespace deep_target {

template <class First, class Target>
struct[[= ::foundation::transfer::refers_deep{^^Target}]] WritesOtherTarget {
    Target& target;
};

}  // namespace deep_target

static_assert(::foundation::transfer::Sendable<deep_target::WritesOtherTarget<int const, int>>);

int main() { return 0; }
