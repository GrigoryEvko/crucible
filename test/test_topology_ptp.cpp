#include <crucible/topology/Ptp.h>
#include <foundation/core/Array.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <thread>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace topology = crucible::topology;

// True when the test runs under the emulator of a cross build, qemu user mode
// (test/CMakeLists.txt sets the definition).
constexpr bool kIsUnderEmulator = CRUCIBLE_TEST_UNDER_EMULATOR != 0;

// A background context that may block on the file system, which is what
// opening /dev/ptpN needs, and that reads a clock and a socket.
using BgFsCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block, ::foundation::effects::Effect::Nondet>>;

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
    using ::fixy::enum_name_text;
    static_assert(enum_name_text(topology::PtpError::NonNicCog) == "NonNicCog");
    static_assert(enum_name_text(topology::PtpServoState::Slave) == "Slave");
    static_assert(enum_name_text(topology::PtpDegradationReason::ExcessiveSkew) == "ExcessiveSkew");
    volatile auto error = topology::PtpError::TimestampOverflow;
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(static_cast<topology::PtpError>(error))
                                    == ::fixy::TextView{"TimestampOverflow"});
    ::fixy::check::pass(kTest, "  test_name_accessors:        PASSED\n");
}

// The reader of no device, for a handle on a host with no /dev/ptpN.
[[nodiscard]] static topology::PtpClockReader empty_clock() {
    return ::fixy::time::mint_empty_ptp_clock_reader(::fixy::TestRunnerCtx{::foundation::effects::testing::test()});
}

static void test_nic_and_device_admission() {
    CRUCIBLE_TEST_ASSERT(kTest, topology::ptp_capable_cog(nic(1)));

    auto gpu = nic(2);
    gpu.kind = cog::CogKind::Gpu;
    CRUCIBLE_TEST_ASSERT(kTest, !topology::ptp_capable_cog(gpu));

    auto index = topology::admit_ptp_device_index(12);
    CRUCIBLE_TEST_ASSERT(kTest, index.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(index).expect("12 is a device index").value() == 12);
    auto too_high = topology::admit_ptp_device_index(256);
    CRUCIBLE_TEST_ASSERT(kTest, too_high.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, too_high.err() == ::fixy::some(topology::PtpError::InvalidDeviceIndex));
    ::fixy::check::pass(kTest, "  test_nic_and_device_admission: PASSED\n");
}

// A handle owns the reader of its clock, and the clock of the handle is
// that reader.  The reader of no device refuses each read and each query of
// the capabilities.
static void test_handle_owns_its_clock() {
    auto const handle =
        topology::mint_ptp_handle(eff::ColdInitCtx{::foundation::effects::testing::init()}, nic(5), empty_clock());
    CRUCIBLE_TEST_ASSERT(kTest,
                         topology::ptp_now(handle.clock(), ::foundation::effects::testing::test().nondet).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(topology::query_ptp_clock_caps(handle.clock()))
                                    == topology::PtpError::ClockCapsUnavailable);
    CRUCIBLE_TEST_ASSERT(kTest, handle.nic().uuid == nic(5).uuid);
    ::fixy::check::pass(kTest, "  test_handle_owns_its_clock: PASSED\n");
}

static void test_handle_status_and_timestamp() {
    topology::PtpStatus status{
        .servo = topology::PtpServoState::Slave,
        .offset_from_master_ns = 12,
        .mean_path_delay_ns = path_delay(800),
        .frequency_adjustment_ppb = -3,
        .skew_bound_ns = skew_bound(90),
        .sequence = 4,
    };
    auto handle = topology::mint_ptp_handle(eff::ColdInitCtx{::foundation::effects::testing::init()}, nic(3),
                                            empty_clock(), status);
    CRUCIBLE_TEST_ASSERT(kTest, handle.status().synchronized());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(handle.latest_timestamp()) == topology::PtpError::NoTimestamp);

    handle.record_timestamp(eff::BgDrainCtx{::foundation::effects::testing::bg()},
                            ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::uint64_t{123456}), 5);
    auto const latest = handle.latest_timestamp().expect("the handle recorded a timestamp");
    CRUCIBLE_TEST_ASSERT(kTest, latest.value() == 123456);
    CRUCIBLE_TEST_ASSERT(kTest, handle.latest_timestamp_sequence() == 5);

    auto const degraded = ::fixy::mint_tagged<::fixy::tags::source::Ptp>(topology::PtpStatus{
        .servo = topology::PtpServoState::Degraded,
        .offset_from_master_ns = 1200,
        .mean_path_delay_ns = path_delay(900),
        .frequency_adjustment_ppb = 7,
        .skew_bound_ns = skew_bound(1500),
        .sequence = 6,
    });
    handle.record_status(eff::BgDrainCtx{::foundation::effects::testing::bg()}, degraded);
    CRUCIBLE_TEST_ASSERT(kTest, handle.status().servo == topology::PtpServoState::Degraded);
    CRUCIBLE_TEST_ASSERT(kTest, handle.status().sequence == 6);
    ::fixy::check::pass(kTest, "  test_handle_status_and_timestamp: PASSED\n");
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
    auto handle = topology::mint_ptp_handle(eff::ColdInitCtx{::foundation::effects::testing::init()}, nic(4),
                                            empty_clock(), status_at_tick(0));

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

    CRUCIBLE_TEST_ASSERT(kTest, torn.load(std::memory_order_acquire) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, handle.status().sequence == ticks);
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
    CRUCIBLE_TEST_ASSERT(kTest, status.value().synchronized());
    CRUCIBLE_TEST_ASSERT(kTest, status.value().offset_from_master_ns == -80);
    CRUCIBLE_TEST_ASSERT(kTest, status.value().sequence == 7);

    auto diagnostic = topology::ptp_diagnostic_from_daemon_report(declared);
    CRUCIBLE_TEST_ASSERT(kTest, !diagnostic.value().degraded());
    CRUCIBLE_TEST_ASSERT(kTest, diagnostic.value().reason == topology::PtpDegradationReason::None);
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::enum_name_text(diagnostic.value().reason) == ::fixy::TextView{"None"});

    report.grandmaster_present = false;
    auto no_grandmaster = topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(bg, report));
    CRUCIBLE_TEST_ASSERT(kTest, no_grandmaster.value().degraded());
    CRUCIBLE_TEST_ASSERT(kTest, no_grandmaster.value().reason == topology::PtpDegradationReason::GrandmasterMissing);
    CRUCIBLE_TEST_ASSERT(kTest, no_grandmaster.value().status.servo == topology::PtpServoState::Degraded);

    report.grandmaster_present = true;
    report.offset_from_master_ns = -101;
    auto excessive_offset = topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(bg, report));
    CRUCIBLE_TEST_ASSERT(kTest, excessive_offset.value().reason == topology::PtpDegradationReason::ExcessiveOffset);

    report.offset_from_master_ns = 0;
    report.skew_bound_ns = skew_bound(101);
    auto excessive_skew = topology::ptp_diagnostic_from_daemon_report(topology::admit_ptp_daemon_report(bg, report));
    CRUCIBLE_TEST_ASSERT(kTest, excessive_skew.value().reason == topology::PtpDegradationReason::ExcessiveSkew);

    ::fixy::check::pass(kTest, "  test_daemon_report_boundary: PASSED\n");
}

static void test_timestamped_packet_view() {
    ::foundation::core::FixedArray<std::byte, 4> const payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto const stamp = ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::uint64_t{55});
    auto const packet =
        topology::timestamp_packet_view(payload.view(), stamp, 8).expect("a payload with bytes gives a packet");
    CRUCIBLE_TEST_ASSERT(kTest, packet.payload.size() == payload.size());
    CRUCIBLE_TEST_ASSERT(kTest, packet.timestamp_ns.value() == 55);
    CRUCIBLE_TEST_ASSERT(kTest, packet.sequence == 8);

    auto empty = topology::timestamp_packet_view(::fixy::View<std::byte const>{}, stamp, 9);
    CRUCIBLE_TEST_ASSERT(kTest, empty.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(empty) == topology::PtpError::Degraded);
    ::fixy::check::pass(kTest, "  test_timestamped_packet_view: PASSED\n");
}

// A build host usually has no /dev/ptpN, so the open takes the missing
// device path and names it OpenClockFailed.  Where a device exists, a read
// gives a stamp or a read failure and a capability query gives the caps or
// the query failure.
static void test_linux_boundaries_if_available() {
    BgFsCtx const bg_fs{::foundation::effects::testing::bg()};
    auto absent = topology::admit_ptp_device_index(254);
    CRUCIBLE_TEST_ASSERT(kTest, absent.is_ok());
    auto missing = topology::open_ptp_clock(bg_fs, std::move(absent).expect("254 is a device index"));
    if (missing.is_err()) {
        CRUCIBLE_TEST_ASSERT(kTest, error_of(missing) == topology::PtpError::OpenClockFailed);
    }

    auto index0 = topology::admit_ptp_device_index(0);
    CRUCIBLE_TEST_ASSERT(kTest, index0.is_ok());
    auto clock = topology::open_ptp_clock(bg_fs, std::move(index0).expect("0 is a device index"));
    if (clock.is_ok()) {
        auto const handle = topology::mint_ptp_handle(eff::ColdInitCtx{::foundation::effects::testing::init()}, nic(6),
                                                      std::move(clock).expect("the clock opened"));
        auto stamp = topology::ptp_now(handle.clock(), ::foundation::effects::testing::test().nondet);
        CRUCIBLE_TEST_ASSERT(kTest, stamp.is_ok() || error_of(stamp) == topology::PtpError::ClockReadFailed
                                        || error_of(stamp) == topology::PtpError::TimestampOverflow);
        auto caps = topology::query_ptp_clock_caps(handle.clock());
        CRUCIBLE_TEST_ASSERT(kTest, caps.is_ok() || error_of(caps) == topology::PtpError::ClockCapsUnavailable);
    } else {
        CRUCIBLE_TEST_ASSERT(kTest, error_of(clock) == topology::PtpError::OpenClockFailed);
    }

    ::fixy::check::pass(kTest, "  test_linux_boundaries_if_available: PASSED\n");
}

// The socket calls on an idle TCP socket of the socket door.  The loopback
// interface has no hardware clock, so the kernel refuses its hardware
// stamps.  A socket with no data gives RecvFailed, and the receive does not
// wait.
static void test_socket_calls() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{4096};
    BgFsCtx const bg_fs{::foundation::effects::testing::bg()};
    using TcpSocket = ::fixy::net::SocketHandle<::fixy::net::socket_kind::TcpStream>;
    TcpSocket const tcp = ::fixy::net::mint_socket<::fixy::net::socket_kind::TcpStream>(bg_fs).match(
        [](::fixy::Linear<TcpSocket> opened) -> TcpSocket { return std::move(opened).consume(); },
        [](::foundation::os::Errno) -> TcpSocket { return TcpSocket{}; });
    CRUCIBLE_TEST_ASSERT(kTest, tcp.is_open());
    crucible::cntp::SocketFd const sock_fd = ::fixy::net::share_socket(tcp.ref());
    // qemu user mode has no SO_TIMESTAMPING, and the door of the socket gives
    // its enoprotoopt.  A real kernel takes the option.  The receive below
    // needs no stamps, so it runs in each case.
    auto const stamping = topology::enable_socket_timestamping(bg_fs, sock_fd);
    bool const is_emulator_refusal =
        kIsUnderEmulator && stamping.is_err()
        && ::fixy::net::set_socket_timestamping(bg_fs, sock_fd).err().value_or(::foundation::os::Errno{})
               == ::foundation::os::Errno::enoprotoopt;
    CRUCIBLE_TEST_ASSERT(kTest, stamping.is_ok() || is_emulator_refusal);
    if (is_emulator_refusal) {
        ::fixy::report(::fixy::Sink::Out,
                       "  test_socket_calls: the stamps SKIPPED (qemu-user has no SO_TIMESTAMPING)\n");
    }

    crucible::cntp::NicInterfaceName const lo =
        crucible::cntp::NicInterfaceName::from("lo").expect("lo is an interface name");
    auto const configured = topology::configure_hardware_timestamping(bg_fs, sock_fd, lo);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(configured) == topology::PtpError::HardwareTimestampingFailed);

    auto const empty = topology::recv_with_hw_timestamp(bg_fs, sock_fd, ::fixy::View<std::byte>{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(empty) == topology::PtpError::InvalidReceiveBuffer);

    ::fixy::View<std::byte> const buffer = arena.array(test.alloc, 16, std::byte{0});
    auto const idle = topology::recv_with_hw_timestamp(bg_fs, sock_fd, buffer);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(idle) == topology::PtpError::RecvFailed);
    ::fixy::check::pass(kTest, "  test_socket_calls:          PASSED\n");
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
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{4096};
    ::fixy::View<std::byte> const buffer = arena.array(test.alloc, 16, std::byte{0});
    KernelTimespec const zero{};
    KernelTimespec const software{.seconds = 3, .nanoseconds = 4};
    KernelTimespec const hardware{.seconds = 5, .nanoseconds = 6};

    auto const both = topology::detail::packet_of_receive(buffer, stamped_receive(8, software, hardware))
                          .expect("two stamps give a packet");
    CRUCIBLE_TEST_ASSERT(kTest, both.hardware);
    CRUCIBLE_TEST_ASSERT(kTest, both.timestamp_ns.value() == 5000000006ULL);
    CRUCIBLE_TEST_ASSERT(kTest, both.size == 8 && both.payload.size() == 8);

    auto const soft = topology::detail::packet_of_receive(buffer, stamped_receive(16, software, zero))
                          .expect("a software stamp gives a packet");
    CRUCIBLE_TEST_ASSERT(kTest, !soft.hardware);
    CRUCIBLE_TEST_ASSERT(kTest, soft.timestamp_ns.value() == 3000000004ULL);
    CRUCIBLE_TEST_ASSERT(kTest, soft.size == 16 && soft.payload.size() == 16);

    auto const none = topology::detail::packet_of_receive(buffer, stamped_receive(8, zero, zero));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(none) == PtpError::NoTimestamp);

    auto const no_message = topology::detail::packet_of_receive(
        buffer, ::fixy::net::TimestampedReceive{.count = 8, .control = ::fixy::net::NoTimestampMessage{}});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(no_message) == PtpError::NoTimestamp);
    auto const short_message = topology::detail::packet_of_receive(
        buffer, ::fixy::net::TimestampedReceive{.count = 8, .control = ::fixy::net::ShortTimestampMessage{}});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(short_message) == PtpError::MalformedTimestampControl);

    auto const no_bytes = topology::detail::packet_of_receive(buffer, stamped_receive(0, software, hardware));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(no_bytes) == PtpError::RecvFailed);

    KernelTimespec const past_64_bits{.seconds = ::fixy::max_of<std::int64_t>, .nanoseconds = 0};
    auto const overflow = topology::detail::packet_of_receive(buffer, stamped_receive(8, zero, past_64_bits));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(overflow) == PtpError::TimestampOverflow);

    KernelTimespec const negative{.seconds = -1, .nanoseconds = 0};
    KernelTimespec const full_second{.seconds = 0, .nanoseconds = 1000000000};
    auto const below_zero = topology::detail::packet_of_receive(buffer, stamped_receive(8, negative, zero));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(below_zero) == PtpError::ClockReadFailed);
    auto const too_many_nanoseconds =
        topology::detail::packet_of_receive(buffer, stamped_receive(8, zero, full_second));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(too_many_nanoseconds) == PtpError::ClockReadFailed);

    // A malformed hardware stamp is an error, also beside a good software
    // stamp, because the hardware stamp wins.
    auto const bad_hardware = topology::detail::packet_of_receive(buffer, stamped_receive(8, software, negative));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(bad_hardware) == PtpError::ClockReadFailed);
    ::fixy::check::pass(kTest, "  test_packet_of_receive:     PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsPtpMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsPtpMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsPtpRecord<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsPtpRecord<eff::HotFgCtx>);
    static_assert(::fixy::time::CtxFitsPtpClockReaderMint<BgFsCtx>);
    static_assert(!::fixy::time::CtxFitsPtpClockReaderMint<eff::BgDrainCtx>,
                  "opening /dev/ptpN blocks on the file system, so a context without Block is refused");

    ::fixy::report(::fixy::Sink::Out, "test_topology_ptp: 10 groups\n");
    test_name_accessors();
    test_nic_and_device_admission();
    test_handle_owns_its_clock();
    test_handle_status_and_timestamp();
    test_status_seqlock_never_tears();
    test_daemon_report_boundary();
    test_timestamped_packet_view();
    test_linux_boundaries_if_available();
    test_socket_calls();
    test_packet_of_receive();
    ::fixy::check::pass(kTest, "test_topology_ptp: all passed\n");
    return 0;
}
