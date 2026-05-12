#include <crucible/observe/SdcDetect.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

namespace cog = ::crucible::cog;
namespace effects = ::crucible::effects;
namespace observe = ::crucible::observe;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                            \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n",        \
                         #cond, __FILE__, __LINE__);                      \
            ++total_failed;                                               \
            return;                                                       \
        }                                                                 \
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
    auto detector = observe::mint_sdc_detector<effects::ColdInitCtx, 4, 8>(
        effects::ColdInitCtx{});

    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(1)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(2)));
    CRUCIBLE_REQUIRE(!detector.register_cog(make_cog(2)));

    auto verified = detector.run_with_redundancy(
        effects::BgDrainCtx{}, [](cog::CogIdentity const&) noexcept {
            return std::uint64_t{0xfeed'cafe};
        });

    CRUCIBLE_REQUIRE(verified.has_value());
    CRUCIBLE_REQUIRE(verified->value() == 0xfeed'cafeull);
    static_assert(std::is_same_v<decltype(*verified),
                  observe::SdcVerified<std::uint64_t>&>);
    CRUCIBLE_REQUIRE(detector.events()[0].kind == observe::SdcEventKind::Verified);
    CRUCIBLE_REQUIRE(detector.events()[0].compared_replicas == 2);
}

void test_mismatch_records_implicated_cog_and_threshold() {
    observe::SdcConfig config{};
    config.suspect_after_mismatches = observe::PositiveSdcMismatchThreshold{1};
    auto detector = observe::mint_sdc_detector<effects::ColdInitCtx, 3, 4>(
        effects::ColdInitCtx{}, config);

    auto const primary = make_cog(11);
    auto const bad = make_cog(12);
    CRUCIBLE_REQUIRE(detector.register_cog(primary));
    CRUCIBLE_REQUIRE(detector.register_cog(bad));

    auto result = detector.run_with_redundancy(
        effects::BgDrainCtx{}, [](cog::CogIdentity const& id) noexcept {
            return id.uuid.lo == 12 ? std::uint32_t{8} : std::uint32_t{7};
        });

    CRUCIBLE_REQUIRE(!result.has_value());
    CRUCIBLE_REQUIRE(result.error().kind == observe::SdcEventKind::Mismatch);
    CRUCIBLE_REQUIRE(result.error().comparison_cog == bad.uuid);
    CRUCIBLE_REQUIRE(detector.should_quarantine(bad));
}

void test_arithmetic_tolerance_allows_small_delta() {
    observe::SdcConfig config{};
    config.strategy = observe::SdcComparisonStrategy::ArithmeticTolerance;
    config.tolerance_units = 2;
    config.redundancy_factor = observe::PositiveSdcReplicaCount{3};

    auto detector = observe::mint_sdc_detector<effects::ColdInitCtx, 3, 4>(
        effects::ColdInitCtx{}, config);
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(21)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(22)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(23)));

    auto result = detector.run_with_redundancy(
        effects::BgDrainCtx{}, [](cog::CogIdentity const& id) noexcept {
            return id.uuid.lo == 21 ? std::int32_t{100} : std::int32_t{101};
        });

    CRUCIBLE_REQUIRE(result.has_value());
    CRUCIBLE_REQUIRE(result->value() == 100);
    CRUCIBLE_REQUIRE(detector.events()[0].kind == observe::SdcEventKind::Verified);
}

void test_signed_tolerance_does_not_use_modular_distance() {
    observe::SdcConfig config{};
    config.strategy = observe::SdcComparisonStrategy::ArithmeticTolerance;
    config.tolerance_units = 1;

    auto detector = observe::mint_sdc_detector<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{}, config);
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(24)));
    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(25)));

    auto result = detector.run_with_redundancy(
        effects::BgDrainCtx{}, [](cog::CogIdentity const& id) noexcept {
            return id.uuid.lo == 24 ? std::int32_t{0} : std::int32_t{-1};
        });

    CRUCIBLE_REQUIRE(result.has_value());
    CRUCIBLE_REQUIRE(result->value() == 0);
}

void test_insufficient_replicas_and_observation_publication() {
    auto detector = observe::mint_sdc_detector<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{});
    observe::ObservationSnapshot empty_sink{};
    CRUCIBLE_REQUIRE(!detector.publish_latest(empty_sink));

    CRUCIBLE_REQUIRE(detector.register_cog(make_cog(31)));

    auto result = detector.run_with_redundancy(
        effects::BgDrainCtx{}, [](cog::CogIdentity const&) noexcept {
            return std::uint64_t{1};
        });
    CRUCIBLE_REQUIRE(!result.has_value());
    CRUCIBLE_REQUIRE(result.error().kind == observe::SdcEventKind::InsufficientReplicas);

    observe::ObservationSnapshot sink{};
    CRUCIBLE_REQUIRE(detector.publish_latest(sink));
    auto const observation = observe::latest_observation(sink);
    CRUCIBLE_REQUIRE(observation.metric_id == detector.config().metric_id_base);
    CRUCIBLE_REQUIRE(observation.value
        == static_cast<std::uint64_t>(observe::SdcEventKind::InsufficientReplicas));
}

void test_sampling_decision_is_deterministic() {
    observe::SdcConfig config{};
    config.sampling_rate_ppm = observe::SdcSamplingRatePpm{1'000'000};
    auto all = observe::mint_sdc_detector<effects::ColdInitCtx, 1, 1>(
        effects::ColdInitCtx{}, config);
    CRUCIBLE_REQUIRE(all.should_sample(42));

    config.sampling_rate_ppm = observe::SdcSamplingRatePpm{1};
    auto sparse = observe::mint_sdc_detector<effects::ColdInitCtx, 1, 1>(
        effects::ColdInitCtx{}, config);
    CRUCIBLE_REQUIRE(sparse.should_sample(77) == sparse.should_sample(77));
}

}  // namespace

int main() {
    static_assert(observe::CtxFitsSdcMint<effects::ColdInitCtx>);
    static_assert(!observe::CtxFitsSdcMint<effects::BgDrainCtx>);
    static_assert(observe::CtxFitsSdcRun<effects::BgDrainCtx>);
    static_assert(!observe::CtxFitsSdcRun<effects::HotFgCtx>);
    static_assert(std::is_trivially_copyable_v<observe::SdcEvent>);

    std::fprintf(stderr, "[test_observe_sdc_detect]\n");
    run_test("redundant_equal_results_mint_verified_tag",
             test_redundant_equal_results_mint_verified_tag);
    run_test("mismatch_records_implicated_cog_and_threshold",
             test_mismatch_records_implicated_cog_and_threshold);
    run_test("arithmetic_tolerance_allows_small_delta",
             test_arithmetic_tolerance_allows_small_delta);
    run_test("signed_tolerance_does_not_use_modular_distance",
             test_signed_tolerance_does_not_use_modular_distance);
    run_test("insufficient_replicas_and_observation_publication",
             test_insufficient_replicas_and_observation_publication);
    run_test("sampling_decision_is_deterministic",
             test_sampling_decision_is_deterministic);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
