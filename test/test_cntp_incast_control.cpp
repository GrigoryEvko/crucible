#include <crucible/cntp/IncastControl.h>
#include <crucible/rt/IncastControl.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;
namespace effects = crucible::effects;
namespace rt = crucible::rt;
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

void test_admission_and_names() {
    assert(cntp::incast_error_name(cntp::IncastError::CreditTimeout) ==
           std::string_view{"CreditTimeout"});

    auto credit = cntp::admit_credit_bytes(64 * 1024);
    assert(credit.has_value());
    assert(credit->value() == 64 * 1024);

    auto zero_credit = cntp::admit_credit_bytes(0);
    assert(!zero_credit.has_value());
    assert(zero_credit.error() == cntp::IncastError::InvalidCreditBytes);

    auto rto = cntp::admit_rto_min_usec(10'000);
    assert(rto.has_value());
    assert(rto->value() == 10'000);

    auto zero_rto = cntp::admit_rto_min_usec(0);
    assert(!zero_rto.has_value());
    assert(zero_rto.error() == cntp::IncastError::InvalidRtoMin);

    auto senders = cntp::admit_sender_count(64);
    assert(senders.has_value());
    assert(senders->value() == 64);

    auto zero_senders = cntp::admit_sender_count(0);
    assert(!zero_senders.has_value());
    assert(zero_senders.error() == cntp::IncastError::InvalidSenderCount);

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_config_minting() {
    auto credit = cntp::admit_credit_bytes(32 * 1024);
    auto rto = cntp::admit_rto_min_usec(10'000);
    auto senders = cntp::admit_sender_count(32);
    assert(credit.has_value());
    assert(rto.has_value());
    assert(senders.has_value());

    auto config =
        cntp::mint_dctcp_incast_config<cntp::LinkClass::LosslessDatacenterFabric>(
            *credit, *rto, *senders);
    static_assert(std::same_as<decltype(config), cntp::DeclaredIncastConfig>);
    assert(config.value().enable_dctcp);
    assert(config.value().enable_ecn);
    assert(config.value().enable_credit_pacing);
    assert(config.value().initial_credit_bytes.value() == 32 * 1024);
    assert(config.value().expected_senders.value() == 32);

    auto manual = cntp::mint_incast_config(cntp::IncastConfig{
        .enable_dctcp = false,
        .enable_ecn = false,
        .rto_min_usec = *rto,
        .enable_credit_pacing = true,
        .initial_credit_bytes = *credit,
        .expected_senders = *senders,
    });
    assert(manual.has_value());
    assert(!manual->value().enable_dctcp);

    std::printf("  test_config_minting:      PASSED\n");
}

void test_credit_pacing_state() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto controller = rt::mint_incast_controller<2>(init);

    auto fd0 = cntp::admit_socket_fd(10);
    auto fd1 = cntp::admit_socket_fd(11);
    auto fd2 = cntp::admit_socket_fd(12);
    auto initial = cntp::admit_credit_bytes(4096);
    auto extra = cntp::admit_credit_bytes(1024);
    auto timeout = cntp::admit_rto_min_usec(1000);
    assert(fd0.has_value());
    assert(fd1.has_value());
    assert(fd2.has_value());
    assert(initial.has_value());
    assert(extra.has_value());
    assert(timeout.has_value());

    auto started = controller.start_credit_flow(init, *fd0, *initial);
    assert(started.has_value());
    auto outstanding = controller.outstanding_credit(*fd0);
    assert(outstanding.has_value());
    assert(outstanding->value() == 4096);

    auto grant = controller.issue_credit(bg, *fd0, *extra, 7);
    assert(grant.has_value());
    assert(grant->fd.value() == fd0->value());
    assert(grant->bytes.value() == extra->value());
    assert(grant->sequence == 7);

    auto received = controller.await_credit(bg, *fd0, *timeout);
    assert(received.has_value());
    assert(received->value() == 5120);
    auto empty = controller.await_credit(bg, *fd0, *timeout);
    assert(!empty.has_value());
    assert(empty.error() == cntp::IncastError::CreditTimeout);

    assert(controller.start_credit_flow(init, *fd1, *initial).has_value());
    auto overflow = controller.start_credit_flow(init, *fd2, *initial);
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::IncastError::TooManyFlows);

    std::printf("  test_credit_pacing_state: PASSED\n");
}

void test_live_rto_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());

    auto fd = cntp::admit_socket_fd(socket.raw());
    auto rto = cntp::admit_rto_min_usec(10'000);
    assert(fd.has_value());
    assert(rto.has_value());

    auto set = cntp::set_socket_rto_min_usec(*fd, *rto);
    if (!set.has_value()) {
        assert(set.error() == cntp::IncastError::SetRtoMinFailed ||
               set.error() == cntp::IncastError::UnsupportedRtoMinSockOpt);
        std::printf("  test_live_rto_if_available: SKIPPED\n");
        return;
    }

    std::printf("  test_live_rto_if_available: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositiveCreditBytes) == sizeof(std::uint32_t));
    static_assert(sizeof(cntp::PositiveRtoMinUsec) == sizeof(std::uint32_t));
    static_assert(sizeof(cntp::DeclaredIncastConfig) == sizeof(cntp::IncastConfig));
    static_assert(std::is_trivially_copyable_v<cntp::IncastConfig>);
    static_assert(std::same_as<
                  cntp::DeclaredIncastConfig::tag_type,
                  saf::source::IncastConfig>);
    static_assert(rt::CtxFitsIncastConfigure<effects::ColdInitCtx>);
    static_assert(rt::CtxFitsIncastConfigure<effects::BgDrainCtx>);
    static_assert(!rt::CtxFitsIncastConfigure<effects::HotFgCtx>);
    static_assert(rt::CtxFitsIncastCredit<effects::BgDrainCtx>);
    static_assert(!rt::CtxFitsIncastCredit<effects::HotFgCtx>);

    std::printf("test_cntp_incast_control:\n");
    test_admission_and_names();
    test_config_minting();
    test_credit_pacing_state();
    test_live_rto_if_available();
    std::printf("test_cntp_incast_control: all PASSED\n");
    return 0;
}
