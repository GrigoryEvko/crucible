// Negative-compile test. This file must fail to compile.
//
// A nested handle of a class that states synchronized{} writes the plain
// count of the class through its reference member.  It holds the guard of a
// share of another tag, which the class does not declare as its own, so the
// guard proves nothing about the class, and the door bump of the handle
// stays a door with no proof (the nested classes of foundation/Transfer.h).
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the door bump of the handle.

#include <foundation/Transfer.h>
#include <foundation/permissions/SharedPermission.h>

namespace {

struct OtherTag {};
struct OtherBrand final {};

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    class Handle {
        using Guard = ::foundation::permissions::SharedPermissionGuard<OtherTag, OtherBrand>;

        Counter& counter_;
        Guard guard_;

        Handle(Counter& counter, Guard&& guard) noexcept : counter_{counter}, guard_{static_cast<Guard&&>(guard)} {}

    public:
        void bump() noexcept { ++counter_.count_; }
    };

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
