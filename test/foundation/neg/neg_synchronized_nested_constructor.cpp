// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A public constructor of a nested class takes the class and writes its
// plain count.  The constructor runs after the class is shared, so it is a
// door, and it takes no proof (THE NESTED CLASSES of foundation/Transfer.h).
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the constructor of the handle.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    class Handle {
    public:
        explicit Handle(Counter& counter) noexcept { ++counter.count_; }
    };

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
