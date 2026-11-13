#include <crucible/cntp/IncastControl.h>
#include <crucible/cntp/IncastControlRuntime.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include "test_assert.h"
#include <cstdio>
#include <string_view>
#include <type_traits>

#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;

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

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, cntp::IncastError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.  A call that succeeds ends
// the process.
template <class Value>
cntp::IncastError error_of(::fixy::Result<Value, cntp::IncastError> result) {
    return std::move(result).err().expect("the call fails in this test");
}

void test_admission_and_names() {
    assert(cntp::incast_error_name(cntp::IncastError::CreditUnavailable) == "CreditUnavailable");

    assert(take(cntp::admit_credit_bytes(64 * 1024)).value() == 64 * 1024);
    assert(error_of(cntp::admit_credit_bytes(0)) == cntp::IncastError::InvalidCreditBytes);

    assert(take(cntp::admit_rto_min_usec(10000)).value() == 10000);
    assert(error_of(cntp::admit_rto_min_usec(0)) == cntp::IncastError::InvalidRtoMin);

    assert(take(cntp::admit_sender_count(64)).value() == 64);
    assert(error_of(cntp::admit_sender_count(0)) == cntp::IncastError::InvalidSenderCount);

    crucible::test::pass("  test_admission_and_names: PASSED\n");
}

void test_config_minting() {
    cntp::PositiveCreditBytes const credit = take(cntp::admit_credit_bytes(32 * 1024));
    cntp::PositiveRtoMinUsec const rto = take(cntp::admit_rto_min_usec(10000));
    cntp::PositiveSenderCount const senders = take(cntp::admit_sender_count(32));

    auto config = cntp::mint_dctcp_incast_config<cntp::LinkClass::LosslessDatacenterFabric>(credit, rto, senders);
    static_assert(std::same_as<decltype(config), cntp::DeclaredIncastConfig>);
    assert(config.value().enable_dctcp);
    assert(config.value().enable_ecn);
    assert(config.value().enable_credit_pacing);
    assert(config.value().initial_credit_bytes.value() == 32 * 1024);
    assert(config.value().expected_senders.value() == 32);

    auto manual = cntp::mint_incast_config(cntp::IncastConfig{
        .enable_dctcp = false,
        .enable_ecn = false,
        .rto_min_usec = rto,
        .enable_credit_pacing = true,
        .initial_credit_bytes = credit,
        .expected_senders = senders,
    });
    assert(!take(std::move(manual)).value().enable_dctcp);

    crucible::test::pass("  test_config_minting:      PASSED\n");
}

void test_credit_pacing_state() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_incast_controller<2>(init);

    auto fd0 = cntp::admit_socket_fd(10);
    auto fd1 = cntp::admit_socket_fd(11);
    auto fd2 = cntp::admit_socket_fd(12);
    cntp::PositiveCreditBytes const initial = take(cntp::admit_credit_bytes(4096));
    cntp::PositiveCreditBytes const extra = take(cntp::admit_credit_bytes(1024));
    assert(fd0.has_value());
    assert(fd1.has_value());
    assert(fd2.has_value());

    assert(controller.start_credit_flow(init, *fd0, initial).is_ok());
    assert(take(controller.outstanding_credit(*fd0)).value() == 4096);

    cntp::IncastCreditGrant const grant = take(controller.issue_credit(bg, *fd0, extra, 7));
    assert(grant.fd.value() == fd0->value());
    assert(grant.bytes.value() == extra.value());
    assert(grant.sequence == 7);

    // The consume call polls once and never blocks.  The first drains the
    // whole balance, 4096 started plus 1024 issued, and the second finds
    // nothing left because no further credit was granted between them.
    assert(take(controller.try_consume_credit(bg, *fd0)).value() == 5120);
    assert(error_of(controller.try_consume_credit(bg, *fd0)) == cntp::IncastError::CreditUnavailable);

    // The signature must carry no timeout parameter.  A parameter that is
    // accepted and then ignored reads as a working timeout to every
    // caller.  Naming the exact member type is what pins it.
    using ConsumeFn = ::fixy::Result<cntp::PositiveCreditBytes, cntp::IncastError> (cntp::IncastController<2>::*)(
        ::fixy::BgDrainCtx const&, cntp::SocketFd) noexcept;
    static_cast<void>(static_cast<ConsumeFn>(&cntp::IncastController<2>::try_consume_credit<::fixy::BgDrainCtx>));

    assert(controller.start_credit_flow(init, *fd1, initial).is_ok());
    assert(error_of(controller.start_credit_flow(init, *fd2, initial)) == cntp::IncastError::TooManyFlows);

    crucible::test::pass("  test_credit_pacing_state: PASSED\n");
}

void test_live_rto_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());

    auto fd = cntp::admit_socket_fd(socket.raw());
    cntp::PositiveRtoMinUsec const rto = take(cntp::admit_rto_min_usec(10000));
    assert(fd.has_value());

    // The timeout is a socket option, and the test runner context carries
    // the IO and Block that the socket option gate asks for.
    ::fixy::TestRunnerCtx test_ctx{fe::testing::test()};
    auto set = cntp::set_socket_rto_min_usec(test_ctx, *fd, rto);
    if (set.is_err()) {
        cntp::IncastError const refusal = error_of(std::move(set));
        assert(refusal == cntp::IncastError::SetRtoMinFailed || refusal == cntp::IncastError::UnsupportedRtoMinSockOpt);
        ::fixy::report(::fixy::Sink::Out, "  test_live_rto_if_available: SKIPPED\n");
        return;
    }

    crucible::test::pass("  test_live_rto_if_available: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositiveCreditBytes) == sizeof(std::uint32_t));
    static_assert(sizeof(cntp::PositiveRtoMinUsec) == sizeof(std::uint32_t));
    static_assert(sizeof(cntp::DeclaredIncastConfig) == sizeof(cntp::IncastConfig));
    static_assert(std::is_trivially_copy_constructible_v<cntp::IncastConfig>
                  && std::is_trivially_destructible_v<cntp::IncastConfig>);
    static_assert(!std::is_trivially_copyable_v<cntp::IncastConfig>,
                  "a refined field must keep a byte copy from building a config");
    static_assert(std::same_as<cntp::DeclaredIncastConfig::tag_type, ::fixy::tags::source::IncastConfig>);
    static_assert(cntp::CtxFitsIncastConfigure<::fixy::ColdInitCtx>);
    static_assert(cntp::CtxFitsIncastConfigure<::fixy::BgDrainCtx>);
    static_assert(!cntp::CtxFitsIncastConfigure<::fixy::HotFgCtx>);
    static_assert(cntp::CtxFitsIncastCredit<::fixy::BgDrainCtx>);
    static_assert(!cntp::CtxFitsIncastCredit<::fixy::HotFgCtx>);
    static_assert(cntp::CtxFitsIncastApply<::fixy::InitLoadCtx>);
    static_assert(!cntp::CtxFitsIncastApply<::fixy::ColdInitCtx>,
                  "a context without Block can neither read the algorithm list nor set a socket option");

    ::fixy::report(::fixy::Sink::Out, "test_cntp_incast_control:\n");
    test_admission_and_names();
    test_config_minting();
    test_credit_pacing_state();
    test_live_rto_if_available();
    crucible::test::pass("test_cntp_incast_control: all PASSED\n");
    return 0;
}
