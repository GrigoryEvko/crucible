// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-119 fixture #5: encode_owned consumes a Linear-owned byte
// buffer.  Non-contiguous application structs must be serialized before
// entering the fountain encoder.

#include <crucible/cntp/Fountain.h>

#include <cstdint>
#include <utility>

struct NotWireBytes {
    std::uint64_t value = 0;
};

int main() {
    auto encoder = crucible::cntp::mint_fountain_encoder<4, 16>(
        crucible::effects::Init{});
    auto seed = crucible::Philox::op_key_det(
        1, 2, crucible::ContentHash{3});
    crucible::cntp::LinearFountainBuffer<NotWireBytes> input{NotWireBytes{}};
    (void)encoder.encode_owned(std::move(input), seed, 0);
    return 0;
}
