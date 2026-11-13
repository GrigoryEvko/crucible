// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The bitwise compare of the detector reads each byte of a result.  No
// replica writes a padding byte, so two equal results can differ there and
// give a false mismatch.  A result with a padding byte does not satisfy
// SdcComparableResult, so run_with_redundancy refuses it.

#include <crucible/observe/SdcDetect.h>

#include <cstdint>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace observe = crucible::observe;

namespace {

// One byte, three bytes of padding, and a 32-bit word.
struct PaddedResult {
    std::uint8_t flag = 0;
    std::uint32_t value = 0;
};

}  // namespace

int main() {
    auto detector =
        observe::mint_sdc_detector<eff::ColdInitCtx, 2, 4>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    static_cast<void>(
        sizeof(detector.run_with_redundancy(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                                            [](cog::CogIdentity const&) noexcept { return PaddedResult{}; })));
    return 0;
}
