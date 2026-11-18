// The cell of 128 bits compares the bits of a value.  A value with padding has
// bits that no member sets, so two equal values can compare as different, and
// the cell refuses such a value.

#include <foundation/os/WideAtomic.h>

#include <cstdint>

namespace {

// Twelve bytes of members and four bytes of padding.
struct AddressAndTag {
    std::uint64_t address = 0;
    std::uint32_t tag = 0;
};

}  // namespace

int main() { return static_cast<int>(::foundation::os::WideAtomic<AddressAndTag>{}.load_acquire().tag); }
