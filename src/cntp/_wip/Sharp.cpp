#include <crucible/cntp/_wip/Sharp.h>

namespace crucible::cntp::_wip::sharp {

std::string_view sharp_error_name(SharpError error) noexcept {
    switch (error) {
        case SharpError::None: return "None";
        case SharpError::ZeroSwitchCog: return "ZeroSwitchCog";
        case SharpError::NonSwitchCog: return "NonSwitchCog";
        case SharpError::MissingSwitchSharpCapability:
            return "MissingSwitchSharpCapability";
        case SharpError::EmptyParticipantSet: return "EmptyParticipantSet";
        case SharpError::ParticipantCountMismatch:
            return "ParticipantCountMismatch";
        case SharpError::RecipeNotAssociative: return "RecipeNotAssociative";
        case SharpError::RecipeNotCommutative: return "RecipeNotCommutative";
        case SharpError::BitexactStrictForbidden:
            return "BitexactStrictForbidden";
        case SharpError::UnsupportedScalarType: return "UnsupportedScalarType";
        case SharpError::RuntimeUnavailable: return "RuntimeUnavailable";
        case SharpError::DispatchDeferred: return "DispatchDeferred";
        case SharpError::VendorBackendUnavailable:
            return "VendorBackendUnavailable";
        case SharpError::OutputShapeMismatch: return "OutputShapeMismatch";
        default: return "<unknown SharpError>";
    }
}

std::string_view sharp_fallback_name(SharpFallback fb) noexcept {
    switch (fb) {
        case SharpFallback::None: return "None";
        case SharpFallback::RingOrTree: return "RingOrTree";
        case SharpFallback::BitexactTree: return "BitexactTree";
        case SharpFallback::SoftwareCollectiveCatalog:
            return "SoftwareCollectiveCatalog";
        default: return "<unknown SharpFallback>";
    }
}

std::expected<DeclaredSharpDispatch, SharpError>
dispatch_sharp_allreduce(std::span<const float> input,
                         std::span<float> output,
                         NumericalRecipe const& recipe,
                         SharpRecipeLaws laws,
                         DeclaredSharpFabricPlan plan) noexcept {
    if (input.size() != output.size()) {
        return std::unexpected(SharpError::OutputShapeMismatch);
    }
    auto eligible = eligibility_check(recipe, laws, plan);
    if (!eligible.has_value()) {
        return fallback_dispatch(eligible.error(), plan, input.size());
    }
    if (!plan.value().runtime_loaded) {
        return fallback_dispatch(
            SharpError::RuntimeUnavailable, plan, input.size());
    }
    if (!plan.value().allow_backend_dispatch) {
        return fallback_dispatch(
            SharpError::DispatchDeferred, plan, input.size());
    }
    return fallback_dispatch(
        SharpError::VendorBackendUnavailable, plan, input.size());
}

}  // namespace crucible::cntp::_wip::sharp
