// The bytes of a record with a padding byte would carry a byte that no code
// wrote into a hash or onto a wire.  So bytes() refuses the record.

#include <foundation/core/Algorithms.h>

#include <cstdint>

struct Padded {
    std::uint8_t tag = 0;
    std::uint32_t value = 0;
};

inline auto image_of(Padded const& record) { return ::foundation::core::bytes(record); }

int main() {}
