// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// encode_owned consumes a Linear-owned byte buffer.  A non-contiguous
// application struct must be serialized before FEC.  The codec and the
// Linear buffer come from their doors, so the buffer concept is the only
// refusal.

#include <crucible/cntp/Fec.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

struct NotWireBytes {
    std::uint64_t value = 0;
};

int main() {
    auto rs = crucible::cntp::mint_reed_solomon<4, 2>(::foundation::effects::testing::init());
    auto input = ::fixy::mint_linear<NotWireBytes>();
    std::array<std::byte, 6> output{};
    (void)rs.encode_owned(std::move(input), output);
    return 0;
}
