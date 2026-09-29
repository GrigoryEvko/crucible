#pragma once

// Nothing here links libsharp, programs a switch dataplane or implements the
// software collective that the SharpFallback values name.  The dispatch path
// proves the request shape and then reports deferral or unavailability.

#include <crucible/NumericalRecipe.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OffloadTarget.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::_wip::sharp {

namespace wip_source {
struct Sharp {};
}  // namespace wip_source

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

using SharpParticipantCount = ::fixy::Positive<std::uint16_t>;
using DeclaredSharpDispatch = ::fixy::Tagged<SharpDispatchResult, wip_source::Sharp>;

// The participant count takes no default, so a plan names the count that
// its caller admitted and no plan exists before it.
struct SharpFabricPlan {
    cog::CogIdentity fabric_switch{};
    SharpParticipantCount participant_count;
    bool runtime_loaded = false;
    bool allow_backend_dispatch = false;
};

// mint_sharp_fabric_plan is the one function that returns a declared plan.
using DeclaredSharpFabricPlan = ::fixy::Tagged<SharpFabricPlan, wip_source::Sharp>;

template <class Ctx>
concept CtxFitsSharpMint = ::foundation::effects::IsExecCtx<Ctx>
                        && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

template <class Ctx>
concept CtxFitsSharpDispatch = ::foundation::effects::IsExecCtx<Ctx>
                            && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

class SharpContextHandle;
using SharpContext = ::fixy::Linear<SharpContextHandle>;

// The handle of a SHARP context on one switch.  The constructor is private
// and mint_sharp_context is its one door.  A handle names one context, so it
// moves and does not copy: a copy would let a second owner be minted from
// the first.
class SharpContextHandle {
public:
    SharpContextHandle(SharpContextHandle const&) = delete("a SHARP context handle names one context");
    SharpContextHandle& operator=(SharpContextHandle const&) = delete("a SHARP context handle names one context");
    SharpContextHandle(SharpContextHandle&&) noexcept = default;
    SharpContextHandle& operator=(SharpContextHandle&&) noexcept = default;
    ~SharpContextHandle() = default;

    [[nodiscard]] constexpr cog::Uuid switch_uuid() const noexcept { return switch_uuid_; }
    [[nodiscard]] constexpr SharpParticipantCount participant_count() const noexcept { return participant_count_; }
    [[nodiscard]] constexpr bool runtime_loaded() const noexcept { return runtime_loaded_; }
    [[nodiscard]] constexpr bool allow_backend_dispatch() const noexcept { return allow_backend_dispatch_; }

private:
    constexpr SharpContextHandle(cog::Uuid switch_uuid, SharpParticipantCount participant_count, bool runtime_loaded,
                                 bool allow_backend_dispatch) noexcept
        : switch_uuid_{switch_uuid},
          participant_count_{participant_count},
          runtime_loaded_{runtime_loaded},
          allow_backend_dispatch_{allow_backend_dispatch} {}

    template <class Ctx>
        requires CtxFitsSharpMint<Ctx>
    friend constexpr std::expected<SharpContext, SharpError> mint_sharp_context(Ctx const&,
                                                                                DeclaredSharpFabricPlan plan) noexcept;

    cog::Uuid switch_uuid_{};
    SharpParticipantCount participant_count_;
    bool runtime_loaded_ = false;
    bool allow_backend_dispatch_ = false;
};

template <class Recipe>
concept DeclaresAssociative = requires {
    { Recipe::associative } -> std::convertible_to<bool>;
};

template <class Recipe>
concept DeclaresCommutative = requires {
    { Recipe::commutative } -> std::convertible_to<bool>;
};

template <class Recipe>
concept DeclaresReductionDeterminism = requires {
    { Recipe::determinism } -> std::convertible_to<ReductionDeterminism>;
};

template <class Recipe>
concept SharpEligibleRecipe =
    DeclaresAssociative<Recipe> && DeclaresCommutative<Recipe> && DeclaresReductionDeterminism<Recipe>
    && Recipe::associative && Recipe::commutative && Recipe::determinism != ReductionDeterminism::BITEXACT_STRICT;

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
    return ::fixy::admit_refined<::fixy::positive>(count, SharpError::EmptyParticipantSet);
}

// SHARP reduces inside a switch whose capabilities advertise it.
inline constexpr cog::OffloadTargetRefusals<SharpError> sharp_target_refusals{
    .undiscovered = SharpError::ZeroSwitchCog,
    .wrong_kind = SharpError::NonSwitchCog,
    .missing_feature = SharpError::MissingSwitchSharpCapability,
};

[[nodiscard]] constexpr std::expected<void, SharpError>
validate_sharp_switch(cog::CogIdentity const& fabric_switch, cog::NvSwitchTargetCaps const& caps) noexcept {
    return cog::validate_offload_target<cog::SwitchFeature::Sharp, cog::CogKind::NvSwitch>(fabric_switch, caps,
                                                                                           sharp_target_refusals);
}

[[nodiscard]] constexpr std::expected<void, SharpError> validate_sharp_recipe(SharpRecipeLaws laws,
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

[[nodiscard]] constexpr SharpFallback fallback_for_ineligible(SharpError error) noexcept {
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

// The refined count already holds this bound.  The check reads it again,
// so a count that entered through mint_refined_trusted is still refused
// here.
[[nodiscard]] constexpr std::expected<void, SharpError>
validate_sharp_fabric_plan(SharpFabricPlan const& plan, cog::NvSwitchTargetCaps const& caps) noexcept {
    auto switch_valid = validate_sharp_switch(plan.fabric_switch, caps);
    if (!switch_valid.has_value()) {
        return switch_valid;
    }
    if (plan.participant_count.value() == 0u) {
        return std::unexpected(SharpError::EmptyParticipantSet);
    }
    return {};
}

template <class Ctx>
    requires CtxFitsSharpMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredSharpFabricPlan, SharpError>
mint_sharp_fabric_plan(Ctx const&, cog::CogIdentity fabric_switch, cog::NvSwitchTargetCaps const& caps,
                       SharpParticipantCount participant_count, bool runtime_loaded = false,
                       bool allow_backend_dispatch = false) noexcept {
    const SharpFabricPlan plan{
        .fabric_switch = fabric_switch,
        .participant_count = participant_count,
        .runtime_loaded = runtime_loaded,
        .allow_backend_dispatch = allow_backend_dispatch,
    };
    auto valid = validate_sharp_fabric_plan(plan, caps);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return ::fixy::mint_tagged<wip_source::Sharp>(plan);
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
    return ::fixy::mint_linear<SharpContextHandle>(SharpContextHandle{
        raw.fabric_switch.uuid,
        raw.participant_count,
        raw.runtime_loaded,
        raw.allow_backend_dispatch,
    });
}

[[nodiscard]] constexpr std::expected<DeclaredSharpDispatch, SharpError>
eligibility_check(NumericalRecipe const& recipe, SharpRecipeLaws laws, DeclaredSharpFabricPlan plan) noexcept {
    auto recipe_valid = validate_sharp_recipe(laws, recipe);
    if (!recipe_valid.has_value()) {
        return std::unexpected(recipe_valid.error());
    }
    return ::fixy::mint_tagged<wip_source::Sharp>(SharpDispatchResult{
        .fallback = SharpFallback::None,
        .participant_count = plan.value().participant_count.value(),
        .element_count = 0,
    });
}

[[nodiscard]] constexpr DeclaredSharpDispatch fallback_dispatch(SharpError reason, DeclaredSharpFabricPlan plan,
                                                                std::uint64_t element_count = 0) noexcept {
    return ::fixy::mint_tagged<wip_source::Sharp>(SharpDispatchResult{
        .fallback = fallback_for_ineligible(reason),
        .participant_count = plan.value().participant_count.value(),
        .element_count = element_count,
    });
}

// A reducer takes a SHARP context, and only mint_sharp_context makes one.
class SharpReducer : public ::foundation::Pinned<SharpReducer> {
    SharpContext context_;

public:
    explicit SharpReducer(SharpContext context) noexcept : context_{std::move(context)} {}

    template <class Ctx>
        requires CtxFitsSharpDispatch<Ctx>
    [[nodiscard]] std::expected<DeclaredSharpDispatch, SharpError>
    allreduce_via_sharp(Ctx const&, std::span<const float> input, std::span<float> output,
                        NumericalRecipe const& recipe, SharpRecipeLaws laws, DeclaredSharpFabricPlan plan) noexcept {
        if (input.size() != output.size()) {
            return std::unexpected(SharpError::OutputShapeMismatch);
        }
        auto eligible = eligibility_check(recipe, laws, plan);
        if (!eligible.has_value()) {
            return std::unexpected(eligible.error());
        }
        auto const& handle = context_.peek();
        if (handle.switch_uuid() != plan.value().fabric_switch.uuid
            || handle.participant_count().value() != plan.value().participant_count.value()) {
            return std::unexpected(SharpError::ParticipantCountMismatch);
        }
        if (!handle.runtime_loaded()) {
            return std::unexpected(SharpError::RuntimeUnavailable);
        }
        if (!handle.allow_backend_dispatch()) {
            return std::unexpected(SharpError::DispatchDeferred);
        }
        return std::unexpected(SharpError::VendorBackendUnavailable);
    }
};

[[nodiscard]] std::expected<DeclaredSharpDispatch, SharpError>
dispatch_sharp_allreduce(std::span<const float> input, std::span<float> output, NumericalRecipe const& recipe,
                         SharpRecipeLaws laws, DeclaredSharpFabricPlan plan) noexcept;

static_assert(sizeof(SharpParticipantCount) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredSharpFabricPlan) == sizeof(SharpFabricPlan));
static_assert(sizeof(DeclaredSharpDispatch) == sizeof(SharpDispatchResult));
static_assert(::fixy::qtt_consume_tracked || sizeof(SharpContext) == sizeof(SharpContextHandle));
static_assert(CtxFitsSharpMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSharpMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsSharpDispatch<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSharpDispatch<::fixy::ColdInitCtx>);
static_assert(std::is_trivially_copyable_v<SharpDispatchResult>);
// A refined member makes a plan not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<SharpFabricPlan>
              && std::is_trivially_destructible_v<SharpFabricPlan>);
// No declared plan exists before its count, and no context handle exists
// outside mint_sharp_context or beside the one it names.
static_assert(!std::is_default_constructible_v<DeclaredSharpFabricPlan>);
static_assert(!std::is_constructible_v<SharpContextHandle, cog::Uuid, SharpParticipantCount, bool, bool>);
static_assert(!std::is_copy_constructible_v<SharpContextHandle>
              && std::is_nothrow_move_constructible_v<SharpContextHandle>);

}  // namespace crucible::cntp::_wip::sharp
