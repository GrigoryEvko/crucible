// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A feature names a bit of the schema of its own enum.  A GPU feature is no
// bit of a switch schema, so the offload-target check refuses the pair.

#include <crucible/cog/OffloadTarget.h>

#include <cstdint>

namespace cog = crucible::cog;

enum class Refusal : std::uint8_t {
    Undiscovered,
    WrongKind,
    MissingFeature,
};

int main() {
    cog::CogIdentity target{};
    target.uuid = cog::Uuid{1, 2};
    target.kind = cog::CogKind::NvSwitch;
    const cog::NvSwitchTargetCaps caps{};
    auto checked = cog::validate_offload_target<cog::GpuFeature::GpuDirectRdma, cog::CogKind::NvSwitch>(
        target, caps,
        cog::OffloadTargetRefusals<Refusal>{
            .undiscovered = Refusal::Undiscovered,
            .wrong_kind = Refusal::WrongKind,
            .missing_feature = Refusal::MissingFeature,
        });
    return checked.has_value() ? 0 : 1;
}
