// Negative compile test.  This file must fail to compile.
//
// A Published moves to another thread, so mint_published asks that two
// threads can read a const object of the type (fixy/handle/Published.h).  An
// object that holds a borrow of a plain int can reach an int that a reader writes,
// so the mint refuses it.

#include <fixy/handle/Published.h>

#include <foundation/core/Ref.h>

namespace {

struct Store final {};

struct HoldsBorrow {
    using brand_type = Store;
    int& target;
};

struct PublishesAddresses {
    using published_type = HoldsBorrow;
};

}  // namespace

int main() {
    int value = 0;
    HoldsBorrow const object{value};
    const auto published = ::fixy::handle::mint_published(PublishesAddresses{}, ::foundation::core::to(object));
    return published.get().target;
}
