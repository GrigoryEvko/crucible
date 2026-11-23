// Negative compile test.  This file must fail to compile.
//
// mint_published takes the proof of the stage that publishes objects of the
// type (fixy/handle/Published.h).  A proof that names no published type
// proves nothing about the last write of the object, so the mint refuses it.

#include <fixy/handle/Published.h>

#include <foundation/core/Ref.h>

namespace {

struct Store final {};

struct Plain {
    using brand_type = Store;
    int value = 0;
};

struct NamesNothing {};

}  // namespace

int main() {
    Plain const object{};
    const auto published = ::fixy::handle::mint_published(NamesNothing{}, ::foundation::core::to(object));
    return published.get().value;
}
