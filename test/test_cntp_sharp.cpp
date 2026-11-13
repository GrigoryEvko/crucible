#include <crucible/cntp/_wip/Sharp.h>
#include <fixy/Ctx.h>

#include "test_assert.h"
#include <concepts>
#include <cstdio>
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

// The error of a context mint.  A context is linear, so the context of a
// mint that succeeds ends here, and the error then holds no value.
[[nodiscard]] ::fixy::Option<shp::SharpError> context_error(::fixy::Result<shp::SharpContext, shp::SharpError> minted) {
    return std::move(minted).match(
        [](shp::SharpContext context) noexcept {
            drop(std::move(context));
            return ::fixy::Option<shp::SharpError>{::fixy::none};
        },
        [](shp::SharpError error) noexcept { return ::fixy::some(error); });
}

// The value of a dispatch that succeeds in this test.
[[nodiscard]] shp::DeclaredSharpDispatch take(::fixy::Result<shp::DeclaredSharpDispatch, shp::SharpError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

void test_admission_and_names() {
    assert(shp::sharp_error_name(shp::SharpError::RuntimeUnavailable) == "RuntimeUnavailable");
    assert(shp::sharp_fallback_name(shp::SharpFallback::BitexactTree) == "BitexactTree");

    auto count = shp::admit_sharp_participant_count(8);
    assert(count.is_ok());
    assert(std::move(count).expect("8 participants is positive").value() == 8);

    auto empty = shp::admit_sharp_participant_count(0);
    assert(empty.is_err());
    assert(empty.err() == ::fixy::some(shp::SharpError::EmptyParticipantSet));

    static_assert(shp::SharpEligibleRecipe<GoodRecipe>);
    static_assert(!shp::SharpEligibleRecipe<StrictRecipe>);
    static_assert(!shp::SharpEligibleRecipe<NonCommutativeRecipe>);

    crucible::test::pass("  test_admission_and_names: PASSED\n");
}

void test_fabric_plan_minting() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");
    static_assert(std::same_as<decltype(plan), shp::DeclaredSharpFabricPlan>);
    assert(plan.value().participant_count.value() == 8);

    auto no_cap = switch_caps();
    no_cap.features.unset(cog::SwitchFeature::Sharp);
    auto missing_cap = shp::mint_sharp_fabric_plan(
        init, switch_identity(), no_cap, shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    assert(missing_cap.err() == ::fixy::some(shp::SharpError::MissingSwitchSharpCapability));

    auto wrong_kind = switch_identity();
    wrong_kind.kind = cog::CogKind::NicPort;
    auto non_switch = shp::mint_sharp_fabric_plan(
        init, wrong_kind, switch_caps(), shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    assert(non_switch.err() == ::fixy::some(shp::SharpError::NonSwitchCog));

    auto zero = switch_identity();
    zero.uuid = cog::Uuid{};
    auto zero_switch = shp::mint_sharp_fabric_plan(
        init, zero, switch_caps(), shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    assert(zero_switch.err() == ::fixy::some(shp::SharpError::ZeroSwitchCog));

    crucible::test::pass("  test_fabric_plan_minting: PASSED\n");
}

void test_recipe_eligibility() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");

    shp::DeclaredSharpDispatch const eligible = take(shp::eligibility_check(
        recipe(crucible::ReductionDeterminism::BITEXACT_TC), shp::sharp_recipe_laws<GoodRecipe>(), plan));
    assert(eligible.value().fallback == shp::SharpFallback::None);
    assert(eligible.value().participant_count == 8);

    auto strict = shp::eligibility_check(recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                         shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
    assert(strict.err() == ::fixy::some(shp::SharpError::BitexactStrictForbidden));

    auto non_associative =
        shp::eligibility_check(recipe(), shp::SharpRecipeLaws{.associative = false, .commutative = true}, plan);
    assert(non_associative.err() == ::fixy::some(shp::SharpError::RecipeNotAssociative));

    auto non_commutative =
        shp::eligibility_check(recipe(), shp::SharpRecipeLaws{.associative = true, .commutative = false}, plan);
    assert(non_commutative.err() == ::fixy::some(shp::SharpError::RecipeNotCommutative));

    auto unsupported_dtype =
        shp::eligibility_check(recipe(crucible::ReductionDeterminism::ORDERED, crucible::ScalarType::Bool),
                               shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
    assert(unsupported_dtype.err() == ::fixy::some(shp::SharpError::UnsupportedScalarType));

    crucible::test::pass("  test_recipe_eligibility: PASSED\n");
}

void test_dispatch_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");

    ::foundation::core::FixedArray<float, 4> const input_values{1.0f, 2.0f, 3.0f, 4.0f};
    ::foundation::core::FixedArray<float, 4> output_values{};
    ::fixy::View<float const> const input = input_values.view();
    ::fixy::View<float> const output = output_values.view();
    shp::DeclaredSharpDispatch const unavailable =
        take(shp::dispatch_sharp_allreduce(input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), plan));
    assert(unavailable.value().fallback == shp::SharpFallback::RingOrTree);
    assert(unavailable.value().element_count == input.size());

    auto deferred_plan =
        shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                    shp::admit_sharp_participant_count(8).expect("8 participants is positive"), true)
            .expect("a loaded runtime makes a plan");
    shp::DeclaredSharpDispatch const deferred = take(
        shp::dispatch_sharp_allreduce(input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), deferred_plan));
    assert(deferred.value().fallback == shp::SharpFallback::RingOrTree);

    assert(context_error(shp::mint_sharp_context(init, deferred_plan))
           == ::fixy::some(shp::SharpError::DispatchDeferred));
    assert(context_error(shp::mint_sharp_context(init, plan)) == ::fixy::some(shp::SharpError::RuntimeUnavailable));

    auto backend_plan = shp::mint_sharp_fabric_plan(
                            init, switch_identity(), switch_caps(),
                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"), true, true)
                            .expect("a plan that asks for the backend makes a plan");
    shp::DeclaredSharpDispatch const backend = take(
        shp::dispatch_sharp_allreduce(input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), backend_plan));
    assert(backend.value().fallback == shp::SharpFallback::RingOrTree);

    shp::DeclaredSharpDispatch const strict_fallback = take(
        shp::dispatch_sharp_allreduce(input, output, recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                      shp::SharpRecipeLaws{.associative = true, .commutative = true}, backend_plan));
    assert(strict_fallback.value().fallback == shp::SharpFallback::BitexactTree);

    ::foundation::core::FixedArray<float, 2> short_values{};
    auto shape = shp::dispatch_sharp_allreduce(input, short_values.view(), recipe(),
                                               shp::sharp_recipe_laws<GoodRecipe>(), backend_plan);
    assert(shape.err() == ::fixy::some(shp::SharpError::OutputShapeMismatch));

    shp::SharpContext context =
        shp::mint_sharp_context(init, backend_plan).expect("a plan that asks for the backend makes a context");
    assert(context.peek().switch_uuid() == switch_identity().uuid);
    assert(context.peek().participant_count().value() == 8);
    shp::SharpReducer reducer{std::move(context)};
    auto reduced =
        reducer.allreduce_via_sharp(bg, input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), backend_plan);
    assert(reduced.err() == ::fixy::some(shp::SharpError::VendorBackendUnavailable));

    auto short_reduced = reducer.allreduce_via_sharp(bg, input, short_values.view(), recipe(),
                                                     shp::sharp_recipe_laws<GoodRecipe>(), backend_plan);
    assert(short_reduced.err() == ::fixy::some(shp::SharpError::OutputShapeMismatch));

    auto other_plan = shp::mint_sharp_fabric_plan(
                          init, switch_identity(), switch_caps(),
                          shp::admit_sharp_participant_count(4).expect("4 participants is positive"), true, true)
                          .expect("a plan of four participants makes a plan");
    auto mismatch =
        reducer.allreduce_via_sharp(bg, input, output, recipe(), shp::sharp_recipe_laws<GoodRecipe>(), other_plan);
    assert(mismatch.err() == ::fixy::some(shp::SharpError::ParticipantCountMismatch));

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
