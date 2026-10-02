#include <crucible/cntp/_wip/Sharp.h>
#include <fixy/Ctx.h>

#include <array>
#include "test_assert.h"
#include <concepts>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

// The context mint and the two dispatch calls carry a CRUCIBLE_STUB
// deprecation, because no SHARP runtime stands behind them.  This file calls
// them on purpose to pin their results, so the warning is suppressed for this
// file alone.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cog = crucible::cog;
namespace fe = ::foundation::effects;
namespace shp = crucible::cntp::_wip::sharp;

namespace {

struct GoodRecipe {
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::BITEXACT_TC;
};

struct StrictRecipe {
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
};

struct NonCommutativeRecipe {
    static constexpr bool associative = true;
    static constexpr bool commutative = false;
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::ORDERED;
};

cog::CogIdentity switch_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x133, 0x500};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NvSwitch;
    return id;
}

cog::NvSwitchTargetCaps switch_caps() {
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Sharp);
    return caps;
}

crucible::NumericalRecipe recipe(crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::ORDERED,
                                 crucible::ScalarType dtype = crucible::ScalarType::Float) {
    crucible::NumericalRecipe r{};
    r.accum_dtype = dtype;
    r.determinism = determinism;
    return r;
}

void test_admission_and_names() {
    assert(shp::sharp_error_name(shp::SharpError::RuntimeUnavailable) == std::string_view{"RuntimeUnavailable"});
    assert(shp::sharp_fallback_name(shp::SharpFallback::BitexactTree) == std::string_view{"BitexactTree"});

    auto count = shp::admit_sharp_participant_count(8);
    assert(count.has_value());
    assert(count->value() == 8);

    auto empty = shp::admit_sharp_participant_count(0);
    assert(!empty.has_value());
    assert(empty.error() == shp::SharpError::EmptyParticipantSet);

    static_assert(shp::SharpEligibleRecipe<GoodRecipe>);
    static_assert(!shp::SharpEligibleRecipe<StrictRecipe>);
    static_assert(!shp::SharpEligibleRecipe<NonCommutativeRecipe>);

    crucible::test::pass("  test_admission_and_names: PASSED\n");
}

void test_fabric_plan_minting() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan =
        shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(), *shp::admit_sharp_participant_count(8));
    assert(plan.has_value());
    static_assert(std::same_as<std::remove_cvref_t<decltype(*plan)>, shp::DeclaredSharpFabricPlan>);
    assert(plan->value().participant_count.value() == 8);

    auto no_cap = switch_caps();
    no_cap.features.unset(cog::SwitchFeature::Sharp);
    auto missing_cap =
        shp::mint_sharp_fabric_plan(init, switch_identity(), no_cap, *shp::admit_sharp_participant_count(8));
    assert(!missing_cap.has_value());
    assert(missing_cap.error() == shp::SharpError::MissingSwitchSharpCapability);

    auto wrong_kind = switch_identity();
    wrong_kind.kind = cog::CogKind::NicPort;
    auto non_switch =
        shp::mint_sharp_fabric_plan(init, wrong_kind, switch_caps(), *shp::admit_sharp_participant_count(8));
    assert(!non_switch.has_value());
    assert(non_switch.error() == shp::SharpError::NonSwitchCog);

    auto zero = switch_identity();
    zero.uuid = cog::Uuid{};
    auto zero_switch = shp::mint_sharp_fabric_plan(init, zero, switch_caps(), *shp::admit_sharp_participant_count(8));
    assert(!zero_switch.has_value());
    assert(zero_switch.error() == shp::SharpError::ZeroSwitchCog);

    crucible::test::pass("  test_fabric_plan_minting: PASSED\n");
}

void test_recipe_eligibility() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan =
        shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(), *shp::admit_sharp_participant_count(8));
    assert(plan.has_value());

    auto eligible = shp::eligibility_check(recipe(crucible::ReductionDeterminism::BITEXACT_TC),
                                           shp::sharp_recipe_laws<GoodRecipe>(), *plan);
    assert(eligible.has_value());
    assert(eligible->value().fallback == shp::SharpFallback::None);

    auto strict = shp::eligibility_check(recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                         shp::SharpRecipeLaws{.associative = true, .commutative = true}, *plan);
    assert(!strict.has_value());
    assert(strict.error() == shp::SharpError::BitexactStrictForbidden);

    auto non_commutative =
        shp::eligibility_check(recipe(), shp::SharpRecipeLaws{.associative = true, .commutative = false}, *plan);
    assert(!non_commutative.has_value());
    assert(non_commutative.error() == shp::SharpError::RecipeNotCommutative);

    auto unsupported_dtype =
        shp::eligibility_check(recipe(crucible::ReductionDeterminism::ORDERED, crucible::ScalarType::Bool),
                               shp::SharpRecipeLaws{.associative = true, .commutative = true}, *plan);
    assert(!unsupported_dtype.has_value());
    assert(unsupported_dtype.error() == shp::SharpError::UnsupportedScalarType);

    crucible::test::pass("  test_recipe_eligibility: PASSED\n");
}

void test_dispatch_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto plan =
        shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(), *shp::admit_sharp_participant_count(8));
    assert(plan.has_value());

    std::array<float, 4> input{1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4> output{};
    auto unavailable =
        shp::dispatch_sharp_allreduce(input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), *plan);
    assert(unavailable.has_value());
    assert(unavailable->value().fallback == shp::SharpFallback::RingOrTree);
    assert(unavailable->value().element_count == input.size());

    auto deferred_plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                                     *shp::admit_sharp_participant_count(8), true);
    assert(deferred_plan.has_value());
    auto deferred =
        shp::dispatch_sharp_allreduce(input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), *deferred_plan);
    assert(deferred.has_value());
    assert(deferred->value().fallback == shp::SharpFallback::RingOrTree);

    auto deferred_context = shp::mint_sharp_context(init, *deferred_plan);
    assert(!deferred_context.has_value());
    assert(deferred_context.error() == shp::SharpError::DispatchDeferred);
    auto unloaded_context = shp::mint_sharp_context(init, *plan);
    assert(!unloaded_context.has_value());
    assert(unloaded_context.error() == shp::SharpError::RuntimeUnavailable);

    auto backend_plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                                    *shp::admit_sharp_participant_count(8), true, true);
    assert(backend_plan.has_value());
    auto backend =
        shp::dispatch_sharp_allreduce(input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), *backend_plan);
    assert(backend.has_value());
    assert(backend->value().fallback == shp::SharpFallback::RingOrTree);

    auto strict_fallback =
        shp::dispatch_sharp_allreduce(input, output, recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                      shp::SharpRecipeLaws{.associative = true, .commutative = true}, *backend_plan);
    assert(strict_fallback.has_value());
    assert(strict_fallback->value().fallback == shp::SharpFallback::BitexactTree);

    std::array<float, 2> short_output{};
    auto shape = shp::dispatch_sharp_allreduce(input, short_output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(),
                                               *backend_plan);
    assert(!shape.has_value());
    assert(shape.error() == shp::SharpError::OutputShapeMismatch);

    auto context = shp::mint_sharp_context(init, *backend_plan);
    assert(context.has_value());
    assert(context->peek().switch_uuid() == switch_identity().uuid);
    assert(context->peek().participant_count().value() == 8);
    shp::SharpReducer reducer{std::move(*context)};
    auto reduced =
        reducer.allreduce_via_sharp(bg, input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), *backend_plan);
    assert(!reduced.has_value());
    assert(reduced.error() == shp::SharpError::VendorBackendUnavailable);

    auto other_plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                                  *shp::admit_sharp_participant_count(4), true, true);
    assert(other_plan.has_value());
    auto mismatch =
        reducer.allreduce_via_sharp(bg, input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), *other_plan);
    assert(!mismatch.has_value());
    assert(mismatch.error() == shp::SharpError::ParticipantCountMismatch);

    crucible::test::pass("  test_dispatch_boundary: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(shp::SharpParticipantCount) == sizeof(std::uint16_t));
    static_assert(sizeof(shp::DeclaredSharpFabricPlan) == sizeof(shp::SharpFabricPlan));
    static_assert(sizeof(shp::DeclaredSharpDispatch) == sizeof(shp::SharpDispatchResult));
    static_assert(std::same_as<shp::DeclaredSharpFabricPlan::tag_type, shp::wip_source::Sharp>);
    static_assert(shp::CtxFitsSharpMint<::fixy::ColdInitCtx>);
    static_assert(!shp::CtxFitsSharpMint<::fixy::BgDrainCtx>);
    static_assert(!shp::CtxFitsSharpMint<::fixy::HotFgCtx>);
    static_assert(shp::CtxFitsSharpDispatch<::fixy::BgDrainCtx>);
    static_assert(!shp::CtxFitsSharpDispatch<::fixy::ColdInitCtx>);
    static_assert(!shp::CtxFitsSharpDispatch<::fixy::HotFgCtx>);
    static_assert(std::is_trivially_copy_constructible_v<shp::SharpFabricPlan>);
    static_assert(std::is_trivially_copyable_v<shp::SharpDispatchResult>);
    static_assert(!std::is_default_constructible_v<shp::DeclaredSharpFabricPlan>);
    static_assert(!std::is_copy_constructible_v<shp::SharpContextHandle>);
    static_assert(!std::is_constructible_v<shp::SharpContextHandle, cog::Uuid, shp::SharpParticipantCount, bool, bool>);
    static_assert(!shp::sharp_backend_implemented,
                  "no SHARP runtime is linked, so the context mint and the dispatch calls are stubs");

    ::fixy::report(::fixy::Sink::Out, "test_cntp_sharp:\n");
    test_admission_and_names();
    test_fabric_plan_minting();
    test_recipe_eligibility();
    test_dispatch_boundary();
    crucible::test::pass("test_cntp_sharp: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
