#include <crucible/observe/Metrics.h>
#include <crucible/observe/Observation.h>
#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/Brand.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include "test_assert.h"

namespace {

namespace observe = ::crucible::observe;
namespace perm = ::foundation::permissions;

// One call site mints every reader root of the channel type, so every
// channel of the type carries the brand of that one site.
[[nodiscard]] auto metrics_reader_root() noexcept {
    return perm::mint_permission_root<observe::RuntimeMetricsReaderTag>();
}

// The writer root has one call site too, and the channel type names its
// brand, so the channel takes a writer of that site and no other.
[[nodiscard]] auto metrics_writer_root() noexcept {
    return perm::mint_permission_root<observe::RuntimeMetricsWriterTag>();
}

using Channel = observe::RuntimeMetricsChannel<::foundation::brand::brand_of_t<decltype(metrics_reader_root())>,
                                               ::foundation::brand::brand_of_t<decltype(metrics_writer_root())>>;

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

observe::RuntimeMetrics make_metrics(double base) noexcept {
    observe::RuntimeMetrics m{};
    m.meb_lambda_max = base + 1.0;
    m.meb_threshold = base + 2.0;
    m.wasserstein_ratio = base + 3.0;
    m.bits_per_step_ratio = base + 4.0;
    m.dmft_tail_fraction = base + 5.0;
    m.ntk_alpha = base + 6.0;
    m.ntk_alpha_drift = base + 7.0;
    m.delta_g_count = 3;
    m.delta_g.at(::fixy::index<0>()) = base + 8.0;
    m.delta_g.at(::fixy::index<1>()) = base + 9.0;
    m.delta_g.at(::fixy::index<2>()) = base + 10.0;
    return m;
}

bool near(double a, double b) noexcept {
    double const diff = a > b ? a - b : b - a;
    return diff < 1e-12;
}

void test_metric_payload_is_snapshot_safe() {
    static_assert(std::is_trivially_copyable_v<observe::RuntimeMetrics>);
    static_assert(std::is_trivially_destructible_v<observe::RuntimeMetrics>);
    static_assert(::fixy::concurrent::SnapshotValue<observe::RuntimeMetricsSample>);
    static_assert(sizeof(observe::RuntimeMetricsSample) <= 256);
    CRUCIBLE_REQUIRE(true);
}

void test_writer_publish_keeper_and_canopy_readers() {
    ::fixy::SequenceSource steps =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    auto initial = observe::fresh_metrics_sample(make_metrics(0.0), steps);
    Channel channel{metrics_reader_root(), initial};

    auto writer = observe::mint_metrics_writer(channel, metrics_writer_root());

    auto keeper = observe::mint_keeper_metrics_reader(channel).expect("a fresh channel has a free reader share");
    auto canopy = observe::mint_canopy_metrics_reader(channel).expect("a fresh channel has a free reader share");

    // The sample of step zero, graded seven steps later.
    ::fixy::SequenceStamp const sampled = steps.stamp();
    for (int step = 0; step < 7; ++step)
        static_cast<void>(steps.advance());
    auto sample = observe::metrics_sample_at(make_metrics(100.0), steps, sampled);
    CRUCIBLE_REQUIRE(sample.staleness().value == 7);
    writer.publish(sample);

    auto keeper_sample = keeper.load();
    auto canopy_sample = canopy.load();
    CRUCIBLE_REQUIRE(keeper.try_load().is_some());

    CRUCIBLE_REQUIRE(near(keeper_sample.peek().meb_lambda_max, 101.0));
    CRUCIBLE_REQUIRE(near(canopy_sample.peek().ntk_alpha_drift, 107.0));
    CRUCIBLE_REQUIRE(keeper_sample.peek().delta_g_count == 3);
    CRUCIBLE_REQUIRE(near(canopy_sample.peek().delta_g.at(::fixy::index<2>()), 110.0));
    CRUCIBLE_REQUIRE(keeper_sample.staleness() == sample.staleness());
    CRUCIBLE_REQUIRE(channel.outstanding_readers() == 2);
}

void test_exclusive_drain_waits_for_readers() {
    ::fixy::SequenceSource const steps =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    Channel channel{metrics_reader_root(), observe::fresh_metrics_sample(make_metrics(0.0), steps)};

    {
        auto reader = observe::mint_keeper_metrics_reader(channel);
        CRUCIBLE_REQUIRE(reader.is_some());
        // The reader root stays in the pool while a reader lives.
        bool const is_refused = channel.try_withdraw_reader_root().match(
            [&channel](auto root) noexcept {
                channel.deposit_reader_root(std::move(root));
                return false;
            },
            []() noexcept { return true; });
        CRUCIBLE_REQUIRE(is_refused);
    }

    // The end of the scope gives the share of the reader back, so the root
    // comes out.
    auto root = channel.try_withdraw_reader_root().expect("the root comes out with no reader out");
    bool const is_root_out = channel.is_exclusive_active();
    channel.deposit_reader_root(std::move(root));
    CRUCIBLE_REQUIRE(is_root_out);
}

void test_runtime_observation_snapshot() {
    observe::ObservationSnapshot sink{observe::latency_ns(11, 250, 1, observe::ObservationSource::Bpf)};

    observe::record_observation(sink, observe::bits_transferred(12, 4096, 2, observe::ObservationSource::Runtime));

    const observe::Observation latest = observe::latest_observation(sink);
    CRUCIBLE_REQUIRE(latest.kind == observe::ObservationKind::BitsTransferred);
    CRUCIBLE_REQUIRE(latest.source == observe::ObservationSource::Runtime);
    CRUCIBLE_REQUIRE(latest.metric_id == 12);
    CRUCIBLE_REQUIRE(latest.value == 4096);
    CRUCIBLE_REQUIRE(latest.sequence == 2);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_metrics_swmr]\n");
    run_test("metric_payload_is_snapshot_safe", test_metric_payload_is_snapshot_safe);
    run_test("writer_publish_keeper_and_canopy_readers", test_writer_publish_keeper_and_canopy_readers);
    run_test("exclusive_drain_waits_for_readers", test_exclusive_drain_waits_for_readers);
    run_test("runtime_observation_snapshot", test_runtime_observation_snapshot);

    crucible::test::pass("\n{} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
