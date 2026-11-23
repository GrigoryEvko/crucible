// Negative compile test.  This file must fail to compile.
//
// A Published keeps the brand of the store of its object, and the mint takes
// only a fresh brand (fixy/handle/Published.h).  A Ref on the erased brand
// names no store, so a Published of it could stand in for a Published of each
// store, and the mint refuses it.

#include <fixy/handle/Published.h>

#include <foundation/core/Ref.h>

namespace {

struct Plain {
    int value = 0;
};

struct PublishesPlain {
    using published_type = Plain;
};

}  // namespace

int main() {
    Plain const object{};
    const auto published = ::fixy::handle::mint_published(
        PublishesPlain{}, ::foundation::core::erase_brand(::foundation::core::to(object)));
    return published.get().value;
}
