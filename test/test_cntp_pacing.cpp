#include <crucible/cntp/Pacing.h>
#include <fixy/Ctx.h>
#include <fixy/os/Socket.h>
#include <foundation/effects/Effect.h>

#include <array>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;

namespace {

// Probes for the construction guarantee on NicInterfaceName.  The length
// is private and from() is its only writer, so a caller cannot install a
// length that no validation saw.  Before that,
// `cntp::NicInterfaceName n{}; n.size = 200;` was well-formed and the
// value reached the memcpy plus indexed NUL store into ifreq::ifr_name in
// configure_hardware_timestamping, a char[IFNAMSIZ].
template <typename T>
concept HasWritableLengthMember = requires(T value) { value.size = static_cast<std::uint8_t>(200); };

template <typename T>
concept HasReadableByteMember = requires(T value) { value.bytes; };

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

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, cntp::PacingError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.  A call that succeeds ends
// the process.
template <class Value>
cntp::PacingError error_of(::fixy::Result<Value, cntp::PacingError> result) {
    return std::move(result).err().expect("the call fails in this test");
}

void test_interface_name_admission() {
    cntp::NicInterfaceName const lo = take(cntp::NicInterfaceName::from("lo"));
    assert(lo.view() == "lo");

    cntp::NicInterfaceName const colon = take(cntp::NicInterfaceName::from("enp1s0f0:1"));
    assert(colon.view() == "enp1s0f0:1");

    // A name of max_bytes - 1 characters fits beside its zero, and one more
    // character does not.
    cntp::NicInterfaceName const longest = take(cntp::NicInterfaceName::from("0123456789abcde"));
    assert(longest.view().size() == cntp::NicInterfaceName::max_bytes - 1);
    assert(error_of(cntp::NicInterfaceName::from("0123456789abcdef")) == cntp::PacingError::InvalidInterfaceName);

    assert(error_of(cntp::NicInterfaceName::from("")) == cntp::PacingError::InvalidInterfaceName);
    assert(error_of(cntp::NicInterfaceName::from("../eth0")) == cntp::PacingError::InvalidInterfaceName);

    // "." and ".." name a directory and no interface, and the kernel refuses
    // them as interface names.  A sysfs path of either would leave the
    // directory of the interface.
    assert(error_of(cntp::NicInterfaceName::from(".")) == cntp::PacingError::InvalidInterfaceName);
    assert(error_of(cntp::NicInterfaceName::from("..")) == cntp::PacingError::InvalidInterfaceName);
    cntp::NicInterfaceName const three_dots = take(cntp::NicInterfaceName::from("..."));
    assert(three_dots.view() == "...");

    crucible::test::pass("  test_interface_name_admission: PASSED\n");
}

// The sysfs path of a name stays in the directory of the interface.
void test_interface_path() {
    cntp::NicInterfaceName const lo = take(cntp::NicInterfaceName::from("lo"));
    cntp::detail::InterfacePath const path = cntp::detail::interface_path_of(lo, "ifindex");
    assert(path.c_text().view() == "/sys/class/net/lo/ifindex");

    // The index of a missing interface is refused.  The name holds the
    // characters that the type admits, and no interface has it.
    cntp::NicInterfaceName const missing = take(cntp::NicInterfaceName::from("no-such-nic.0"));
    assert(error_of(cntp::detail::read_interface_index(missing)) == cntp::PacingError::InterfaceNotFound);

    crucible::test::pass("  test_interface_path:           PASSED\n");
}

void test_qdisc_parse() {
    cntp::Qdisc const fq = take(cntp::qdisc_from_kernel_name("fq"));
    assert(fq == cntp::Qdisc::Fq);
    assert(cntp::qdisc_name(fq) == "fq");

    assert(take(cntp::qdisc_from_kernel_name("fq_codel")) == cntp::Qdisc::FqCodel);
    assert(take(cntp::qdisc_from_kernel_name("pfifo_fast")) == cntp::Qdisc::Pfifo);
    assert(take(cntp::qdisc_from_kernel_name("mq")) == cntp::Qdisc::Mq);
    assert(take(cntp::qdisc_from_kernel_name("noqueue")) == cntp::Qdisc::Noqueue);
    assert(take(cntp::qdisc_from_kernel_name("cake")) == cntp::Qdisc::Other);
    assert(error_of(cntp::qdisc_from_kernel_name("")) == cntp::PacingError::QdiscKindMissing);

    assert(take(cntp::parse_tc_qdisc_show("qdisc fq 8001: root refcnt 2 limit 10000p flow_limit 100p\n"))
           == cntp::Qdisc::Fq);
    assert(take(cntp::parse_tc_qdisc_show("qdisc fq_codel 0: root\n")) == cntp::Qdisc::FqCodel);
    assert(error_of(cntp::parse_tc_qdisc_show("class htb 1:1 root")) == cntp::PacingError::QdiscKindMissing);
    assert(take(cntp::parse_tc_qdisc_show("noise qdisc   mq 0: root\n")) == cntp::Qdisc::Mq);
    assert(take(cntp::parse_tc_qdisc_show("qdisc a_very_long_queue_kind 0: root")) == cntp::Qdisc::Other);
    assert(error_of(cntp::parse_tc_qdisc_show("qdisc \n")) == cntp::PacingError::QdiscKindMissing);

    crucible::test::pass("  test_qdisc_parse:              PASSED\n");
}

// A part of a qdisc dump reply: a message of a different request, the
// message of the root qdisc fq of one interface, and the end of the dump.
// The fields are the records of Pacing.h, so the part has no padding.
struct QdiscReplyPart final {
    cntp::detail::NetlinkHeader foreign_header{};
    cntp::detail::NetlinkHeader qdisc_header{};
    cntp::detail::TrafficControlMessage qdisc_message{};
    cntp::detail::RouteAttribute kind_attribute{};
    std::uint8_t kind_first = 'f';
    std::uint8_t kind_second = 'q';
    std::uint8_t kind_zero = 0;
    std::uint8_t kind_pad = 0;
    cntp::detail::NetlinkHeader done_header{};
    std::int32_t done_status = 0;
};

[[nodiscard]] QdiscReplyPart qdisc_reply_part(std::int32_t interface_index) {
    constexpr std::uint32_t header_bytes = sizeof(cntp::detail::NetlinkHeader);
    constexpr std::uint32_t qdisc_bytes =
        header_bytes + sizeof(cntp::detail::TrafficControlMessage) + sizeof(cntp::detail::RouteAttribute) + 4;
    QdiscReplyPart part{};
    part.foreign_header.length = header_bytes;
    part.foreign_header.type = cntp::detail::route_new_qdisc;
    part.foreign_header.sequence = cntp::detail::qdisc_dump_sequence + 8;
    part.qdisc_header.length = qdisc_bytes;
    part.qdisc_header.type = cntp::detail::route_new_qdisc;
    part.qdisc_header.sequence = cntp::detail::qdisc_dump_sequence;
    part.qdisc_message.interface_index = interface_index;
    part.qdisc_message.parent = cntp::detail::root_parent;
    part.kind_attribute.length = sizeof(cntp::detail::RouteAttribute) + 3;
    part.kind_attribute.type = cntp::detail::qdisc_kind_attribute;
    part.done_header.length = header_bytes + sizeof(std::int32_t);
    part.done_header.type = cntp::detail::netlink_done;
    part.done_header.sequence = cntp::detail::qdisc_dump_sequence;
    return part;
}

// The scan of a dump reply finds the root qdisc of its interface, skips the
// message of a different request, and stops at the end of the dump.
void test_qdisc_reply_scan() {
    QdiscReplyPart const part = qdisc_reply_part(7);

    auto found = cntp::detail::scan_qdisc_reply(::fixy::bytes(part), 7);
    assert(found.is_some());
    assert(take(std::move(found).expect("the part names the root qdisc of interface 7")) == cntp::Qdisc::Fq);

    // The part names no qdisc of interface 8, so the scan reaches the end.
    auto ended = cntp::detail::scan_qdisc_reply(::fixy::bytes(part), 8);
    assert(error_of(std::move(ended).expect("the part holds the end of the dump"))
           == cntp::PacingError::QdiscKindMissing);

    // The message of a different request alone holds no answer.
    assert(cntp::detail::scan_qdisc_reply(::fixy::bytes(part.foreign_header), 7).is_none());

    // A message whose length passes the end of the part is refused.
    QdiscReplyPart cut = part;
    cut.qdisc_header.length = 4096;
    auto refused = cntp::detail::scan_qdisc_reply(::fixy::bytes(cut), 7);
    assert(error_of(std::move(refused).expect("the scan refuses the cut message"))
           == cntp::PacingError::NetlinkReceiveFailed);

    crucible::test::pass("  test_qdisc_reply_scan:         PASSED\n");
}

void test_mint_and_rate_surfaces() {
    cntp::NicInterfaceName const lo = take(cntp::NicInterfaceName::from("lo"));

    auto config = cntp::mint_bbr_qdisc_config<cntp::Qdisc::FqCodel>(lo);
    static_assert(std::same_as<decltype(config), cntp::DeclaredQdiscConfig>);
    assert(config.value().interface.view() == "lo");
    assert(config.value().required == cntp::Qdisc::FqCodel);
    assert(!config.value().allow_auto_config);

    assert(take(cntp::admit_pacing_rate(1000000)).value() == 1000000);
    assert(error_of(cntp::admit_pacing_rate(0)) == cntp::PacingError::InvalidPacingRate);

    crucible::test::pass("  test_mint_and_rate_surfaces:   PASSED\n");
}

void test_live_socket_pacing_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());

    cntp::SocketFd const fd = cntp::admit_socket_fd(socket.raw()).expect("an open socket has a descriptor");

    cntp::PositivePacingRate const rate = take(cntp::admit_pacing_rate(1000000));

    // The rate is a socket option, and the test runner context carries the
    // IO and Block that the socket option gate asks for.
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    assert(cntp::set_socket_pacing_rate(test_ctx, fd, rate).is_ok());

    crucible::test::pass("  test_live_socket_pacing_if_available: PASSED\n");
}

void test_live_loopback_qdisc_query_if_available() {
    unsigned const kernel_index = ::if_nametoindex("lo");
    if (kernel_index == 0) {
        ::fixy::report(::fixy::Sink::Out, "  test_live_loopback_qdisc_query_if_available: SKIPPED\n");
        return;
    }

    // The index from sysfs is the index that the kernel gives for the name.
    cntp::NicInterfaceName const lo = take(cntp::NicInterfaceName::from("lo"));
    assert(take(cntp::detail::read_interface_index(lo)) == static_cast<std::int32_t>(kernel_index));
    // The socket door wants IO and Block, and the test runner context
    // carries both.
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto queried = cntp::query_active_qdisc(test_ctx, lo);
    if (queried.is_err()) {
        cntp::PacingError const refusal = error_of(std::move(queried));
        assert(refusal == cntp::PacingError::QdiscKindMissing || refusal == cntp::PacingError::NetlinkOpenFailed
               || refusal == cntp::PacingError::NetlinkSendFailed
               || refusal == cntp::PacingError::NetlinkReceiveFailed);
        ::fixy::report(::fixy::Sink::Out, "  test_live_loopback_qdisc_query_if_available: SKIPPED\n");
        return;
    }

    cntp::Qdisc const active = take(std::move(queried));
    assert(active == cntp::Qdisc::Fq || active == cntp::Qdisc::FqCodel || active == cntp::Qdisc::Pfifo
           || active == cntp::Qdisc::Mq || active == cntp::Qdisc::Noqueue || active == cntp::Qdisc::Other);
    crucible::test::pass("  test_live_loopback_qdisc_query_if_available: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositivePacingRate) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredQdiscConfig) == sizeof(cntp::QdiscConfig));
    static_assert(cntp::BbrCompatibleQdisc<cntp::Qdisc::Fq>);
    static_assert(cntp::BbrCompatibleQdisc<cntp::Qdisc::FqCodel>);
    static_assert(!cntp::BbrCompatibleQdisc<cntp::Qdisc::Pfifo>);
    static_assert(std::is_trivially_copy_constructible_v<cntp::NicInterfaceName>
                  && std::is_trivially_destructible_v<cntp::NicInterfaceName>);
    static_assert(std::is_trivially_copy_constructible_v<cntp::QdiscConfig>
                  && std::is_trivially_destructible_v<cntp::QdiscConfig>);
    static_assert(!::fixy::net::CtxFitsSocketMint<::fixy::ColdInitCtx, ::fixy::net::socket_kind::NetlinkRoute>,
                  "a context without Block must not open the netlink socket the qdisc query needs");
    static_assert(!cntp::CtxFitsSocketOption<::fixy::ColdInitCtx>,
                  "a context without Block must not set the pacing rate: the call takes the socket lock");

    // The length and the bytes are unreachable from outside, the type is
    // not an aggregate, and there is no constructor that takes a length,
    // so from() is the only way a length is ever set.
    static_assert(!HasWritableLengthMember<cntp::NicInterfaceName>);
    static_assert(!HasReadableByteMember<cntp::NicInterfaceName>);
    static_assert(!std::is_aggregate_v<cntp::NicInterfaceName>);
    static_assert(!std::is_constructible_v<cntp::NicInterfaceName, std::array<char, cntp::NicInterfaceName::max_bytes>,
                                           std::uint8_t>);
    // Default construction stays available and yields the empty name,
    // which every consumer treats as "no interface selected".
    static_assert(std::is_nothrow_default_constructible_v<cntp::NicInterfaceName>);
    static_assert([] {
        cntp::NicInterfaceName const empty{};
        return empty.view().size() == 0;
    }());
    // The guarantee every consumer relies on when it copies view() into a
    // fixed-size kernel field.
    static_assert([] {
        cntp::NicInterfaceName const eth0 = cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
        return eth0.view().size() < cntp::NicInterfaceName::max_bytes;
    }());
    static_assert(std::same_as<cntp::DeclaredQdiscConfig::tag_type, ::fixy::tags::source::QdiscConfig>);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_pacing:\n");
    test_interface_name_admission();
    test_interface_path();
    test_qdisc_parse();
    test_qdisc_reply_scan();
    test_mint_and_rate_surfaces();
    test_live_socket_pacing_if_available();
    test_live_loopback_qdisc_query_if_available();
    crucible::test::pass("test_cntp_pacing: all PASSED\n");
    return 0;
}
