// Negative compile test.  This file must fail to compile.
//
// storage_of gives the page range of the bytes of an object.  A temporary ends
// at the end of the full expression, so the range of its storage would name
// memory that no object holds, and the overload for an rvalue is deleted.

#include <fixy/os/Pages.h>

#include <cstdint>

struct PaddedRecord {
    std::uint8_t tag = 0;
    std::uint64_t value = 0;
};

int main() {
    auto range = ::fixy::pages::storage_of(PaddedRecord{});
    (void)range;
    return 0;
}
