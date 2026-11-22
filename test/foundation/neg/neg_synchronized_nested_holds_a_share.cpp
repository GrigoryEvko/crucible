// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A nested handle of a class that states synchronized{} holds the guard of a
// share of an own tag by value, and its door writes the plain count of the
// class through its reference to the class.  Many handles hold a share at one
// time, so two of them write the count at the same time.  A held share makes
// no proof holder (THE SHARE of foundation/Transfer.h).  The control holds the
// exclusive token by value: the class Handle of PermittedCount in the check
// file of foundation/permissions/Permission.h.
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the door bump of the handle.

#include <foundation/Transfer.h>
#include <foundation/permissions/SharedPermission.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    struct Region {};
    struct CounterBrand final {};

    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    class Handle {
        using Guard = ::foundation::permissions::SharedPermissionGuard<Region, CounterBrand>;

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
