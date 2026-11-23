// Negative-compile test. This file must fail to compile.
//
// A nested class template of a class that states synchronized{} holds a
// reference to the plain count of the class.  The door bump of each instance
// writes the count with no proof.  The check reads the template through its
// instance for the probe brand (the nested classes of foundation/Transfer.h).
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the door bump of the instance for the probe brand.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    template <class Scope>
    class Handle {
        int& count_;

        explicit Handle(int& count) noexcept : count_{count} {}
        friend class Counter;

    public:
        void bump() noexcept { ++count_; }
    };

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
