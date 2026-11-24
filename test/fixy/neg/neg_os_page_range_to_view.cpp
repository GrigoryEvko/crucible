// Negative compile test.  This file must fail to compile.
//
// A page range of fixy/os/Pages.h holds the address word of the memory of an
// owner, and it can outlive that owner.  So it gives out no View: a View of a
// range would be a borrow that no owner keeps alive, and a read through it
// after the end of the owner would read freed memory.  No conversion makes a
// View of a range.

#include <fixy/os/Pages.h>

#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>

int main() {
    std::uint64_t const word = 7;
    ::fixy::pages::PageRange const range = ::fixy::pages::storage_of(word);
    ::foundation::core::View<std::byte const> const bytes = range;
    return static_cast<int>(bytes.size());
}
