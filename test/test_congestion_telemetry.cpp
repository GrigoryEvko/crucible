#include <crucible/rt/CongestionTelemetry.h>
#include <crucible/topology/CongestionTelemetry.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace rt = crucible::rt;
namespace safety = crucible::safety;
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

topology::CongestionState state(std::uint64_t bw,
                                std::uint64_t rtt,
                                topology::CongestionMode mode) {
    topology::CongestionState s{};
    s.algorithm = cntp::CcAlgorithm::Bbr3;
    s.btl_bw_bps = topology::PositiveBandwidthBps{
        bw, typename topology::PositiveBandwidthBps::Trusted{}};
    s.rt_prop_us = topology::PositiveMicroseconds{
        rtt, typename topology::PositiveMicroseconds::Trusted{}};
    s.cwnd_bytes = topology::PositiveWindowBytes{
        std::uint32_t{65'536},
        typename topology::PositiveWindowBytes::Trusted{}};
    s.ssthresh_bytes = topology::PositiveWindowBytes{
        std::uint32_t{131'072},
        typename topology::PositiveWindowBytes::Trusted{}};
    s.in_flight_bytes = 32'768;
    s.mode = mode;
    s.has_bbr = true;
    s.bbr.btl_bw_bps = s.btl_bw_bps;
    s.bbr.rt_prop_us = s.rt_prop_us;
    return s;
}

topology::TcpInfoSnapshot sample(std::uint64_t bw,
                                 std::uint64_t rtt,
                                 topology::CongestionMode mode) {
    auto tagged = topology::tag_tcp_info_for_test(state(bw, rtt, mode));
    assert(tagged.has_value());
    return *tagged;
}

void test_names_and_admission() {
    assert(topology::congestion_mode_name(topology::CongestionMode::BbrDrain) ==
           std::string_view{"BbrDrain"});
    assert(topology::telemetry_error_name(topology::TelemetryError::InvalidNicCog) ==
           std::string_view{"InvalidNicCog"});
    auto zero = topology::admit_sample_period_ns(0);
    assert(!zero.has_value());
    assert(zero.error() == topology::TelemetryError::DeadlineOverflow);
    auto period = topology::admit_sample_period_ns(1'000);
    assert(period.has_value());
    std::printf("  test_names_and_admission: PASSED\n");
}

void test_aggregate_and_drift() {
    std::array samples{
        sample(1'000'000'000, 100, topology::CongestionMode::Open),
        sample(800'000'000, 120, topology::CongestionMode::BbrProbeBw),
        sample(600'000'000, 150, topology::CongestionMode::Recovery),
        sample(500'000'000, 180, topology::CongestionMode::Loss),
    };

    auto aggregate = topology::aggregate_congestion(nic(1), std::span{samples});
    assert(aggregate.nic_uuid == nic(1).uuid);
    assert(aggregate.sample_count == samples.size());
    assert(aggregate.p95_rtt_us >= 150);
    assert(aggregate.p95_btl_bw_bps >= 800'000'000);
    assert(aggregate.mean_btl_bw_bps == 725'000'000);
    assert(aggregate.worst_mode == topology::CongestionMode::Loss);

    auto baseline = topology::PositiveBandwidthBps{
        std::uint64_t{1'200'000'000},
        typename topology::PositiveBandwidthBps::Trusted{}};
    auto drift = topology::detect_congestion_drift(
        aggregate, baseline,
        topology::CongestionDriftPolicy{
            .bandwidth_drop_ppm = 100'000,
            .min_samples = 4,
        });
    assert(drift.degraded);
    assert(drift.bandwidth_drop_ppm >= 100'000);
    std::printf("  test_aggregate_and_drift: PASSED\n");
}

void test_worker_recording() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto worker =
        rt::mint_congestion_telemetry_worker<2, 8>(init);

    std::array nics{nic(10), nic(11)};
    auto start = worker.start(init, std::span{nics});
    assert(start.has_value());

    std::array samples{
        sample(900'000'000, 90, topology::CongestionMode::Open),
        sample(700'000'000, 110, topology::CongestionMode::Cwr),
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

    rt::CongestionObservationSet sinks{};
    rt::publish_congestion(sinks, 0, *last, 7);
    auto p95 = rt::latest_observation(
        sinks[static_cast<std::size_t>(rt::CongestionMetricSlot::P95BandwidthBps)]);
    assert(p95.metric_id == rt::congestion_metric_id(
        0, rt::CongestionMetricSlot::P95BandwidthBps));
    assert(p95.sequence == 7);
    std::printf("  test_worker_recording: PASSED\n");
}

void test_live_tcp_info_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());
    auto fd = cntp::admit_socket_fd(socket.raw());
    assert(fd.has_value());

    auto harvested = topology::harvest_socket(*fd);
    if (!harvested.has_value()) {
        std::printf("  test_live_tcp_info_if_available: SKIPPED\n");
        return;
    }
    assert(harvested->value().rt_prop_us.value() > 0);
    assert(harvested->value().cwnd_bytes.value() > 0);

    std::array fds{*fd};
    auto aggregate = topology::harvest_per_link(nic(77), std::span{fds});
    assert(aggregate.has_value());
    assert(aggregate->sample_count == 1);

    auto wrong_cog = topology::harvest_per_link(gpu(88), std::span{fds});
    assert(!wrong_cog.has_value());
    assert(wrong_cog.error() == topology::TelemetryError::InvalidNicCog);
    std::printf("  test_live_tcp_info_if_available: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(topology::TcpInfoSnapshot) ==
                  sizeof(topology::CongestionState));
    static_assert(rt::CtxFitsCongestionTelemetryStart<effects::ColdInitCtx>);
    static_assert(!rt::CtxFitsCongestionTelemetryStart<effects::BgDrainCtx>);
    static_assert(rt::CtxFitsCongestionTelemetryHarvest<effects::BgDrainCtx>);
    static_assert(!rt::CtxFitsCongestionTelemetryHarvest<effects::HotFgCtx>);
    static_assert(std::same_as<
                  topology::TcpInfoSnapshot::tag_type,
                  safety::source::TcpInfo>);
    static_assert(std::is_trivially_copyable_v<topology::CongestionState>);

    std::printf("test_congestion_telemetry:\n");
    test_names_and_admission();
    test_aggregate_and_drift();
    test_worker_recording();
    test_live_tcp_info_if_available();
    std::printf("test_congestion_telemetry: all PASSED\n");
    return 0;
}
