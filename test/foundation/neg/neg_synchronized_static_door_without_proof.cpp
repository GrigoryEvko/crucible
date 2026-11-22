// Negative-compile test. This file must fail to compile.
//
// A static member function that takes the object through a reference that is
// not const writes the object as a door does.  The door reset writes the plain
// count with no proof of the class, so the check refuses it (the doors of a
// synchronized class of foundation/Transfer.h).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the static door reset.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    static void reset(Counter& counter) noexcept { counter.count_ = 0; }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
