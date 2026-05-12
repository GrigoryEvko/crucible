#include <crucible/cntp/Pacing.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;
namespace saf = crucible::safety;

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

    std::printf("  test_interface_name_admission: PASSED\n");
}

void test_qdisc_parse() {
    auto fq = cntp::qdisc_from_kernel_name("fq");
    assert(fq.has_value());
    assert(*fq == cntp::Qdisc::Fq);
    assert(cntp::qdisc_name(*fq) == std::string_view{"fq"});

    auto fq_codel = cntp::qdisc_from_kernel_name("fq_codel");
    assert(fq_codel.has_value());
    assert(*fq_codel == cntp::Qdisc::FqCodel);

    auto pfifo = cntp::qdisc_from_kernel_name("pfifo_fast");
    assert(pfifo.has_value());
    assert(*pfifo == cntp::Qdisc::Pfifo);

    auto mq = cntp::qdisc_from_kernel_name("mq");
    assert(mq.has_value());
    assert(*mq == cntp::Qdisc::Mq);

    auto noqueue = cntp::qdisc_from_kernel_name("noqueue");
    assert(noqueue.has_value());
    assert(*noqueue == cntp::Qdisc::Noqueue);

    auto other = cntp::qdisc_from_kernel_name("cake");
    assert(other.has_value());
    assert(*other == cntp::Qdisc::Other);

    auto empty = cntp::qdisc_from_kernel_name("");
    assert(!empty.has_value());
    assert(empty.error() == cntp::PacingError::QdiscKindMissing);

    auto tc_fq = cntp::parse_tc_qdisc_show(
        "qdisc fq 8001: root refcnt 2 limit 10000p flow_limit 100p\n");
    assert(tc_fq.has_value());
    assert(*tc_fq == cntp::Qdisc::Fq);

    auto tc_codel = cntp::parse_tc_qdisc_show("qdisc fq_codel 0: root\n");
    assert(tc_codel.has_value());
    assert(*tc_codel == cntp::Qdisc::FqCodel);

    auto missing = cntp::parse_tc_qdisc_show("class htb 1:1 root");
    assert(!missing.has_value());
    assert(missing.error() == cntp::PacingError::QdiscKindMissing);

    std::printf("  test_qdisc_parse:              PASSED\n");
}

void test_mint_and_rate_surfaces() {
    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());

    auto config = cntp::mint_bbr_qdisc_config<cntp::Qdisc::FqCodel>(*lo);
    static_assert(std::same_as<decltype(config), cntp::DeclaredQdiscConfig>);
    assert(config.value().interface.view() == "lo");
    assert(config.value().required == cntp::Qdisc::FqCodel);
    assert(!config.value().allow_auto_config);

    auto rate = cntp::admit_pacing_rate(1'000'000);
    assert(rate.has_value());
    assert(rate->value() == 1'000'000);

    auto zero = cntp::admit_pacing_rate(0);
    assert(!zero.has_value());
    assert(zero.error() == cntp::PacingError::InvalidPacingRate);

    std::printf("  test_mint_and_rate_surfaces:   PASSED\n");
}

void test_live_socket_pacing_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());

    auto fd = cntp::admit_socket_fd(socket.raw());
    assert(fd.has_value());

    auto rate = cntp::admit_pacing_rate(1'000'000);
    assert(rate.has_value());

    auto set = cntp::set_socket_pacing_rate(*fd, *rate);
    assert(set.has_value());

    std::printf("  test_live_socket_pacing_if_available: PASSED\n");
}

void test_live_loopback_qdisc_query_if_available() {
    if (::if_nametoindex("lo") == 0) {
        std::printf("  test_live_loopback_qdisc_query_if_available: SKIPPED\n");
        return;
    }

    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());
    auto queried = cntp::query_active_qdisc(*lo);
    if (!queried.has_value()) {
        assert(queried.error() == cntp::PacingError::QdiscKindMissing ||
               queried.error() == cntp::PacingError::NetlinkOpenFailed ||
               queried.error() == cntp::PacingError::NetlinkSendFailed ||
               queried.error() == cntp::PacingError::NetlinkReceiveFailed);
        std::printf("  test_live_loopback_qdisc_query_if_available: SKIPPED\n");
        return;
    }

    assert(*queried == cntp::Qdisc::Fq ||
           *queried == cntp::Qdisc::FqCodel ||
           *queried == cntp::Qdisc::Pfifo ||
           *queried == cntp::Qdisc::Mq ||
           *queried == cntp::Qdisc::Noqueue ||
           *queried == cntp::Qdisc::Other);
    std::printf("  test_live_loopback_qdisc_query_if_available: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositivePacingRate) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredQdiscConfig) == sizeof(cntp::QdiscConfig));
    static_assert(cntp::BbrCompatibleQdisc<cntp::Qdisc::Fq>);
    static_assert(cntp::BbrCompatibleQdisc<cntp::Qdisc::FqCodel>);
    static_assert(!cntp::BbrCompatibleQdisc<cntp::Qdisc::Pfifo>);
    static_assert(std::is_trivially_copyable_v<cntp::NicInterfaceName>);
    static_assert(std::is_trivially_copyable_v<cntp::QdiscConfig>);
    static_assert(std::same_as<
                  cntp::DeclaredQdiscConfig::tag_type,
                  saf::source::QdiscConfig>);

    std::printf("test_cntp_pacing:\n");
    test_interface_name_admission();
    test_qdisc_parse();
    test_mint_and_rate_surfaces();
    test_live_socket_pacing_if_available();
    test_live_loopback_qdisc_query_if_available();
    std::printf("test_cntp_pacing: all PASSED\n");
    return 0;
}
