// Negative compile test.  This file must fail to compile.
//
// mint_published takes the proof of the stage that publishes objects of the
// type (fixy/handle/Published.h).  A proof that names no published type
// proves nothing about the last write of the object, so the mint refuses it.

#include <fixy/handle/Published.h>

namespace {

struct Plain {
    int value = 0;
};

struct NamesNothing {};

}  // namespace

int main() {
    Plain const object{};
    const auto published = ::fixy::handle::mint_published(NamesNothing{}, object);
    return published.get().value;
}
