// Negative compile test.  This file must fail to compile.
//
// storage_of gives a View of the bytes of an object, and the View borrows the
// object.  A temporary ends at the end of the full expression, so the View of
// its storage would dangle, and the overload for an rvalue is deleted.

#include <fixy/os/Pages.h>

#include <cstdint>

struct PaddedRecord {
    std::uint8_t tag = 0;
    std::uint64_t value = 0;
};

int main() {
    auto bytes = ::fixy::pages::storage_of(PaddedRecord{});
    (void)bytes;
    return 0;
}
