#include <crucible/topology/CongestionTelemetryWorker.h>
#include <crucible/topology/CongestionTelemetry.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <array>
#include <concepts>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace observe = crucible::observe;
namespace topology = crucible::topology;

namespace {

class TestSocket {
public:
    explicit TestSocket(int fd) noexcept : fd_{fd} {}
    TestSocket(TestSocket const&) = delete;
    TestSocket& operator=(TestSocket const&) = delete;
    TestSocket(TestSocket&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    TestSocket& operator=(TestSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~TestSocket() noexcept { close(); }

    [[nodiscard]] int raw() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
};

cog::CogIdentity nic(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x1234, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

cog::CogIdentity gpu(std::uint64_t lo) {
    cog::CogIdentity id = nic(lo);
    id.kind = cog::CogKind::Gpu;
    return id;
}

topology::CongestionSample state(std::uint64_t bw, std::uint64_t rtt, topology::CongestionMode mode) {
    topology::CongestionSample s{};
    s.algorithm = cntp::CcAlgorithm::Bbr3;
    s.btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(bw);
    s.rt_prop_us = ::fixy::mint_refined<::fixy::positive>(rtt);
    s.cwnd_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{65536});
    s.ssthresh_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{131072});
    s.in_flight_bytes = 32768;
    s.mode = mode;
    s.has_bbr = true;
    s.bbr.btl_bw_bps = s.btl_bw_bps;
    s.bbr.rt_prop_us = s.rt_prop_us;
    return s;
}

// The test stands in for the kernel, so it names the TCP_INFO source
// itself.  harvest_socket is the only producer in the library.
topology::TcpInfoSnapshot sample(std::uint64_t bw, std::uint64_t rtt, topology::CongestionMode mode) {
    return ::fixy::mint_tagged<::fixy::tags::source::TcpInfo>(state(bw, rtt, mode));
}

// The log spelling of both enums comes from reflection.
void test_names_and_admission() {
    static_assert(::foundation::reflect::enum_name(topology::CongestionMode::BbrDrain) == "BbrDrain");
    static_assert(::foundation::reflect::enum_name(topology::TelemetryError::InvalidNicCog) == "InvalidNicCog");
    volatile auto mode = topology::CongestionMode::BbrProbeRtt;
    assert(::foundation::reflect::enum_name(static_cast<topology::CongestionMode>(mode))
           == std::string_view{"BbrProbeRtt"});
    auto zero = topology::admit_sample_period_ns(0);
    assert(!zero.has_value());
    assert(zero.error() == topology::TelemetryError::DeadlineOverflow);
    auto period = topology::admit_sample_period_ns(1000);
    assert(period.has_value());
    assert(period->value() == 1000);
    crucible::test::pass("  test_names_and_admission: PASSED\n");
}

void test_aggregate_and_drift() {
    std::array samples{
        sample(1000000000, 100, topology::CongestionMode::Open),
        sample(800000000, 120, topology::CongestionMode::BbrProbeBw),
        sample(600000000, 150, topology::CongestionMode::Recovery),
        sample(500000000, 180, topology::CongestionMode::Loss),
    };

    auto aggregate = topology::aggregate_congestion(nic(1), std::span{samples});
    assert(aggregate.nic_uuid == nic(1).uuid);
    assert(aggregate.sample_count == samples.size());
    assert(aggregate.p95_rtt_us >= 150);
    assert(aggregate.p95_btl_bw_bps >= 800000000);
    assert(aggregate.mean_btl_bw_bps == 725000000);
    assert(aggregate.worst_mode == topology::CongestionMode::Loss);

    auto baseline = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1200000000});
    auto drift = topology::detect_congestion_drift(aggregate, baseline,
                                                   topology::CongestionDriftPolicy{
                                                       .bandwidth_drop_ppm = 100000,
                                                       .min_samples = 4,
                                                   });
    assert(drift.degraded);
    assert(drift.bandwidth_drop_ppm >= 100000);
    crucible::test::pass("  test_aggregate_and_drift: PASSED\n");
}

void test_worker_recording() {
    eff::ColdInitCtx init{::foundation::effects::testing::init()};
    eff::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto worker = topology::mint_congestion_telemetry_worker<2, 8>(init);

    std::array nics{nic(10), nic(11)};
    auto start = worker.start(init, std::span{nics});
    assert(start.has_value());

    std::array samples{
        sample(900000000, 90, topology::CongestionMode::Open),
        sample(700000000, 110, topology::CongestionMode::Cwr),
    };
    auto recorded = worker.record_link(bg, nics[0], std::span{samples}, 1);
    assert(recorded.has_value());
    assert(recorded->sample_count == samples.size());
    auto last = worker.last(nics[0]);
    assert(last.has_value());
    assert(last->sample_count == samples.size());

    auto unstarted = worker.last(nic(99));
    assert(!unstarted.has_value());
    assert(unstarted.error() == topology::TelemetryError::LinkNotStarted);

    std::array bad_nics{gpu(1)};
    auto bad_start = worker.start(init, std::span{bad_nics});
    assert(!bad_start.has_value());
    assert(bad_start.error() == topology::TelemetryError::InvalidNicCog);

    topology::CongestionObservationSet sinks{};
    topology::publish_congestion(sinks, 0, *last, 7);
    auto p95 =
        observe::latest_observation(sinks[static_cast<std::size_t>(topology::CongestionMetricSlot::P95BandwidthBps)]);
    assert(p95.metric_id == topology::congestion_metric_id(0, topology::CongestionMetricSlot::P95BandwidthBps));
    assert(p95.sequence == 7);
    crucible::test::pass("  test_worker_recording: PASSED\n");
}

void test_live_tcp_info_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());
    auto fd = cntp::admit_socket_fd(socket.raw());
    assert(fd.has_value());

    // A harvest reads socket options, and the test runner context carries
    // the IO and Block that the socket option gate asks for.
    eff::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto harvested = topology::harvest_socket(test_ctx, *fd);
    if (!harvested.has_value()) {
        ::fixy::report(::fixy::Sink::Out, "  test_live_tcp_info_if_available: SKIPPED\n");
        return;
    }
    assert(harvested->value().rt_prop_us.value() > 0);
    assert(harvested->value().cwnd_bytes.value() > 0);

    std::array fds{*fd};
    auto aggregate = topology::harvest_per_link(test_ctx, nic(77), std::span{fds});
    assert(aggregate.has_value());
    assert(aggregate->sample_count == 1);

    auto wrong_cog = topology::harvest_per_link(test_ctx, gpu(88), std::span{fds});
    assert(!wrong_cog.has_value());
    assert(wrong_cog.error() == topology::TelemetryError::InvalidNicCog);
    crucible::test::pass("  test_live_tcp_info_if_available: PASSED\n");
}

void test_aggregate_finalize_guard() {
    // Both aggregation entry points share one finalize helper, so an
    // empty span must leave every mean at zero rather than divide by a
    // sample count of zero.  One helper for both call sites is what
    // makes the two paths impossible to drift apart.
    auto empty = topology::aggregate_congestion(nic(101), {});
    assert(empty.sample_count == 0);
    assert(empty.mean_btl_bw_bps == 0);
    assert(empty.p50_rtt_us == 0);
    assert(empty.p95_rtt_us == 0);
    assert(empty.p95_btl_bw_bps == 0);
    assert(empty.nic_uuid == nic(101).uuid);

    std::array samples{
        sample(100000000ull, 1500ull, topology::CongestionMode::BbrProbeBw),
        sample(200000000ull, 2500ull, topology::CongestionMode::BbrProbeBw),
        sample(300000000ull, 3500ull, topology::CongestionMode::BbrProbeBw),
    };
    auto agg = topology::aggregate_congestion(nic(102), std::span<const topology::TcpInfoSnapshot>{samples});
    assert(agg.sample_count == 3);
    assert(agg.mean_btl_bw_bps == 200000000ull);
    assert(agg.nic_uuid == nic(102).uuid);

    crucible::test::pass("  test_aggregate_finalize_guard: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(topology::TcpInfoSnapshot) == sizeof(topology::CongestionSample));
    static_assert(topology::CtxFitsCongestionTelemetryStart<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsCongestionTelemetryStart<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsCongestionTelemetryHarvest<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsCongestionTelemetryHarvest<eff::HotFgCtx>);
    static_assert(topology::CtxFitsCongestionTelemetryPoll<eff::BgLoadCtx>);
    static_assert(!topology::CtxFitsCongestionTelemetryPoll<eff::BgDrainCtx>,
                  "a poll reads sockets through getsockopt, and a drain context carries no IO");
    static_assert(std::same_as<topology::TcpInfoSnapshot::tag_type, ::fixy::tags::source::TcpInfo>);
    static_assert(std::is_trivially_copy_constructible_v<topology::CongestionSample>
                  && std::is_trivially_destructible_v<topology::CongestionSample>);
    static_assert(!std::is_trivially_copyable_v<topology::CongestionSample>,
                  "a refined field keeps bytes from becoming a sample");
    static_assert(!std::is_default_constructible_v<topology::CongestionTelemetryWorker<2, 8>>);

    ::fixy::report(::fixy::Sink::Out, "test_congestion_telemetry:\n");
    test_names_and_admission();
    test_aggregate_and_drift();
    test_aggregate_finalize_guard();
    test_worker_recording();
    test_live_tcp_info_if_available();
    crucible::test::pass("test_congestion_telemetry: all PASSED\n");
    return 0;
}
