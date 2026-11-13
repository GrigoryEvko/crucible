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

void test_interface_name_admission() {
    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());
    assert(lo->view() == "lo");

    auto colon = cntp::NicInterfaceName::from("enp1s0f0:1");
    assert(colon.has_value());
    assert(colon->view() == "enp1s0f0:1");

    auto empty = cntp::NicInterfaceName::from("");
    assert(!empty.has_value());
    assert(empty.error() == cntp::PacingError::InvalidInterfaceName);

    auto slash = cntp::NicInterfaceName::from("../eth0");
    assert(!slash.has_value());
    assert(slash.error() == cntp::PacingError::InvalidInterfaceName);

    auto too_long = cntp::NicInterfaceName::from("0123456789abcdef");
    assert(!too_long.has_value());
    assert(too_long.error() == cntp::PacingError::InvalidInterfaceName);

    crucible::test::pass("  test_interface_name_admission: PASSED\n");
}

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

    crucible::test::pass("  test_qdisc_parse:              PASSED\n");
}

void test_mint_and_rate_surfaces() {
    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());

    auto config = cntp::mint_bbr_qdisc_config<cntp::Qdisc::FqCodel>(*lo);
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

    auto fd = cntp::admit_socket_fd(socket.raw());
    assert(fd.has_value());

    cntp::PositivePacingRate const rate = take(cntp::admit_pacing_rate(1000000));

    // The rate is a socket option, and the test runner context carries the
    // IO and Block that the socket option gate asks for.
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    assert(cntp::set_socket_pacing_rate(test_ctx, *fd, rate).is_ok());

    crucible::test::pass("  test_live_socket_pacing_if_available: PASSED\n");
}

void test_live_loopback_qdisc_query_if_available() {
    if (::if_nametoindex("lo") == 0) {
        ::fixy::report(::fixy::Sink::Out, "  test_live_loopback_qdisc_query_if_available: SKIPPED\n");
        return;
    }

    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());
    // The socket door wants IO and Block, and the test runner context
    // carries both.
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto queried = cntp::query_active_qdisc(test_ctx, *lo);
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
    static_assert(std::is_trivially_copyable_v<cntp::NicInterfaceName>);
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
    static_assert(cntp::NicInterfaceName{}.view().empty());
    // The guarantee every consumer relies on when it copies view() into a
    // fixed-size kernel field.
    static_assert(cntp::NicInterfaceName::from("eth0").value().view().size() < cntp::NicInterfaceName::max_bytes);
    static_assert(std::same_as<cntp::DeclaredQdiscConfig::tag_type, ::fixy::tags::source::QdiscConfig>);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_pacing:\n");
    test_interface_name_admission();
    test_qdisc_parse();
    test_mint_and_rate_surfaces();
    test_live_socket_pacing_if_available();
    test_live_loopback_qdisc_query_if_available();
    crucible::test::pass("test_cntp_pacing: all PASSED\n");
    return 0;
}
