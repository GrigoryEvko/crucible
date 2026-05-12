#include <crucible/rt/Metrics.h>
#include <crucible/rt/Observation.h>
#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

namespace rt = ::crucible::rt;
namespace safety = ::crucible::safety;

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

rt::RuntimeMetrics make_metrics(double base) noexcept {
    rt::RuntimeMetrics m{};
    m.meb_lambda_max = base + 1.0;
    m.meb_threshold = base + 2.0;
    m.wasserstein_ratio = base + 3.0;
    m.bits_per_step_ratio = base + 4.0;
    m.dmft_tail_fraction = base + 5.0;
    m.ntk_alpha = base + 6.0;
    m.ntk_alpha_drift = base + 7.0;
    m.delta_g_count = 3;
    m.delta_g[0] = base + 8.0;
    m.delta_g[1] = base + 9.0;
    m.delta_g[2] = base + 10.0;
    return m;
}

bool near(double a, double b) noexcept {
    double const diff = a > b ? a - b : b - a;
    return diff < 1e-12;
}

void test_metric_payload_is_snapshot_safe() {
    static_assert(std::is_trivially_copyable_v<rt::RuntimeMetrics>);
    static_assert(std::is_trivially_destructible_v<rt::RuntimeMetrics>);
    static_assert(::crucible::concurrent::SnapshotValue<
                  rt::RuntimeMetricsSample>);
    static_assert(sizeof(rt::RuntimeMetricsSample) <= 256);
    CRUCIBLE_REQUIRE(true);
}

void test_writer_publish_keeper_and_canopy_readers() {
    auto initial = rt::fresh_metrics_sample(make_metrics(0.0));
    rt::RuntimeMetricsChannel channel{initial};

    auto writer_perm = safety::mint_permission_root<rt::RuntimeMetricsWriterTag>();
    auto writer = rt::mint_rt_metrics_writer(
        channel, std::move(writer_perm));

    auto keeper = rt::mint_keeper_metrics_reader(channel);
    auto canopy = rt::mint_canopy_metrics_reader(channel);
    CRUCIBLE_REQUIRE(keeper.has_value());
    CRUCIBLE_REQUIRE(canopy.has_value());

    auto sample = rt::metrics_sample_at(make_metrics(100.0), 7);
    writer.publish(sample);

    auto keeper_sample = keeper->load();
    auto canopy_sample = canopy->load();

    CRUCIBLE_REQUIRE(near(keeper_sample.peek().meb_lambda_max, 101.0));
    CRUCIBLE_REQUIRE(near(canopy_sample.peek().ntk_alpha_drift, 107.0));
    CRUCIBLE_REQUIRE(keeper_sample.peek().delta_g_count == 3);
    CRUCIBLE_REQUIRE(near(canopy_sample.peek().delta_g[2], 110.0));
    CRUCIBLE_REQUIRE(keeper_sample.staleness() == sample.staleness());
    CRUCIBLE_REQUIRE(channel.outstanding_readers() == 2);
}

void test_exclusive_drain_waits_for_readers() {
    rt::RuntimeMetricsChannel channel{
        rt::fresh_metrics_sample(make_metrics(0.0))};

    auto reader = rt::mint_keeper_metrics_reader(channel);
    CRUCIBLE_REQUIRE(reader.has_value());

    bool drained = false;
    CRUCIBLE_REQUIRE(!channel.with_drained_access([&] { drained = true; }));
    CRUCIBLE_REQUIRE(!drained);

    reader.reset();
    CRUCIBLE_REQUIRE(channel.with_drained_access([&] { drained = true; }));
    CRUCIBLE_REQUIRE(drained);
}

void test_runtime_observation_snapshot() {
    rt::ObservationSnapshot sink{
        rt::latency_ns(11, 250, 1, rt::ObservationSource::Bpf)};

    rt::record_observation(
        sink,
        rt::bits_transferred(12, 4096, 2, rt::ObservationSource::Runtime));

    const rt::Observation latest = rt::latest_observation(sink);
    CRUCIBLE_REQUIRE(latest.kind == rt::ObservationKind::BitsTransferred);
    CRUCIBLE_REQUIRE(latest.source == rt::ObservationSource::Runtime);
    CRUCIBLE_REQUIRE(latest.metric_id == 12);
    CRUCIBLE_REQUIRE(latest.value == 4096);
    CRUCIBLE_REQUIRE(latest.sequence == 2);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_rt_metrics_swmr]\n");
    run_test("metric_payload_is_snapshot_safe",
             test_metric_payload_is_snapshot_safe);
    run_test("writer_publish_keeper_and_canopy_readers",
             test_writer_publish_keeper_and_canopy_readers);
    run_test("exclusive_drain_waits_for_readers",
             test_exclusive_drain_waits_for_readers);
    run_test("runtime_observation_snapshot",
             test_runtime_observation_snapshot);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
