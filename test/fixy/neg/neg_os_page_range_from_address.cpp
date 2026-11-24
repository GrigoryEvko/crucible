// Negative compile test.  This file must fail to compile.
//
// Only storage_of of fixy/os/Pages.h makes a page range, from an object or a
// run that the caller holds.  A range of a raw address would let a page door
// lock or advise memory that no owner of the caller holds.  So no constructor
// takes an address word and a count.

#include <fixy/os/Pages.h>

#include <cstddef>

int main() {
    ::fixy::pages::PageRange const range{::fixy::pages::PageWord{0x7f0000000000}, std::size_t{4096}};
    return static_cast<int>(range.size());
}
