// The compile-time checks of crucible/cntp/Fountain.h.

#include <crucible/cntp/Fountain.h>

namespace crucible::cntp {

static_assert(FountainShape<8, 1024, 16>);
static_assert(!FountainShape<0, 1024, 16>);
static_assert(!FountainShape<8, 0, 16>);
static_assert(!FountainShape<65, 1024, 80>);
static_assert(::fixy::qtt_consume_tracked
              || sizeof(LinearFountainBuffer<::fixy::View<std::byte>>) == sizeof(::fixy::View<std::byte>));

// Each codec is reached only through its mint, and each mint takes a
// context that admits the initialization row.
static_assert(!std::is_default_constructible_v<FountainEncoder<8, 16>>);
static_assert(!std::is_default_constructible_v<FountainDecoder<8, 16>>);
static_assert(CtxFitsFountainMint<::fixy::ColdInitCtx, 8, 16, 8, LtFountain>);
static_assert(!CtxFitsFountainMint<::fixy::TestRunnerCtx, 8, 16, 8, LtFountain>,
              "a test context carries no initialization effect");
static_assert(!CtxFitsFountainMint<::fixy::BgDrainCtx, 8, 16, 8, LtFountain>,
              "a codec is built at start-up, not on a background drain");
static_assert(!CtxFitsFountainMint<::fixy::ColdInitCtx, 8, 16, 7, LtFountain>,
              "a decoder needs an equation slot for each source symbol");
static_assert(FountainPacket<8, 16>::admit_source_bytes(0).is_err());
static_assert(FountainPacket<8, 16>::admit_source_bytes(129).is_err());
static_assert(FountainPacket<8, 16>::admit_source_bytes(128).expect("the full capacity fits").value() == 128);

}  // namespace crucible::cntp
