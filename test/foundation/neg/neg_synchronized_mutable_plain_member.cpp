// Negative-compile test. This file must fail to compile.
//
// A const door of a class that states synchronized{} writes only mutable
// members, so the check counts a const door as safe.  A mutable member that two
// threads cannot write would then race through the const door bump, so the
// check refuses the member (the doors of a synchronized class of
// foundation/Transfer.h).
//
// Expected diagnostic: a private member is mutable and two threads cannot
// write it, and the diagnostic names the member count_.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    void bump() const noexcept { ++count_; }

private:
    mutable int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
