#include <crucible/cntp/_wip/Sharp.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
    CRUCIBLE_TEST_ASSERT(kTest, shp::sharp_error_name(shp::SharpError::RuntimeUnavailable) == "RuntimeUnavailable");
    CRUCIBLE_TEST_ASSERT(kTest, shp::sharp_fallback_name(shp::SharpFallback::BitexactTree) == "BitexactTree");

    auto count = shp::admit_sharp_participant_count(8);
    CRUCIBLE_TEST_ASSERT(kTest, count.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(count).expect("8 participants is positive").value() == 8);

    auto empty = shp::admit_sharp_participant_count(0);
    CRUCIBLE_TEST_ASSERT(kTest, empty.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, empty.err() == ::fixy::some(shp::SharpError::EmptyParticipantSet));

    static_assert(shp::SharpEligibleRecipe<GoodRecipe>);
    static_assert(!shp::SharpEligibleRecipe<StrictRecipe>);
    static_assert(!shp::SharpEligibleRecipe<NonCommutativeRecipe>);

    ::fixy::check::pass(kTest, "  test_admission_and_names: PASSED\n");
}

void test_fabric_plan_minting() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");
    static_assert(std::same_as<decltype(plan), shp::DeclaredSharpFabricPlan>);
    CRUCIBLE_TEST_ASSERT(kTest, plan.value().participant_count.value() == 8);

    auto no_cap = switch_caps();
    no_cap.features.unset(cog::SwitchFeature::Sharp);
    auto missing_cap = shp::mint_sharp_fabric_plan(
        init, switch_identity(), no_cap, shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, missing_cap.err() == ::fixy::some(shp::SharpError::MissingSwitchSharpCapability));

    auto wrong_kind = switch_identity();
    wrong_kind.kind = cog::CogKind::NicPort;
    auto non_switch = shp::mint_sharp_fabric_plan(
        init, wrong_kind, switch_caps(), shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, non_switch.err() == ::fixy::some(shp::SharpError::NonSwitchCog));

    auto zero = switch_identity();
    zero.uuid = cog::Uuid{};
    auto zero_switch = shp::mint_sharp_fabric_plan(
        init, zero, switch_caps(), shp::admit_sharp_participant_count(8).expect("8 participants is positive"));
    CRUCIBLE_TEST_ASSERT(kTest, zero_switch.err() == ::fixy::some(shp::SharpError::ZeroSwitchCog));

    ::fixy::check::pass(kTest, "  test_fabric_plan_minting: PASSED\n");
}

void test_recipe_eligibility() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");

    shp::DeclaredSharpDispatch const eligible = take(shp::eligibility_check(
        recipe(crucible::ReductionDeterminism::BITEXACT_TC), shp::sharp_recipe_laws<GoodRecipe>(), plan));
    CRUCIBLE_TEST_ASSERT(kTest, eligible.value().fallback == shp::SharpFallback::None);
    CRUCIBLE_TEST_ASSERT(kTest, eligible.value().participant_count == 8);

    auto strict = shp::eligibility_check(recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                         shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
    CRUCIBLE_TEST_ASSERT(kTest, strict.err() == ::fixy::some(shp::SharpError::BitexactStrictForbidden));

    auto non_associative =
        shp::eligibility_check(recipe(), shp::SharpRecipeLaws{.associative = false, .commutative = true}, plan);
    CRUCIBLE_TEST_ASSERT(kTest, non_associative.err() == ::fixy::some(shp::SharpError::RecipeNotAssociative));

    auto non_commutative =
        shp::eligibility_check(recipe(), shp::SharpRecipeLaws{.associative = true, .commutative = false}, plan);
    CRUCIBLE_TEST_ASSERT(kTest, non_commutative.err() == ::fixy::some(shp::SharpError::RecipeNotCommutative));

    auto unsupported_dtype =
        shp::eligibility_check(recipe(crucible::ReductionDeterminism::ORDERED, crucible::ScalarType::Bool),
                               shp::SharpRecipeLaws{.associative = true, .commutative = true}, plan);
    CRUCIBLE_TEST_ASSERT(kTest, unsupported_dtype.err() == ::fixy::some(shp::SharpError::UnsupportedScalarType));

    ::fixy::check::pass(kTest, "  test_recipe_eligibility: PASSED\n");
}

void test_dispatch_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto plan = shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"))
                    .expect("a SHARP switch makes a plan");

    ::foundation::core::FixedArray<float, 4> const input_values{1.0f, 2.0f, 3.0f, 4.0f};
    auto [output_values, output_values_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<float, 4>);
    ::fixy::View<float const> const input = input_values.view();
    shp::DeclaredSharpDispatch const unavailable =
        take(output_values.with_mut(output_values_key, [input, plan](auto cells) noexcept {
            return shp::dispatch_sharp_allreduce(input, std::move(cells), recipe(),
                                                 shp::sharp_recipe_laws<GoodRecipe>(), plan);
        }));
    CRUCIBLE_TEST_ASSERT(kTest, unavailable.value().fallback == shp::SharpFallback::RingOrTree);
    CRUCIBLE_TEST_ASSERT(kTest, unavailable.value().element_count == input.size());

    auto deferred_plan =
        shp::mint_sharp_fabric_plan(init, switch_identity(), switch_caps(),
                                    shp::admit_sharp_participant_count(8).expect("8 participants is positive"), true)
            .expect("a loaded runtime makes a plan");
    shp::DeclaredSharpDispatch const deferred =
        take(output_values.with_mut(output_values_key, [input, deferred_plan](auto cells) noexcept {
            return shp::dispatch_sharp_allreduce(input, std::move(cells), recipe(),
                                                 shp::sharp_recipe_laws<GoodRecipe>(), deferred_plan);
        }));
    CRUCIBLE_TEST_ASSERT(kTest, deferred.value().fallback == shp::SharpFallback::RingOrTree);

    CRUCIBLE_TEST_ASSERT(kTest, context_error(shp::mint_sharp_context(init, deferred_plan))
                                    == ::fixy::some(shp::SharpError::DispatchDeferred));
    CRUCIBLE_TEST_ASSERT(kTest, context_error(shp::mint_sharp_context(init, plan))
                                    == ::fixy::some(shp::SharpError::RuntimeUnavailable));

    auto backend_plan = shp::mint_sharp_fabric_plan(
                            init, switch_identity(), switch_caps(),
                            shp::admit_sharp_participant_count(8).expect("8 participants is positive"), true, true)
                            .expect("a plan that asks for the backend makes a plan");
    shp::DeclaredSharpDispatch const backend =
        take(output_values.with_mut(output_values_key, [input, backend_plan](auto cells) noexcept {
            return shp::dispatch_sharp_allreduce(input, std::move(cells), recipe(),
                                                 shp::sharp_recipe_laws<GoodRecipe>(), backend_plan);
        }));
    CRUCIBLE_TEST_ASSERT(kTest, backend.value().fallback == shp::SharpFallback::RingOrTree);

    shp::DeclaredSharpDispatch const strict_fallback =
        take(output_values.with_mut(output_values_key, [input, backend_plan](auto cells) noexcept {
            return shp::dispatch_sharp_allreduce(
                input, std::move(cells), recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                shp::SharpRecipeLaws{.associative = true, .commutative = true}, backend_plan);
        }));
    CRUCIBLE_TEST_ASSERT(kTest, strict_fallback.value().fallback == shp::SharpFallback::BitexactTree);

    auto [short_values, short_values_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<float, 2>);
    auto shape = short_values.with_mut(short_values_key, [input, backend_plan](auto cells) noexcept {
        return shp::dispatch_sharp_allreduce(input, std::move(cells), recipe(), shp::sharp_recipe_laws<GoodRecipe>(),
                                             backend_plan);
    });
    CRUCIBLE_TEST_ASSERT(kTest, shape.err() == ::fixy::some(shp::SharpError::OutputShapeMismatch));

    shp::SharpContext context =
        shp::mint_sharp_context(init, backend_plan).expect("a plan that asks for the backend makes a context");
    CRUCIBLE_TEST_ASSERT(kTest, context.peek().switch_uuid() == switch_identity().uuid);
    CRUCIBLE_TEST_ASSERT(kTest, context.peek().participant_count().value() == 8);
    shp::SharpReducer reducer{std::move(context)};
    auto reduced = output_values.with_mut(output_values_key, [&reducer, &bg, input, backend_plan](auto cells) noexcept {
        return reducer.allreduce_via_sharp(bg, input, std::move(cells), recipe(), shp::sharp_recipe_laws<GoodRecipe>(),
                                           backend_plan);
    });
    CRUCIBLE_TEST_ASSERT(kTest, reduced.err() == ::fixy::some(shp::SharpError::VendorBackendUnavailable));

    auto short_reduced =
        short_values.with_mut(short_values_key, [&reducer, &bg, input, backend_plan](auto cells) noexcept {
            return reducer.allreduce_via_sharp(bg, input, std::move(cells), recipe(),
                                               shp::sharp_recipe_laws<GoodRecipe>(), backend_plan);
        });
    CRUCIBLE_TEST_ASSERT(kTest, short_reduced.err() == ::fixy::some(shp::SharpError::OutputShapeMismatch));

    auto other_plan = shp::mint_sharp_fabric_plan(
                          init, switch_identity(), switch_caps(),
                          shp::admit_sharp_participant_count(4).expect("4 participants is positive"), true, true)
                          .expect("a plan of four participants makes a plan");
    auto mismatch = output_values.with_mut(output_values_key, [&reducer, &bg, input, other_plan](auto cells) noexcept {
        return reducer.allreduce_via_sharp(bg, input, std::move(cells), recipe(), shp::sharp_recipe_laws<GoodRecipe>(),
                                           other_plan);
    });
    CRUCIBLE_TEST_ASSERT(kTest, mismatch.err() == ::fixy::some(shp::SharpError::ParticipantCountMismatch));

    ::fixy::check::pass(kTest, "  test_dispatch_boundary: PASSED\n");
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
    ::fixy::check::pass(kTest, "test_cntp_sharp: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
