// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class states synchronized{} and holds a plain private count, and its door
// bump writes the count with no proof of the class.  Two threads that borrow
// one object can call bump at the same time, as the two bodies of the plant of
// BackgroundThread::set_numerics did.  The check of the statement reads the
// doors of the class (THE DOORS OF A SYNCHRONIZED CLASS of
// foundation/Transfer.h).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the door bump.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    void bump() noexcept { ++count_; }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
