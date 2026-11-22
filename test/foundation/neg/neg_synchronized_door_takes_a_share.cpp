// Negative-compile test.  This file must fail to compile.
//
// A door of a class that states synchronized{} writes the plain count of the
// class, and it takes the guard of a share of an own tag as its proof.  Many
// holders hold a share of one pool at one time, so two holders write the
// count at the same time.  The guard states shares{}, and a share proves no
// door (Share of foundation/Transfer.h).
//
// Expected diagnostic: the door add_shared takes no proof of the class.

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

    void add_shared(::foundation::permissions::SharedPermissionGuard<Region, CounterBrand>& /*guard*/) noexcept {
        ++count_;
    }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
