// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A Published moves to another thread, so mint_published asks that two
// threads can read a const object of the type (fixy/handle/Published.h).  An
// object that holds a borrow of a plain int can reach an int that a reader writes,
// so the mint refuses it.

#include <fixy/handle/Published.h>

namespace {

struct HoldsBorrow {
    int& target;
};

struct PublishesAddresses {
    using published_type = HoldsBorrow;
};

}  // namespace

int main() {
    int value = 0;
    HoldsBorrow const object{value};
    const auto published = ::fixy::handle::mint_published(PublishesAddresses{}, object);
    return published.get().target;
}
