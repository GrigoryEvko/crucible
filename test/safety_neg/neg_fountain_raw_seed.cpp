// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A repair packet choice derives from the DetSafe Philox key chain.  A raw
// integer seed cannot cross the replay boundary, so start_encoding refuses
// it.  The encoder comes from its mint, so the seed is the one thing
// refused.

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <cstddef>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto encoder = crucible::cntp::mint_fountain_encoder<4, 16>(init);
    std::array<std::byte, 16> payload{};
    (void)encoder.start_encoding(payload, 42ULL);
    return 0;
}
