#pragma once

// The one check that a Cog can carry an offload.  The identity names a
// discovered Cog, its kind is one of the kinds that the offload runs on,
// and its target capabilities advertise the feature that the offload
// needs.  The DPU, switch, GPU and NIC offloads read this check, and each
// maps the refusals to its own error enum.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <type_traits>

namespace crucible::cog {

// The refusals of the identity check, spelled in the error enum of the
// caller.  A caller names each field, so no refusal takes a default.
template <class Error>
    requires std::is_scoped_enum_v<Error>
struct CogTargetRefusals {
    Error undiscovered;
    Error wrong_kind;
};

// The refusals of the full check: the two of the identity check, and the
// refusal of a Cog whose capabilities do not advertise the feature.
template <class Error>
    requires std::is_scoped_enum_v<Error>
struct OffloadTargetRefusals {
    Error undiscovered;
    Error wrong_kind;
    Error missing_feature;
};

// The capabilities hold a flag word of the enum that Feature belongs to.
// A feature of a different enum names no bit of this schema, so the check
// refuses it at compile time.
template <class Caps, auto Feature>
concept AdvertisesFeatureOf = requires(Caps const& caps) {
    { caps.features.test(Feature) } -> std::same_as<bool>;
};

// A discovered Cog of one of the named kinds.  A zero identifier is the
// sentinel for a Cog that discovery has not reached.  O(number of kinds).
template <CogKind... Kinds, class Error>
    requires(sizeof...(Kinds) > 0)
[[nodiscard]] constexpr std::expected<void, Error> validate_cog_target(CogIdentity const& target,
                                                                       CogTargetRefusals<Error> refusals) noexcept {
    if (target.uuid.is_zero()) {
        return std::unexpected(refusals.undiscovered);
    }
    if (!((target.kind == Kinds) || ...)) {
        return std::unexpected(refusals.wrong_kind);
    }
    return {};
}

// The identity check, then the feature.  The caller names the feature and
// the kinds as template arguments, so each offload states its target once.
template <auto Feature, CogKind... Kinds, class Caps, class Error>
    requires(sizeof...(Kinds) > 0) && AdvertisesFeatureOf<Caps, Feature>
[[nodiscard]] constexpr std::expected<void, Error>
validate_offload_target(CogIdentity const& target, Caps const& caps, OffloadTargetRefusals<Error> refusals) noexcept {
    auto identity = validate_cog_target<Kinds...>(target, CogTargetRefusals<Error>{
                                                              .undiscovered = refusals.undiscovered,
                                                              .wrong_kind = refusals.wrong_kind,
                                                          });
    if (!identity.has_value()) {
        return identity;
    }
    if (!caps.features.test(Feature)) {
        return std::unexpected(refusals.missing_feature);
    }
    return {};
}

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

[[nodiscard]] constexpr NvSwitchTargetCaps p4_caps() noexcept {
    NvSwitchTargetCaps caps{};
    caps.features.set(SwitchFeature::P4);
    return caps;
}

static_assert(validate_offload_target<SwitchFeature::P4, CogKind::NvSwitch>(switch_identity(CogKind::NvSwitch),
                                                                            p4_caps(), refusals)
                  .has_value());
static_assert(validate_offload_target<SwitchFeature::P4, CogKind::NicCard, CogKind::NvSwitch>(
                  switch_identity(CogKind::NicCard), p4_caps(), refusals)
                  .has_value());
static_assert(validate_offload_target<SwitchFeature::P4, CogKind::NvSwitch>(CogIdentity{}, p4_caps(), refusals).error()
              == Refusal::Undiscovered);
static_assert(validate_offload_target<SwitchFeature::P4, CogKind::NvSwitch>(switch_identity(CogKind::NicPort),
                                                                            p4_caps(), refusals)
                  .error()
              == Refusal::WrongKind);
static_assert(validate_offload_target<SwitchFeature::Sharp, CogKind::NvSwitch>(switch_identity(CogKind::NvSwitch),
                                                                               p4_caps(), refusals)
                  .error()
              == Refusal::MissingFeature);
static_assert(validate_cog_target<CogKind::NvmeNamespace, CogKind::NvmeDrive>(
                  switch_identity(CogKind::NvmeDrive),
                  CogTargetRefusals<Refusal>{.undiscovered = Refusal::Undiscovered, .wrong_kind = Refusal::WrongKind})
                  .has_value());
static_assert(AdvertisesFeatureOf<NvSwitchTargetCaps, SwitchFeature::Doca>);
static_assert(AdvertisesFeatureOf<GpuTargetCaps, GpuFeature::GpuDirectRdma>);
static_assert(!AdvertisesFeatureOf<NvSwitchTargetCaps, GpuFeature::GpuDirectRdma>);
static_assert(!AdvertisesFeatureOf<NicPortTargetCaps, SwitchFeature::Doca>);
static_assert(!AdvertisesFeatureOf<NvSwitchTargetCaps, 1>);

}  // namespace detail::offload_target_self_test

}  // namespace crucible::cog
