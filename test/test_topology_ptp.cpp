#include <crucible/topology/Ptp.h>
#include <foundation/core/Array.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <thread>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace topology = crucible::topology;

// A background context that may block on the file system, which is what
// opening /dev/ptpN needs.
using BgFsCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>>;

static topology::PositivePtpPathDelayNs path_delay(std::uint64_t ns) {
    return ::fixy::mint_refined<::fixy::positive>(ns);
}

static topology::PositivePtpSkewBoundNs skew_bound(std::uint64_t ns) {
    return ::fixy::mint_refined<::fixy::positive>(ns);
}

// The error of a result that the test knows to hold one.
template <class T>
[[nodiscard]] static topology::PtpError error_of(::fixy::Result<T, topology::PtpError> const& result) {
    return result.err().expect("the result holds an error");
}

static cog::CogIdentity nic(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x129, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

// The log spelling of the three enums comes from reflection.
static void test_name_accessors() {
    using ::foundation::reflect::enum_name;
    static_assert(enum_name(topology::PtpError::InvalidClockFd) == "InvalidClockFd");
    static_assert(enum_name(topology::PtpServoState::Slave) == "Slave");
    static_assert(enum_name(topology::PtpDegradationReason::ExcessiveSkew) == "ExcessiveSkew");
    volatile auto error = topology::PtpError::TimestampOverflow;
    assert(enum_name(static_cast<topology::PtpError>(error)) == std::string_view{"TimestampOverflow"});
    crucible::test::pass("  test_name_accessors:        PASSED\n");
}

static void test_fd_and_nic_admission() {
    auto bad_fd = topology::admit_ptp_clock_fd(-1);
    assert(bad_fd.is_err());
    assert(bad_fd.err() == ::fixy::some(topology::PtpError::InvalidClockFd));

    auto fd = topology::admit_ptp_clock_fd(7);
    assert(fd.is_ok());
    assert(std::move(fd).expect("7 is not negative").value() == 7);
    assert(topology::ptp_capable_cog(nic(1)));

    auto gpu = nic(2);
    gpu.kind = cog::CogKind::Gpu;
    assert(!topology::ptp_capable_cog(gpu));

    auto index = topology::admit_ptp_device_index(12);
    assert(index.is_ok());
    assert(std::move(index).expect("12 is a device index").value() == 12);
    auto too_high = topology::admit_ptp_device_index(256);
    assert(too_high.is_err());
    assert(too_high.err() == ::fixy::some(topology::PtpError::InvalidDeviceIndex));
    crucible::test::pass("  test_fd_and_nic_admission:  PASSED\n");
}

static void test_handle_status_and_timestamp() {
    auto fd = topology::admit_ptp_clock_fd(9);
    assert(fd.is_ok());
    topology::PtpStatus status{
        .servo = topology::PtpServoState::Slave,
        .offset_from_master_ns = 12,
        .mean_path_delay_ns = path_delay(800),
        .frequency_adjustment_ppb = -3,
        .skew_bound_ns = skew_bound(90),
        .sequence = 4,
    };
    auto handle = topology::mint_ptp_handle(eff::ColdInitCtx{::foundation::effects::testing::init()}, nic(3),
                                            std::move(fd).expect("9 is not negative"), status);
    assert(handle.status().synchronized());
    assert(error_of(handle.latest_timestamp()) == topology::PtpError::NoTimestamp);

    handle.record_timestamp(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                            ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::uint64_t{123456}), 5);
    auto const latest = handle.latest_timestamp().expect("the handle recorded a timestamp");
    assert(latest.value() == 123456);
    assert(handle.latest_timestamp_sequence() == 5);

    auto const degraded = ::fixy::mint_tagged<::fixy::tags::source::Ptp>(topology::PtpStatus{
        .servo = topology::PtpServoState::Degraded,
        .offset_from_master_ns = 1200,
        .mean_path_delay_ns = path_delay(900),
        .frequency_adjustment_ppb = 7,
        .skew_bound_ns = skew_bound(1500),
        .sequence = 6,
    });
    handle.record_status(eff::BgDrainCtx{::foundation::effects::testing::bg()}, degraded);
    assert(handle.status().servo == topology::PtpServoState::Degraded);
    assert(handle.status().sequence == 6);
    crucible::test::pass("  test_handle_status_and_timestamp: PASSED\n");
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
        .mean_path_delay_ns = path_delay(tick + 1u),
        .frequency_adjustment_ppb = -static_cast<std::int64_t>(tick),
        .skew_bound_ns = skew_bound((tick * 2u) + 1u),
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
    assert(fd.is_ok());
    auto handle = topology::mint_ptp_handle(eff::ColdInitCtx{::foundation::effects::testing::init()}, nic(4),
                                            std::move(fd).expect("11 is not negative"), status_at_tick(0));

    constexpr std::uint64_t ticks = 200000;
    std::atomic<bool> writer_done{false};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> reads{0};

    {
        std::jthread writer{[&handle, &writer_done] {
            for (std::uint64_t tick = 1; tick <= ticks; ++tick) {
                handle.record_status(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                                     ::fixy::mint_tagged<::fixy::tags::source::Ptp>(status_at_tick(tick)));
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
    ::fixy::report(::fixy::Sink::Out, "  test_status_seqlock_never_tears: PASSED ({} reads, 0 torn)\n",
                   static_cast<unsigned long long>(reads.load(std::memory_order_acquire)));
}

static void test_daemon_report_boundary() {
    topology::PtpDaemonReport report{
        .ptp4l_running = true,
        .phc2sys_running = true,
        .grandmaster_present = true,
        .servo = topology::PtpServoState::Slave,
        .offset_from_master_ns = -80,
        .mean_path_delay_ns = path_delay(700),
        .frequency_adjustment_ppb = 2,
        .skew_bound_ns = skew_bound(90),
        .max_accepted_skew_ns = skew_bound(100),
        .max_accepted_offset_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{100}),
        .sequence = 7,
    };
    eff::BgDrainCtx const bg{::foundation::effects::testing::bg()};
    auto declared = topology::admit_ptp_daemon_report(bg, report);
    auto status = topology::ptp_status_from_daemon_report(declared);
    assert(status.value().synchronized());
    assert(status.value().offset_from_master_ns == -80);
    assert(status.value().sequence == 7);

    auto diagnostic = topology::ptp_diagnostic_from_daemon_report(declared);
    assert(!diagnostic.value().degraded());
    assert(diagnostic.value().reason == topology::PtpDegradationReason::None);
    assert(::foundation::reflect::enum_name(diagnostic.value().reason) == std::string_view{"None"});

    report.grandmaster_present = false;
    auto no_grandmaster = topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(bg, report));
    assert(no_grandmaster.value().degraded());
    assert(no_grandmaster.value().reason == topology::PtpDegradationReason::GrandmasterMissing);
    assert(no_grandmaster.value().status.servo == topology::PtpServoState::Degraded);

    report.grandmaster_present = true;
    report.offset_from_master_ns = -101;
    auto excessive_offset = topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(bg, report));
    assert(excessive_offset.value().reason == topology::PtpDegradationReason::ExcessiveOffset);

    report.offset_from_master_ns = 0;
    report.skew_bound_ns = skew_bound(101);
    auto excessive_skew = topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(bg, report));
    assert(excessive_skew.value().reason == topology::PtpDegradationReason::ExcessiveSkew);

    crucible::test::pass("  test_daemon_report_boundary: PASSED\n");
}

static void test_timestamped_packet_view() {
    ::foundation::core::FixedArray<std::byte, 4> const payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto const stamp = ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::uint64_t{55});
    auto const packet =
        topology::timestamp_packet_view(payload.view(), stamp, 8).expect("a payload with bytes gives a packet");
    assert(packet.payload.size() == payload.size());
    assert(packet.timestamp_ns.value() == 55);
    assert(packet.sequence == 8);

    auto empty = topology::timestamp_packet_view(::fixy::View<std::byte const>{}, stamp, 9);
    assert(empty.is_err());
    assert(error_of(empty) == topology::PtpError::Degraded);
    crucible::test::pass("  test_timestamped_packet_view: PASSED\n");
}

// A build host usually has no /dev/ptpN, so the open takes the missing
// device path and names it OpenClockFailed.  Where a device exists, a read
// gives a stamp or a read failure and a capability query gives the caps or
// the query failure.
static void test_linux_boundaries_if_available() {
    BgFsCtx const bg_fs{::foundation::effects::testing::bg()};
    auto absent = topology::admit_ptp_device_index(254);
    assert(absent.is_ok());
    auto missing = topology::open_ptp_clock(bg_fs, std::move(absent).expect("254 is a device index"));
    if (missing.is_err()) {
        assert(error_of(missing) == topology::PtpError::OpenClockFailed);
    }

    auto index0 = topology::admit_ptp_device_index(0);
    assert(index0.is_ok());
    auto clock = topology::open_ptp_clock(bg_fs, std::move(index0).expect("0 is a device index"));
    if (clock.is_ok()) {
        topology::PtpClockReader const reader = std::move(clock).expect("the clock opened");
        auto stamp = topology::ptp_now(reader);
        assert(stamp.is_ok() || error_of(stamp) == topology::PtpError::ClockReadFailed
               || error_of(stamp) == topology::PtpError::TimestampOverflow);
        auto caps = topology::query_ptp_clock_caps(reader);
        assert(caps.is_ok() || error_of(caps) == topology::PtpError::ClockCapsUnavailable);
    } else {
        assert(error_of(clock) == topology::PtpError::OpenClockFailed);
    }

    crucible::test::pass("  test_linux_boundaries_if_available: PASSED\n");
}

// The socket calls on an idle TCP socket of the socket door.  The loopback
// interface has no hardware clock, so the kernel refuses its hardware
// stamps.  A socket with no data gives RecvFailed, and the receive does not
// wait.
static void test_socket_calls() {
    BgFsCtx const bg_fs{::foundation::effects::testing::bg()};
    ::fixy::fs::OwnedFd const tcp = ::fixy::net::mint_socket<::fixy::net::socket_kind::TcpStream>(bg_fs).match(
        [](::fixy::Linear<::fixy::fs::OwnedFd> opened) -> ::fixy::fs::OwnedFd { return std::move(opened).consume(); },
        [](::foundation::os::Errno) -> ::fixy::fs::OwnedFd { return ::fixy::fs::OwnedFd{}; });
    assert(tcp.is_open());
    crucible::cntp::SocketFd const sock_fd =
        crucible::cntp::admit_socket_fd(tcp.get()).expect("an open socket has a descriptor");
    assert(topology::enable_socket_timestamping(bg_fs, sock_fd).is_ok());

    crucible::cntp::NicInterfaceName const lo =
        crucible::cntp::NicInterfaceName::from("lo").expect("lo is an interface name");
    auto const configured = topology::configure_hardware_timestamping(bg_fs, sock_fd, lo);
    assert(error_of(configured) == topology::PtpError::HardwareTimestampingFailed);

    auto const empty = topology::recv_with_hw_timestamp(bg_fs, sock_fd, ::fixy::View<std::byte>{});
    assert(error_of(empty) == topology::PtpError::InvalidReceiveBuffer);

    ::foundation::core::FixedArray<std::byte, 16> buffer{};
    auto const idle = topology::recv_with_hw_timestamp(bg_fs, sock_fd, buffer.view());
    assert(error_of(idle) == topology::PtpError::RecvFailed);
    crucible::test::pass("  test_socket_calls:          PASSED\n");
}

// A receive of count bytes with the two stamps.
static ::fixy::net::TimestampedReceive stamped_receive(std::size_t count, ::foundation::os::KernelTimespec software,
                                                       ::foundation::os::KernelTimespec raw_hardware) {
    return ::fixy::net::TimestampedReceive{
        .count = count,
        .control = ::fixy::net::ReceiveStamps{.software = software, .raw_hardware = raw_hardware},
    };
}

// The packet of a receive: the raw hardware stamp wins over the software
// stamp, a stamp of zero is no stamp, and a stamp past 64 bits of
// nanoseconds stays apart from a malformed stamp.  The cases need no socket,
// because the loopback interface gives no hardware stamp and no overflow.
static void test_packet_of_receive() {
    using ::foundation::os::KernelTimespec;
    using topology::PtpError;
    ::foundation::core::FixedArray<std::byte, 16> buffer{};
    KernelTimespec const zero{};
    KernelTimespec const software{.seconds = 3, .nanoseconds = 4};
    KernelTimespec const hardware{.seconds = 5, .nanoseconds = 6};

    auto const both = topology::detail::packet_of_receive(buffer.view(), stamped_receive(8, software, hardware))
                          .expect("two stamps give a packet");
    assert(both.hardware);
    assert(both.timestamp_ns.value() == 5000000006ULL);
    assert(both.size == 8 && both.payload.size() == 8);

    auto const soft = topology::detail::packet_of_receive(buffer.view(), stamped_receive(16, software, zero))
                          .expect("a software stamp gives a packet");
    assert(!soft.hardware);
    assert(soft.timestamp_ns.value() == 3000000004ULL);
    assert(soft.size == 16 && soft.payload.size() == 16);

    auto const none = topology::detail::packet_of_receive(buffer.view(), stamped_receive(8, zero, zero));
    assert(error_of(none) == PtpError::NoTimestamp);

    auto const no_message = topology::detail::packet_of_receive(
        buffer.view(), ::fixy::net::TimestampedReceive{.count = 8, .control = ::fixy::net::NoTimestampMessage{}});
    assert(error_of(no_message) == PtpError::NoTimestamp);
    auto const short_message = topology::detail::packet_of_receive(
        buffer.view(), ::fixy::net::TimestampedReceive{.count = 8, .control = ::fixy::net::ShortTimestampMessage{}});
    assert(error_of(short_message) == PtpError::MalformedTimestampControl);

    auto const no_bytes = topology::detail::packet_of_receive(buffer.view(), stamped_receive(0, software, hardware));
    assert(error_of(no_bytes) == PtpError::RecvFailed);

    KernelTimespec const past_64_bits{.seconds = ::fixy::max_of<std::int64_t>, .nanoseconds = 0};
    auto const overflow = topology::detail::packet_of_receive(buffer.view(), stamped_receive(8, zero, past_64_bits));
    assert(error_of(overflow) == PtpError::TimestampOverflow);

    KernelTimespec const negative{.seconds = -1, .nanoseconds = 0};
    KernelTimespec const full_second{.seconds = 0, .nanoseconds = 1000000000};
    auto const below_zero = topology::detail::packet_of_receive(buffer.view(), stamped_receive(8, negative, zero));
    assert(error_of(below_zero) == PtpError::ClockReadFailed);
    auto const too_many_nanoseconds =
        topology::detail::packet_of_receive(buffer.view(), stamped_receive(8, zero, full_second));
    assert(error_of(too_many_nanoseconds) == PtpError::ClockReadFailed);

    // A malformed hardware stamp is an error, also beside a good software
    // stamp, because the hardware stamp wins.
    auto const bad_hardware =
        topology::detail::packet_of_receive(buffer.view(), stamped_receive(8, software, negative));
    assert(error_of(bad_hardware) == PtpError::ClockReadFailed);
    crucible::test::pass("  test_packet_of_receive:     PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsPtpMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsPtpMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsPtpRecord<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsPtpRecord<eff::HotFgCtx>);
    static_assert(::fixy::time::CtxFitsPtpClockReaderMint<BgFsCtx>);
    static_assert(!::fixy::time::CtxFitsPtpClockReaderMint<eff::BgDrainCtx>,
                  "opening /dev/ptpN blocks on the file system, so a context without Block is refused");

    ::fixy::report(::fixy::Sink::Out, "test_topology_ptp: 9 groups\n");
    test_name_accessors();
    test_fd_and_nic_admission();
    test_handle_status_and_timestamp();
    test_status_seqlock_never_tears();
    test_daemon_report_boundary();
    test_timestamped_packet_view();
    test_linux_boundaries_if_available();
    test_socket_calls();
    test_packet_of_receive();
    crucible::test::pass("test_topology_ptp: all passed\n");
    return 0;
}
