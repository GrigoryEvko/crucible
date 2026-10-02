#include <crucible/observe/SdcDetect.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include "test_assert.h"

namespace {

namespace cog = ::crucible::cog;
namespace eff = ::fixy;
namespace observe = ::crucible::observe;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                                                 \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            ++total_failed;                                                                    \
            return;                                                                            \
        }                                                                                      \
    } while (0)

template <typename Body>
void run_test(char const* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int const before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

cog::CogIdentity make_cog(std::uint64_t lo) noexcept {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x180, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::Gpu;
    return id;
}

void test_redundant_equal_results_mint_verified_tag() {
    auto detector =
        observe::mint_sdc_detector<eff::ColdInitCtx, 4, 8>(eff::ColdInitCtx{::foundation::effects::testing::init()});

    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(1)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(2)));
    CRUCIBLE_REQUIRE(!detector.register_cog(make_cog(2)));

    auto verified =
        detector.run_with_redundancy(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                                     [](cog::CogIdentity const&) noexcept { return std::uint64_t{0xfeedcafe}; });

    CRUCIBLE_REQUIRE(verified.has_value());
    CRUCIBLE_REQUIRE(verified->value() == 0xfeedcafeull);
    static_assert(std::is_same_v<decltype(*verified), observe::SdcVerified<std::uint64_t>&>);
    CRUCIBLE_REQUIRE(detector.events()[0].kind == observe::SdcEventKind::Verified);
    CRUCIBLE_REQUIRE(detector.events()[0].compared_replicas == 2);
}

void test_mismatch_records_implicated_cog_and_threshold() {
    observe::SdcConfig config{};
    config.suspect_after_mismatches = ::fixy::mint_refined<observe::PositiveSdcMismatchThreshold::predicate_type{},
                                                           observe::PositiveSdcMismatchThreshold::value_type>(1);
    auto detector = observe::mint_sdc_detector<eff::ColdInitCtx, 3, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, config);

    auto const primary = make_cog(11);
    auto const bad = make_cog(12);
    CRUCIBLE_REQUIRE(detector.register_cog(primary));
    CRUCIBLE_REQUIRE(detector.register_cog(bad));

    auto result = detector.run_with_redundancy(
        eff::BgDrainCtx{::foundation::effects::testing::bg()},
        [](cog::CogIdentity const& id) noexcept { return id.uuid.lo == 12 ? std::uint32_t{8} : std::uint32_t{7}; });

    CRUCIBLE_REQUIRE(!result.has_value());
    CRUCIBLE_REQUIRE(result.error().kind == observe::SdcEventKind::Mismatch);
    CRUCIBLE_REQUIRE(result.error().strategy == observe::SdcComparisonStrategy::BitwiseEqual);
    CRUCIBLE_REQUIRE(result.error().primary_slot == 0);
    CRUCIBLE_REQUIRE(result.error().comparison_slot == 1);
    CRUCIBLE_REQUIRE(result.error().compared_replicas == 2);
    CRUCIBLE_REQUIRE(result.error().mismatch_count == 1);
    CRUCIBLE_REQUIRE(result.error().primary_cog == primary.uuid);
    CRUCIBLE_REQUIRE(result.error().comparison_cog == bad.uuid);
    CRUCIBLE_REQUIRE(detector.should_quarantine(bad));
    CRUCIBLE_REQUIRE(!detector.should_quarantine(make_cog(13)));
}

// A mismatch count that wrapped to zero would clear a suspect Cog, so the
// count saturates.
void test_mismatch_count_saturates() {
    auto detector =
        observe::mint_sdc_detector<eff::ColdInitCtx, 2, 4>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto const primary = make_cog(14);
    auto const bad = make_cog(15);
    CRUCIBLE_REQUIRE(detector.register_cog(primary));
    CRUCIBLE_REQUIRE(detector.register_cog(bad));

    constexpr std::uint32_t runs = std::uint32_t{std::numeric_limits<std::uint16_t>::max()} + 10u;
    for (std::uint32_t run = 0; run < runs; ++run) {
        auto result = detector.run_with_redundancy(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                                                   [](cog::CogIdentity const& id) noexcept { return id.uuid.lo; });
        CRUCIBLE_REQUIRE(!result.has_value());
    }
    CRUCIBLE_REQUIRE(detector.cogs()[1].mismatch_count == std::numeric_limits<std::uint16_t>::max());
    CRUCIBLE_REQUIRE(detector.should_quarantine(bad));
}

void test_arithmetic_tolerance_allows_small_delta() {
    observe::SdcConfig config{};
    config.strategy = observe::SdcComparisonStrategy::ArithmeticTolerance;
    config.tolerance_units = 2;
    config.redundancy_factor = ::fixy::mint_refined<observe::PositiveSdcReplicaCount::predicate_type{},
                                                    observe::PositiveSdcReplicaCount::value_type>(3);

    auto detector = observe::mint_sdc_detector<eff::ColdInitCtx, 3, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, config);
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(21)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(22)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(23)));

    auto result = detector.run_with_redundancy(
        eff::BgDrainCtx{::foundation::effects::testing::bg()},
        [](cog::CogIdentity const& id) noexcept { return id.uuid.lo == 21 ? std::int32_t{100} : std::int32_t{101}; });

    CRUCIBLE_REQUIRE(result.has_value());
    CRUCIBLE_REQUIRE(result->value() == 100);
    CRUCIBLE_REQUIRE(detector.events()[0].kind == observe::SdcEventKind::Verified);
}

void test_signed_tolerance_does_not_use_modular_distance() {
    observe::SdcConfig config{};
    config.strategy = observe::SdcComparisonStrategy::ArithmeticTolerance;
    config.tolerance_units = 1;

    auto detector = observe::mint_sdc_detector<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, config);
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(24)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(25)));

    auto result = detector.run_with_redundancy(
        eff::BgDrainCtx{::foundation::effects::testing::bg()},
        [](cog::CogIdentity const& id) noexcept { return id.uuid.lo == 24 ? std::int32_t{0} : std::int32_t{-1}; });

    CRUCIBLE_REQUIRE(result.has_value());
    CRUCIBLE_REQUIRE(result->value() == 0);
}

void test_insufficient_replicas_and_observation_publication() {
    auto detector =
        observe::mint_sdc_detector<eff::ColdInitCtx, 2, 4>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    observe::ObservationSnapshot empty_sink{};
    CRUCIBLE_REQUIRE(!detector.publish_latest(empty_sink));

    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(31)));

    auto result = detector.run_with_redundancy(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                                               [](cog::CogIdentity const&) noexcept { return std::uint64_t{1}; });
    CRUCIBLE_REQUIRE(!result.has_value());
    CRUCIBLE_REQUIRE(result.error().kind == observe::SdcEventKind::InsufficientReplicas);

    observe::ObservationSnapshot sink{};
    CRUCIBLE_REQUIRE(detector.publish_latest(sink));
    auto const observation = observe::latest_observation(sink);
    CRUCIBLE_REQUIRE(observation.metric_id == detector.config().metric_id_base);
    CRUCIBLE_REQUIRE(observation.value == static_cast<std::uint64_t>(observe::SdcEventKind::InsufficientReplicas));
}

void test_sampling_decision_is_deterministic() {
    observe::SdcConfig config{};
    config.sampling_rate_ppm =
        ::fixy::mint_refined<observe::SdcSamplingRatePpm::predicate_type{}, observe::SdcSamplingRatePpm::value_type>(
            1000000);
    auto all = observe::mint_sdc_detector<eff::ColdInitCtx, 1, 1>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, config);
    CRUCIBLE_REQUIRE(all.should_sample(42));

    config.sampling_rate_ppm =
        ::fixy::mint_refined<observe::SdcSamplingRatePpm::predicate_type{}, observe::SdcSamplingRatePpm::value_type>(1);
    auto sparse = observe::mint_sdc_detector<eff::ColdInitCtx, 1, 1>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, config);
    CRUCIBLE_REQUIRE(sparse.should_sample(77) == sparse.should_sample(77));
}

}  // namespace

int main() {
    static_assert(observe::CtxFitsSdcMint<eff::ColdInitCtx>);
    static_assert(!observe::CtxFitsSdcMint<eff::BgDrainCtx>);
    static_assert(observe::CtxFitsSdcRun<eff::BgDrainCtx>);
    static_assert(!observe::CtxFitsSdcRun<eff::HotFgCtx>);
    static_assert(std::is_trivially_copyable_v<observe::SdcEvent>);

    std::fprintf(stderr, "[test_observe_sdc_detect]\n");
    run_test("redundant_equal_results_mint_verified_tag", test_redundant_equal_results_mint_verified_tag);
    run_test("mismatch_records_implicated_cog_and_threshold", test_mismatch_records_implicated_cog_and_threshold);
    run_test("mismatch_count_saturates", test_mismatch_count_saturates);
    run_test("arithmetic_tolerance_allows_small_delta", test_arithmetic_tolerance_allows_small_delta);
    run_test("signed_tolerance_does_not_use_modular_distance", test_signed_tolerance_does_not_use_modular_distance);
    run_test("insufficient_replicas_and_observation_publication",
             test_insufficient_replicas_and_observation_publication);
    run_test("sampling_decision_is_deterministic", test_sampling_decision_is_deterministic);

    crucible::test::pass("\n{} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
