// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// encode_owned consumes a Linear byte buffer.  An application struct that
// is not contiguous bytes must be serialized before it enters the
// encoder, so encode_owned refuses it.

#include <crucible/cntp/Fountain.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <utility>

struct NotWireBytes {
    std::uint64_t value = 0;
};

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto encoder = crucible::cntp::mint_fountain_encoder<4, 16>(init);
    auto seed = crucible::Philox::op_key_det(1, 2, crucible::ContentHash{3});
    auto input = ::fixy::mint_linear<NotWireBytes>();
    (void)encoder.encode_owned(std::move(input), seed, 0);
    return 0;
}
