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

}  // namespace crucible::cog
