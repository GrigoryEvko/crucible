#include <crucible/topology/Ptp.h>

#include "test_assert.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace cog = crucible::cog;
namespace effects = crucible::effects;
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

}  // namespace

static cog::CogIdentity nic(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x129, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static void test_name_accessors() {
    assert(topology::ptp_error_name(topology::PtpError::InvalidClockFd) == std::string_view{"InvalidClockFd"});
    assert(topology::ptp_servo_state_name(topology::PtpServoState::Slave) == std::string_view{"Slave"});
    std::printf("  test_name_accessors:        PASSED\n");
}

static void test_fd_and_nic_admission() {
    auto bad_fd = topology::admit_ptp_clock_fd(-1);
    assert(!bad_fd.has_value());
    assert(bad_fd.error() == topology::PtpError::InvalidClockFd);

    auto fd = topology::admit_ptp_clock_fd(7);
    assert(fd.has_value());
    assert(fd->value() == 7);
    assert(topology::ptp_capable_cog(nic(1)));

    auto gpu = nic(2);
    gpu.kind = cog::CogKind::Gpu;
    assert(!topology::ptp_capable_cog(gpu));

    auto index = topology::admit_ptp_device_index(12);
    assert(index.has_value());
    auto path = topology::ptp_device_path(*index);
    assert(path.view() == std::string_view{"/dev/ptp12"});
    std::printf("  test_fd_and_nic_admission:  PASSED\n");
}

static void test_handle_status_and_timestamp() {
    auto fd = topology::admit_ptp_clock_fd(9);
    assert(fd.has_value());
    topology::PtpStatus status{
        .servo = topology::PtpServoState::Slave,
        .offset_from_master_ns = 12,
        .mean_path_delay_ns = topology::PositivePtpPathDelayNs{800},
        .frequency_adjustment_ppb = -3,
        .skew_bound_ns = topology::PositivePtpSkewBoundNs{90},
        .sequence = 4,
    };
    auto handle = topology::mint_ptp_handle(effects::ColdInitCtx{}, nic(3), *fd, status);
    assert(handle.status().synchronized());
    assert(handle.latest_timestamp().error() == topology::PtpError::NoTimestamp);

    handle.record_timestamp(effects::BgDrainCtx{}, topology::PtpTimestampNs{123456}, 5);
    auto latest = handle.latest_timestamp();
    assert(latest.has_value());
    assert(latest->value() == 123456);
    assert(handle.latest_timestamp_sequence() == 5);

    topology::DeclaredPtpStatus degraded{topology::PtpStatus{
        .servo = topology::PtpServoState::Degraded,
        .offset_from_master_ns = 1200,
        .mean_path_delay_ns = topology::PositivePtpPathDelayNs{900},
        .frequency_adjustment_ppb = 7,
        .skew_bound_ns = topology::PositivePtpSkewBoundNs{1500},
        .sequence = 6,
    }};
    handle.record_status(effects::BgDrainCtx{}, degraded);
    assert(handle.status().servo == topology::PtpServoState::Degraded);
    assert(handle.status().sequence == 6);
    std::printf("  test_handle_status_and_timestamp: PASSED\n");
}

// Every field of the status written at tick n is a function of n alone, so a
// status whose fields disagree is a status the reader assembled out of two
// different writes.  status() claims that cannot happen: it reads the epoch,
// reads the payload and reads the epoch again, and returns only when the two
// epoch reads agree.  Without an acquire fence between the payload reads and
// the closing epoch read the payload reads are free to move past it, and the
// comparison then says nothing about the payload it was meant to validate.
static topology::PtpStatus status_at_tick(std::uint64_t tick) noexcept {
    return topology::PtpStatus{
        .servo = (tick & 1u) != 0u ? topology::PtpServoState::Slave : topology::PtpServoState::Master,
        .offset_from_master_ns = static_cast<std::int64_t>(tick),
        .mean_path_delay_ns = topology::PositivePtpPathDelayNs{tick + 1u},
        .frequency_adjustment_ppb = -static_cast<std::int64_t>(tick),
        .skew_bound_ns = topology::PositivePtpSkewBoundNs{(tick * 2u) + 1u},
        .sequence = tick,
    };
}

static bool status_is_self_consistent(topology::PtpStatus const& observed) noexcept {
    auto const expected = status_at_tick(observed.sequence);
    return observed.servo == expected.servo && observed.offset_from_master_ns == expected.offset_from_master_ns
        && observed.mean_path_delay_ns.value() == expected.mean_path_delay_ns.value()
        && observed.frequency_adjustment_ppb == expected.frequency_adjustment_ppb
        && observed.skew_bound_ns.value() == expected.skew_bound_ns.value();
}

static void test_status_seqlock_never_tears() {
    auto fd = topology::admit_ptp_clock_fd(11);
    assert(fd.has_value());
    auto handle = topology::mint_ptp_handle(effects::ColdInitCtx{}, nic(4), *fd, status_at_tick(0));

    constexpr std::uint64_t ticks = 200000;
    std::atomic<bool> writer_done{false};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> reads{0};

    {
        std::jthread writer{[&handle, &writer_done] {
            for (std::uint64_t tick = 1; tick <= ticks; ++tick) {
                handle.record_status(effects::BgDrainCtx{}, topology::DeclaredPtpStatus{status_at_tick(tick)});
            }
            writer_done.store(true, std::memory_order_release);
        }};

        std::jthread reader{[&handle, &writer_done, &torn, &reads] {
            std::uint64_t local_torn = 0;
            std::uint64_t local_reads = 0;
            while (!writer_done.load(std::memory_order_acquire)) {
                auto const observed = handle.status();
                ++local_reads;
                if (!status_is_self_consistent(observed)) {
                    ++local_torn;
                }
            }
            torn.store(local_torn, std::memory_order_release);
            reads.store(local_reads, std::memory_order_release);
        }};
    }

    assert(torn.load(std::memory_order_acquire) == 0);
    assert(handle.status().sequence == ticks);
    std::printf("  test_status_seqlock_never_tears: PASSED (%llu reads, 0 torn)\n",
                static_cast<unsigned long long>(reads.load(std::memory_order_acquire)));
}

static void test_daemon_report_boundary() {
    topology::PtpDaemonReport report{
        .ptp4l_running = true,
        .phc2sys_running = true,
        .grandmaster_present = true,
        .servo = topology::PtpServoState::Slave,
        .offset_from_master_ns = -80,
        .mean_path_delay_ns = topology::PositivePtpPathDelayNs{700},
        .frequency_adjustment_ppb = 2,
        .skew_bound_ns = topology::PositivePtpSkewBoundNs{90},
        .max_accepted_skew_ns = topology::PositivePtpSkewBoundNs{100},
        .max_accepted_offset_ns = topology::PositivePtpOffsetBoundNs{100},
        .sequence = 7,
    };
    auto declared = topology::admit_ptp_daemon_report(effects::BgDrainCtx{}, report);
    auto status = topology::ptp_status_from_daemon_report(declared);
    assert(status.value().synchronized());
    assert(status.value().offset_from_master_ns == -80);
    assert(status.value().sequence == 7);

    auto diagnostic = topology::ptp_diagnostic_from_daemon_report(declared);
    assert(!diagnostic.value().degraded());
    assert(diagnostic.value().reason == topology::PtpDegradationReason::None);
    assert(topology::ptp_degradation_reason_name(diagnostic.value().reason) == std::string_view{"None"});

    report.grandmaster_present = false;
    auto no_grandmaster =
        topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(effects::BgDrainCtx{}, report));
    assert(no_grandmaster.value().degraded());
    assert(no_grandmaster.value().reason == topology::PtpDegradationReason::GrandmasterMissing);
    assert(no_grandmaster.value().status.servo == topology::PtpServoState::Degraded);

    report.grandmaster_present = true;
    report.offset_from_master_ns = -101;
    auto excessive_offset =
        topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(effects::BgDrainCtx{}, report));
    assert(excessive_offset.value().reason == topology::PtpDegradationReason::ExcessiveOffset);

    report.offset_from_master_ns = 0;
    report.skew_bound_ns = topology::PositivePtpSkewBoundNs{101};
    auto excessive_skew =
        topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(effects::BgDrainCtx{}, report));
    assert(excessive_skew.value().reason == topology::PtpDegradationReason::ExcessiveSkew);

    std::printf("  test_daemon_report_boundary: PASSED\n");
}

static void test_timestamped_packet_view() {
    std::array<std::byte, 4> payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto packet = topology::timestamp_packet_view(std::span<const std::byte>{payload}, topology::PtpTimestampNs{55}, 8);
    assert(packet.has_value());
    assert(packet->payload.size() == payload.size());
    assert(packet->timestamp_ns.value() == 55);
    assert(packet->sequence == 8);

    auto empty = topology::timestamp_packet_view(std::span<const std::byte>{}, topology::PtpTimestampNs{55}, 9);
    assert(!empty.has_value());
    assert(empty.error() == topology::PtpError::Degraded);
    std::printf("  test_timestamped_packet_view: PASSED\n");
}

static void test_linux_boundaries_if_available() {
    auto fd = topology::admit_ptp_clock_fd(999999);
    assert(fd.has_value());
    auto now = topology::ptp_now(*fd);
    assert(!now.has_value());
    assert(now.error() == topology::PtpError::ClockReadFailed);

    auto caps = topology::query_ptp_clock_caps(*fd);
    assert(!caps.has_value());
    assert(caps.error() == topology::PtpError::ClockCapsUnavailable);

    auto index0 = topology::admit_ptp_device_index(0);
    assert(index0.has_value());
    auto clock = topology::open_ptp_clock(*index0);
    if (clock.has_value()) {
        assert(clock->peek().valid());
        auto stamp = topology::ptp_now(clock->peek().fd());
        assert(stamp.has_value() || stamp.error() == topology::PtpError::ClockReadFailed);
    }

    TestSocket socket{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());
    auto sock_fd = crucible::cntp::admit_socket_fd(socket.raw());
    assert(sock_fd.has_value());
    auto enabled = topology::enable_socket_timestamping(*sock_fd);
    assert(enabled.has_value() || enabled.error() == topology::PtpError::SocketTimestampingFailed);

    auto lo = crucible::cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());
    auto configured = topology::configure_hardware_timestamping(*sock_fd, *lo);
    assert(configured.has_value() || configured.error() == topology::PtpError::HardwareTimestampingFailed);

    std::array<std::byte, 8> payload{};
    auto empty = topology::recv_with_hw_timestamp(*sock_fd, std::span<std::byte>{});
    assert(!empty.has_value());
    assert(empty.error() == topology::PtpError::InvalidReceiveBuffer);

    auto recv = topology::recv_with_hw_timestamp(*sock_fd, std::span<std::byte>{payload});
    assert(recv.has_value() || recv.error() == topology::PtpError::RecvFailed
           || recv.error() == topology::PtpError::NoTimestamp);
    std::printf("  test_linux_boundaries_if_available: PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsPtpMint<effects::ColdInitCtx>);
    static_assert(!topology::CtxFitsPtpMint<effects::BgDrainCtx>);
    static_assert(topology::CtxFitsPtpRecord<effects::BgDrainCtx>);
    static_assert(!topology::CtxFitsPtpRecord<effects::HotFgCtx>);

    std::printf("test_topology_ptp: 7 groups\n");
    test_name_accessors();
    test_fd_and_nic_admission();
    test_handle_status_and_timestamp();
    test_status_seqlock_never_tears();
    test_daemon_report_boundary();
    test_timestamped_packet_view();
    test_linux_boundaries_if_available();
    std::printf("test_topology_ptp: all passed\n");
    return 0;
}
