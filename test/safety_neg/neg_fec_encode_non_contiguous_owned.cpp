// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-117 fixture #5: encode_owned consumes a Linear-owned byte buffer.
// Non-contiguous application structs must be serialized before FEC.

#include <crucible/cntp/Fec.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

struct NotWireBytes {
    std::uint64_t value = 0;
};

int main() {
    auto rs = crucible::cntp::mint_reed_solomon<4, 2>(
        crucible::effects::Init{});
    crucible::cntp::LinearShardBuffer<NotWireBytes> input{NotWireBytes{}};
    std::array<std::byte, 6> output{};
    (void)rs.encode_owned(std::move(input), output);
    return 0;
}
