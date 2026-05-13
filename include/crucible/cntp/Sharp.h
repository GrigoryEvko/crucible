#pragma once

// GAPS-133. CNT-P SHARP in-network reduction substrate.
//
// This header owns typed eligibility for Mellanox/NVIDIA SHARP-style
// in-fabric reductions. It deliberately does not link libsharp.so,
// program switch dataplanes, or implement software collective fallback.
// Live backends consume the declared plan/context and currently report
// explicit deferral / unavailability after the request shape is proven.

#include <crucible/NumericalRecipe.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::sharp {

enum class SharpError : std::uint8_t {
    None = 0,
    ZeroSwitchCog,
    NonSwitchCog,
    MissingSwitchSharpCapability,
    EmptyParticipantSet,
    ParticipantCountMismatch,
    RecipeNotAssociative,
    RecipeNotCommutative,
    BitexactStrictForbidden,
    UnsupportedScalarType,
    RuntimeUnavailable,
    DispatchDeferred,
    VendorBackendUnavailable,
    OutputShapeMismatch,
};

enum class SharpFallback : std::uint8_t {
    None = 0,
    RingOrTree,
    BitexactTree,
    SoftwareCollectiveCatalog,
};

struct AssociativeTrue {
    static constexpr bool associative = true;
};
struct AssociativeFalse {
    static constexpr bool associative = false;
};
struct CommutativeTrue {
    static constexpr bool commutative = true;
};
struct CommutativeFalse {
    static constexpr bool commutative = false;
};

struct SharpRecipeLaws {
    bool associative = false;
    bool commutative = false;
};

struct SharpDispatchResult {
    SharpFallback fallback = SharpFallback::RingOrTree;
    std::uint32_t participant_count = 0;
    std::uint64_t element_count = 0;
};

using SharpParticipantCount = safety::Positive<std::uint16_t>;
using DeclaredSharpDispatch =
    safety::Tagged<SharpDispatchResult, safety::source::Sharp>;

struct SharpFabricPlan {
    cog::CogIdentity fabric_switch{};
    SharpParticipantCount participant_count{std::uint16_t{1}};
    bool runtime_loaded = false;
    bool allow_backend_dispatch = false;
};

using DeclaredSharpFabricPlan =
    safety::Tagged<SharpFabricPlan, safety::source::Sharp>;

struct SharpContextHandle {
    cog::Uuid switch_uuid{};
    SharpParticipantCount participant_count{std::uint16_t{1}};
    bool runtime_loaded = false;
    bool allow_backend_dispatch = false;
};

using SharpContext = safety::Linear<SharpContextHandle>;

template <class Ctx>
concept CtxFitsSharpMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsSharpDispatch =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>;

template <class Recipe>
concept DeclaresAssociative =
    requires {
        { Recipe::associative } -> std::convertible_to<bool>;
    };

template <class Recipe>
concept DeclaresCommutative =
    requires {
        { Recipe::commutative } -> std::convertible_to<bool>;
    };

template <class Recipe>
concept DeclaresReductionDeterminism =
    requires {
        { Recipe::determinism } -> std::convertible_to<ReductionDeterminism>;
    };

template <class Recipe>
concept SharpEligibleRecipe =
    DeclaresAssociative<Recipe>
    && DeclaresCommutative<Recipe>
    && DeclaresReductionDeterminism<Recipe>
    && Recipe::associative
    && Recipe::commutative
    && Recipe::determinism != ReductionDeterminism::BITEXACT_STRICT;

[[nodiscard]] std::string_view sharp_error_name(SharpError error) noexcept;
[[nodiscard]] std::string_view sharp_fallback_name(SharpFallback fb) noexcept;

[[nodiscard]] constexpr bool sharp_scalar_supported(ScalarType dtype) noexcept {
    switch (dtype) {
        case ScalarType::Half:
        case ScalarType::Float:
        case ScalarType::Double:
        case ScalarType::BFloat16:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr std::expected<SharpParticipantCount, SharpError>
admit_sharp_participant_count(std::uint16_t count) noexcept {
    if (count == 0u) {
        return std::unexpected(SharpError::EmptyParticipantSet);
    }
    return SharpParticipantCount{
        count, typename SharpParticipantCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<void, SharpError>
validate_sharp_switch(cog::CogIdentity fabric_switch,
                      cog::NvSwitchTargetCaps const& caps) noexcept {
    if (fabric_switch.uuid.is_zero()) {
        return std::unexpected(SharpError::ZeroSwitchCog);
    }
    if (fabric_switch.kind != cog::CogKind::NvSwitch) {
        return std::unexpected(SharpError::NonSwitchCog);
    }
    if (!caps.features.test(cog::SwitchFeature::Sharp)) {
        return std::unexpected(SharpError::MissingSwitchSharpCapability);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, SharpError>
validate_sharp_recipe(SharpRecipeLaws laws,
                      NumericalRecipe const& recipe) noexcept {
    if (!laws.associative) {
        return std::unexpected(SharpError::RecipeNotAssociative);
    }
    if (!laws.commutative) {
        return std::unexpected(SharpError::RecipeNotCommutative);
    }
    if (recipe.determinism == ReductionDeterminism::BITEXACT_STRICT) {
        return std::unexpected(SharpError::BitexactStrictForbidden);
    }
    if (!sharp_scalar_supported(recipe.accum_dtype)) {
        return std::unexpected(SharpError::UnsupportedScalarType);
    }
    return {};
}

[[nodiscard]] constexpr SharpFallback fallback_for_ineligible(
    SharpError error) noexcept {
    switch (error) {
        case SharpError::BitexactStrictForbidden:
            return SharpFallback::BitexactTree;
        case SharpError::RecipeNotAssociative:
        case SharpError::RecipeNotCommutative:
        case SharpError::UnsupportedScalarType:
        case SharpError::MissingSwitchSharpCapability:
        case SharpError::RuntimeUnavailable:
        case SharpError::DispatchDeferred:
        case SharpError::VendorBackendUnavailable:
            return SharpFallback::RingOrTree;
        default:
            return SharpFallback::SoftwareCollectiveCatalog;
    }
}

template <class Recipe>
    requires SharpEligibleRecipe<Recipe>
[[nodiscard]] constexpr SharpRecipeLaws sharp_recipe_laws() noexcept {
    return SharpRecipeLaws{
        .associative = Recipe::associative,
        .commutative = Recipe::commutative,
    };
}

[[nodiscard]] constexpr std::expected<DeclaredSharpFabricPlan, SharpError>
validate_sharp_fabric_plan(SharpFabricPlan plan,
                           cog::NvSwitchTargetCaps const& caps) noexcept {
    auto switch_valid = validate_sharp_switch(plan.fabric_switch, caps);
    if (!switch_valid.has_value()) {
        return std::unexpected(switch_valid.error());
    }
    if (plan.participant_count.value() == 0u) {
        return std::unexpected(SharpError::EmptyParticipantSet);
    }
    return DeclaredSharpFabricPlan{plan};
}

template <class Ctx>
    requires CtxFitsSharpMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredSharpFabricPlan, SharpError>
mint_sharp_fabric_plan(Ctx const&,
                       cog::CogIdentity fabric_switch,
                       cog::NvSwitchTargetCaps caps,
                       SharpParticipantCount participant_count,
                       bool runtime_loaded = false,
                       bool allow_backend_dispatch = false) noexcept {
    return validate_sharp_fabric_plan(SharpFabricPlan{
        .fabric_switch = fabric_switch,
        .participant_count = participant_count,
        .runtime_loaded = runtime_loaded,
        .allow_backend_dispatch = allow_backend_dispatch,
    }, caps);
}

template <class Ctx>
    requires CtxFitsSharpMint<Ctx>
[[nodiscard]] constexpr std::expected<SharpContext, SharpError>
mint_sharp_context(Ctx const&, DeclaredSharpFabricPlan plan) noexcept {
    auto const& raw = plan.value();
    if (!raw.runtime_loaded) {
        return std::unexpected(SharpError::RuntimeUnavailable);
    }
    if (!raw.allow_backend_dispatch) {
        return std::unexpected(SharpError::DispatchDeferred);
    }
    return SharpContext{SharpContextHandle{
        .switch_uuid = raw.fabric_switch.uuid,
        .participant_count = raw.participant_count,
        .runtime_loaded = raw.runtime_loaded,
        .allow_backend_dispatch = raw.allow_backend_dispatch,
    }};
}

[[nodiscard]] constexpr std::expected<DeclaredSharpDispatch, SharpError>
eligibility_check(NumericalRecipe const& recipe,
                  SharpRecipeLaws laws,
                  DeclaredSharpFabricPlan plan) noexcept {
    auto recipe_valid = validate_sharp_recipe(laws, recipe);
    if (!recipe_valid.has_value()) {
        return std::unexpected(recipe_valid.error());
    }
    return DeclaredSharpDispatch{SharpDispatchResult{
        .fallback = SharpFallback::None,
        .participant_count = plan.value().participant_count.value(),
        .element_count = 0,
    }};
}

[[nodiscard]] constexpr DeclaredSharpDispatch
fallback_dispatch(SharpError reason,
                  DeclaredSharpFabricPlan plan,
                  std::uint64_t element_count = 0) noexcept {
    return DeclaredSharpDispatch{SharpDispatchResult{
        .fallback = fallback_for_ineligible(reason),
        .participant_count = plan.value().participant_count.value(),
        .element_count = element_count,
    }};
}

class SharpReducer : public safety::Pinned<SharpReducer> {
    SharpContext context_;

public:
    explicit SharpReducer(SharpContext context) noexcept
        : context_{std::move(context)} {}

    template <class Ctx>
        requires CtxFitsSharpDispatch<Ctx>
    [[nodiscard]] std::expected<DeclaredSharpDispatch, SharpError>
    allreduce_via_sharp(Ctx const&,
                        std::span<const float> input,
                        std::span<float> output,
                        NumericalRecipe const& recipe,
                        SharpRecipeLaws laws,
                        DeclaredSharpFabricPlan plan) noexcept {
        if (input.size() != output.size()) {
            return std::unexpected(SharpError::OutputShapeMismatch);
        }
        auto eligible = eligibility_check(recipe, laws, plan);
        if (!eligible.has_value()) {
            return std::unexpected(eligible.error());
        }
        auto const& handle = context_.peek();
        if (handle.switch_uuid != plan.value().fabric_switch.uuid
            || handle.participant_count.value()
                != plan.value().participant_count.value()) {
            return std::unexpected(SharpError::ParticipantCountMismatch);
        }
        if (!handle.runtime_loaded) {
            return std::unexpected(SharpError::RuntimeUnavailable);
        }
        if (!handle.allow_backend_dispatch) {
            return std::unexpected(SharpError::DispatchDeferred);
        }
        return std::unexpected(SharpError::VendorBackendUnavailable);
    }
};

[[nodiscard]] std::expected<DeclaredSharpDispatch, SharpError>
dispatch_sharp_allreduce(std::span<const float> input,
                         std::span<float> output,
                         NumericalRecipe const& recipe,
                         SharpRecipeLaws laws,
                         DeclaredSharpFabricPlan plan) noexcept;

static_assert(sizeof(SharpParticipantCount) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredSharpFabricPlan) == sizeof(SharpFabricPlan));
static_assert(sizeof(DeclaredSharpDispatch) == sizeof(SharpDispatchResult));
static_assert(sizeof(SharpContext) == sizeof(SharpContextHandle));
static_assert(CtxFitsSharpMint<effects::ColdInitCtx>);
static_assert(!CtxFitsSharpMint<effects::BgDrainCtx>);
static_assert(CtxFitsSharpDispatch<effects::BgDrainCtx>);
static_assert(!CtxFitsSharpDispatch<effects::ColdInitCtx>);
static_assert(std::is_trivially_copyable_v<SharpDispatchResult>);
static_assert(std::is_trivially_copyable_v<SharpFabricPlan>);
static_assert(std::is_trivially_copyable_v<SharpContextHandle>);

}  // namespace crucible::cntp::sharp
