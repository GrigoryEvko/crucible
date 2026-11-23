#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// A door of the swapper takes the time of the call as a count of
// nanoseconds of the swap clock.  A raw integer has no conversion to that
// count, so a number of another unit, such as a count of microseconds,
// cannot stand for the time.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto swapper = cntp::mint_path_swapper(init);
    std::uint64_t const now_us = 5;
    auto result = swapper.receiver_accepts_bidir(bg, now_us);
    (void)result;
    return 0;
}
