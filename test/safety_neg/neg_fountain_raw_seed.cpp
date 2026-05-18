// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-119 fixture #4: fountain repair packet choices must derive from
// the DetSafe-pinned Philox key chain.  A raw integer seed cannot cross
// the deterministic replay boundary.

#include <crucible/cntp/Fountain.h>

#include <array>
#include <cstddef>

int main() {
    auto encoder = crucible::cntp::mint_fountain_encoder<4, 16>(
        crucible::effects::testing::init());
    std::array<std::byte, 16> payload{};
    (void)encoder.start_encoding(payload, 42ULL);
    return 0;
}
