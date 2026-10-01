// The compile-time checks of crucible/cog/OffloadTarget.h.

#include <crucible/cog/OffloadTarget.h>

namespace crucible::cog {

namespace detail::offload_target_self_test {

enum class Refusal : std::uint8_t {
    Undiscovered,
    WrongKind,
    MissingFeature,
};

inline constexpr OffloadTargetRefusals<Refusal> refusals{
    .undiscovered = Refusal::Undiscovered,
    .wrong_kind = Refusal::WrongKind,
    .missing_feature = Refusal::MissingFeature,
};

[[nodiscard]] constexpr CogIdentity switch_identity(CogKind kind) noexcept {
    CogIdentity identity{};
    identity.uuid = Uuid{1, 2};
    identity.kind = kind;
    return identity;
}

[[nodiscard]] constexpr NvSwitchTargetCaps adaptive_routing_caps() noexcept {
    NvSwitchTargetCaps caps{};
    caps.features.set(SwitchFeature::AdaptiveRouting);
    return caps;
}

static_assert(validate_offload_target<SwitchFeature::AdaptiveRouting, CogKind::NvSwitch>(
                  switch_identity(CogKind::NvSwitch), adaptive_routing_caps(), refusals)
                  .has_value());
static_assert(validate_offload_target<SwitchFeature::AdaptiveRouting, CogKind::NicCard, CogKind::NvSwitch>(
                  switch_identity(CogKind::NicCard), adaptive_routing_caps(), refusals)
                  .has_value());
static_assert(validate_offload_target<SwitchFeature::AdaptiveRouting, CogKind::NvSwitch>(CogIdentity{},
                                                                                         adaptive_routing_caps(),
                                                                                         refusals)
                  .error()
              == Refusal::Undiscovered);
static_assert(validate_offload_target<SwitchFeature::AdaptiveRouting, CogKind::NvSwitch>(
                  switch_identity(CogKind::NicPort), adaptive_routing_caps(), refusals)
                  .error()
              == Refusal::WrongKind);
static_assert(validate_offload_target<SwitchFeature::Sharp, CogKind::NvSwitch>(switch_identity(CogKind::NvSwitch),
                                                                               adaptive_routing_caps(), refusals)
                  .error()
              == Refusal::MissingFeature);
static_assert(validate_cog_target<CogKind::NvmeNamespace, CogKind::NvmeDrive>(
                  switch_identity(CogKind::NvmeDrive),
                  CogTargetRefusals<Refusal>{.undiscovered = Refusal::Undiscovered, .wrong_kind = Refusal::WrongKind})
                  .has_value());
// NicFeature and SwitchFeature each have a Tcam atom, so the two lines on
// SwitchFeature::Tcam show that the check reads the enum of a feature and
// not its name.
static_assert(AdvertisesFeatureOf<NvSwitchTargetCaps, SwitchFeature::Tcam>);
static_assert(AdvertisesFeatureOf<GpuTargetCaps, GpuFeature::GpuDirectRdma>);
static_assert(!AdvertisesFeatureOf<NvSwitchTargetCaps, GpuFeature::GpuDirectRdma>);
static_assert(!AdvertisesFeatureOf<NicPortTargetCaps, SwitchFeature::Tcam>);
static_assert(!AdvertisesFeatureOf<NvSwitchTargetCaps, 1>);

}  // namespace detail::offload_target_self_test

}  // namespace crucible::cog
