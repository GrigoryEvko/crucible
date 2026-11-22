// Negative-compile test. This file must fail to compile.
//
// A const door of a nested handle still writes the class through the
// reference member of the handle.  So a const door of a nested class gets no
// exemption, and the door bump, which takes no proof, refuses (the nested
// classes of foundation/Transfer.h).
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the const door bump of the handle.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    class Handle {
        Counter& counter_;

        explicit Handle(Counter& counter) noexcept : counter_{counter} {}

    public:
        void bump() const noexcept { ++counter_.count_; }
    };

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
