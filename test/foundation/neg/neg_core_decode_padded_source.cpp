// The padding of a class holds bits that no member sets.  A decode from a
// padded source would make those bits value bits of the result, so decode
// refuses a source with padding.

#include <foundation/core/Scalar.h>

#include <cstdint>

struct Padded {
    std::uint32_t key = 0;
    std::uint16_t tag = 0;
};

int main() { return ::foundation::core::decode<std::uint64_t>(Padded{}).is_some() ? 0 : 1; }
