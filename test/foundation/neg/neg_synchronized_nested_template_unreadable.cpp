// Negative-compile test. This file must fail to compile.
//
// A nested class template of a class that states synchronized{} takes a lane
// number, which no probe fills.  The check cannot read the doors of any
// instance, and a door of an instance can write the plain count of the class,
// so the check fails closed (the nested classes of foundation/Transfer.h).
//
// Expected diagnostic: a nested class template has no specialization for the
// probe brand, the probe body or a probe context, and the diagnostic names the
// class and the template.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    template <int Lane>
    class Handle {
        int& count_;

        explicit Handle(int& count) noexcept : count_{count} {}
        friend class Counter;

    public:
        void bump() noexcept { count_ += Lane; }
    };

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
