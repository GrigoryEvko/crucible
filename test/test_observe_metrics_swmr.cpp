#include <crucible/observe/Metrics.h>
#include <crucible/observe/Observation.h>
#include <fixy/Ctx.h>
#include <fixy/Stale.h>
#include <foundation/Brand.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The tag of Nondet, which a read with no wait takes.
constexpr ::foundation::effects::cap::Nondet kNondet = ::foundation::effects::testing::test().nondet;

}  // namespace

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

#define CRUCIBLE_REQUIRE(cond)                                                                           \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            ::fixy::report(::fixy::Sink::Err, "  REQUIRE FAILED: {} @ {}:{}\n", ::fixy::TextView{#cond}, \
                           ::fixy::TextView{__FILE__}, __LINE__);                                        \
            ++total_failed;                                                                              \
            return;                                                                                      \
        }                                                                                                \
    } while (0)

template <typename Body>
void run_test(::fixy::TextView name, Body body) {
    ::fixy::report(::fixy::Sink::Err, "  {} ... ", name);
    int const before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        ::fixy::report(::fixy::Sink::Err, "OK\n");
    } else {
        ::fixy::report(::fixy::Sink::Err, "FAILED\n");
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
    m.delta_g.with_element(::fixy::index<0>(), [value = base + 8.0](double& element) noexcept { element = value; });
    m.delta_g.with_element(::fixy::index<1>(), [value = base + 9.0](double& element) noexcept { element = value; });
    m.delta_g.with_element(::fixy::index<2>(), [value = base + 10.0](double& element) noexcept { element = value; });
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

    // The sample of step zero, graded seven steps later.
    ::fixy::SequenceStamp const sampled = steps.stamp();
    for (int step = 0; step < 7; ++step)
        static_cast<void>(steps.advance());
    auto sample = observe::metrics_sample_at(make_metrics(100.0), steps, sampled);
    CRUCIBLE_REQUIRE(sample.staleness().value == 7);

    // The two reader frames open inside the frame of the writer, so the
    // writer and the two readers live at the same time.  The writer body ends
    // its handle on each path, so the checks of the readers stand in the
    // innermost body, which holds no linear value.
    bool const is_each_lent =
        observe::with_metrics_writer(channel, metrics_writer_root(), [&channel, &sample](auto writer) noexcept {
            writer.publish(sample);
            auto const lent =
                observe::with_keeper_metrics_reader(kTest, channel, [&channel, &sample](auto keeper) noexcept {
                    return observe::with_canopy_metrics_reader(
                               kTest, channel,
                               [&channel, &sample, &keeper](auto canopy) noexcept {
                                   auto keeper_sample = keeper.load(kTest);
                                   auto canopy_sample = canopy.load(kTest);
                                   CRUCIBLE_REQUIRE(keeper.try_load(kNondet).is_some());

                                   CRUCIBLE_REQUIRE(near(keeper_sample.peek().meb_lambda_max, 101.0));
                                   CRUCIBLE_REQUIRE(near(canopy_sample.peek().ntk_alpha_drift, 107.0));
                                   CRUCIBLE_REQUIRE(keeper_sample.peek().delta_g_count == 3);
                                   CRUCIBLE_REQUIRE(near(canopy_sample.peek().delta_g.at(::fixy::index<2>()), 110.0));
                                   CRUCIBLE_REQUIRE(keeper_sample.staleness() == sample.staleness());
                                   CRUCIBLE_REQUIRE(channel.outstanding_readers() == 2);
                               })
                        .is_some();
                });
            ::foundation::linear::drop(std::move(writer).release());
            return lent == ::fixy::some(true);
        });
    CRUCIBLE_REQUIRE(is_each_lent);
}

void test_exclusive_drain_waits_for_readers() {
    ::fixy::SequenceSource const steps =
        ::fixy::mint_sequence_source(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    Channel channel{metrics_reader_root(), observe::fresh_metrics_sample(make_metrics(0.0), steps)};

    // The reader root stays in the pool while a reader lives.
    auto const lent = observe::with_keeper_metrics_reader(kTest, channel, [&channel](auto /*reader*/) noexcept {
        bool const is_refused = channel.try_withdraw_reader_root(kTest).match(
            [&channel](auto root) noexcept {
                channel.deposit_reader_root(std::move(root));
                return false;
            },
            []() noexcept { return true; });
        CRUCIBLE_REQUIRE(is_refused);
    });
    CRUCIBLE_REQUIRE(lent.is_some());

    // The end of the frame gives the share of the reader back, so the root
    // comes out.
    auto root = channel.try_withdraw_reader_root(kTest).expect("the root comes out with no reader out");
    bool const is_root_out = channel.is_exclusive_active();
    channel.deposit_reader_root(std::move(root));
    CRUCIBLE_REQUIRE(is_root_out);
}

void test_runtime_observation_snapshot() {
    observe::ObservationSnapshot sink{observe::latency_ns(11, 250, 1, observe::ObservationSource::Bpf)};

    observe::record_observation(sink, observe::bits_transferred(12, 4096, 2, observe::ObservationSource::Runtime));

    const observe::Observation latest = observe::latest_observation(kTest, sink);
    CRUCIBLE_REQUIRE(latest.kind == observe::ObservationKind::BitsTransferred);
    CRUCIBLE_REQUIRE(latest.source == observe::ObservationSource::Runtime);
    CRUCIBLE_REQUIRE(latest.metric_id == 12);
    CRUCIBLE_REQUIRE(latest.value == 4096);
    CRUCIBLE_REQUIRE(latest.sequence == 2);
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Err, "[test_metrics_swmr]\n");
    run_test("metric_payload_is_snapshot_safe", test_metric_payload_is_snapshot_safe);
    run_test("writer_publish_keeper_and_canopy_readers", test_writer_publish_keeper_and_canopy_readers);
    run_test("exclusive_drain_waits_for_readers", test_exclusive_drain_waits_for_readers);
    run_test("runtime_observation_snapshot", test_runtime_observation_snapshot);

    ::fixy::check::pass(kTest, "\n{} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
