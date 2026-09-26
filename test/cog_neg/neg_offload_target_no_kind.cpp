// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// An offload names at least one kind of Cog that it runs on.  A check that
// names no kind would refuse every Cog, so it does not compile.

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
    auto checked = cog::validate_offload_target<cog::SwitchFeature::P4>(
        target, caps,
        cog::OffloadTargetRefusals<Refusal>{
            .undiscovered = Refusal::Undiscovered,
            .wrong_kind = Refusal::WrongKind,
            .missing_feature = Refusal::MissingFeature,
        });
    return checked.has_value() ? 0 : 1;
}
