// Negative-compile test. This file must fail to compile.
//
// A nested handle of a class that states synchronized{} writes the plain
// count of the class.  It holds the Permission of an own tag of the class,
// but by reference: another holder owns the token, so the reference proves
// no one holder, and the door bump of the handle stays a door with no proof
// (the nested classes of foundation/Transfer.h).
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the door bump of the handle.

#include <foundation/Transfer.h>
#include <foundation/permissions/Permission.h>

namespace {

struct CounterBrand final {};

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    struct Region {};

    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    class Handle {
        using Token = ::foundation::permissions::Permission<Region, CounterBrand>;

        Counter& counter_;
        Token& token_;

        Handle(Counter& counter, Token& token) noexcept : counter_{counter}, token_{token} {}

    public:
        void bump() noexcept { ++counter_.count_; }
    };

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
