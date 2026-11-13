#include <crucible/cntp/_wip/Sharp.h>

namespace crucible::cntp::_wip::sharp {

::fixy::TextView sharp_error_name(SharpError error) noexcept { return ::foundation::core::enum_name_text(error); }

::fixy::TextView sharp_fallback_name(SharpFallback fallback) noexcept {
    return ::foundation::core::enum_name_text(fallback);
}

::fixy::Result<::fixy::Unit, SharpError> validate_sharp_recipe(SharpRecipeLaws laws,
                                                               NumericalRecipe const& recipe) noexcept {
    if (!laws.associative) {
        return ::fixy::err(SharpError::RecipeNotAssociative);
    }
    if (!laws.commutative) {
        return ::fixy::err(SharpError::RecipeNotCommutative);
    }
    if (recipe.determinism == ReductionDeterminism::BITEXACT_STRICT) {
        return ::fixy::err(SharpError::BitexactStrictForbidden);
    }
    if (!sharp_scalar_supported(recipe.accum_dtype)) {
        return ::fixy::err(SharpError::UnsupportedScalarType);
    }
    return ::fixy::Unit{};
}

::fixy::Result<DeclaredSharpDispatch, SharpError> eligibility_check(NumericalRecipe const& recipe, SharpRecipeLaws laws,
                                                                    DeclaredSharpFabricPlan plan) noexcept {
    CRUCIBLE_TRY(_, validate_sharp_recipe(laws, recipe));
    return ::fixy::mint_tagged<wip_source::Sharp>(SharpDispatchResult{
        .fallback = SharpFallback::None,
        .participant_count = plan.value().participant_count.value(),
        .element_count = 0,
    });
}

::fixy::Result<DeclaredSharpDispatch, SharpError>
SharpReducer::run_allreduce_(::fixy::View<float const> input, ::fixy::View<float> output, NumericalRecipe const& recipe,
                             SharpRecipeLaws laws, DeclaredSharpFabricPlan plan) const noexcept {
    if (input.size() != output.size()) {
        return ::fixy::err(SharpError::OutputShapeMismatch);
    }
    CRUCIBLE_TRY(_, eligibility_check(recipe, laws, plan));
    auto const& handle = context_.peek();
    if (handle.switch_uuid() != plan.value().fabric_switch.uuid
        || handle.participant_count().value() != plan.value().participant_count.value()) {
        return ::fixy::err(SharpError::ParticipantCountMismatch);
    }
    if (!handle.runtime_loaded()) {
        return ::fixy::err(SharpError::RuntimeUnavailable);
    }
    if (!handle.allow_backend_dispatch()) {
        return ::fixy::err(SharpError::DispatchDeferred);
    }
    return ::fixy::err(SharpError::VendorBackendUnavailable);
}

::fixy::Result<DeclaredSharpDispatch, SharpError>
dispatch_sharp_allreduce(::fixy::View<float const> input, ::fixy::View<float> output, NumericalRecipe const& recipe,
                         SharpRecipeLaws laws, DeclaredSharpFabricPlan plan) noexcept {
    if (input.size() != output.size()) {
        return ::fixy::err(SharpError::OutputShapeMismatch);
    }
    std::uint64_t const element_count = input.size();
    // The Option of the refusal holds a value only for a recipe that no
    // switch reduces, so the loop runs at most one time.
    for (SharpError const refusal : eligibility_check(recipe, laws, plan).err()) {
        return fallback_dispatch(refusal, plan, element_count);
    }
    if (!plan.value().runtime_loaded) {
        return fallback_dispatch(SharpError::RuntimeUnavailable, plan, element_count);
    }
    if (!plan.value().allow_backend_dispatch) {
        return fallback_dispatch(SharpError::DispatchDeferred, plan, element_count);
    }
    return fallback_dispatch(SharpError::VendorBackendUnavailable, plan, element_count);
}

}  // namespace crucible::cntp::_wip::sharp
