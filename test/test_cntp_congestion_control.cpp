#include <crucible/cntp/CongestionControl.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

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

struct UserCc {
    static consteval std::string_view congestion_control_name() noexcept { return "user_cc"; }
};

void test_name_admission() {
    auto cubic = cntp::KernelCcName::from("cubic");
    assert(cubic.has_value());
    assert(cubic->view() == "cubic");

    auto bad_space = cntp::KernelCcName::from("cu bic");
    assert(!bad_space.has_value());
    assert(bad_space.error() == cntp::CcError::InvalidAlgorithmName);

    auto bad_long = cntp::KernelCcName::from("0123456789abcdef");
    assert(!bad_long.has_value());
    assert(bad_long.error() == cntp::CcError::InvalidAlgorithmName);

    auto fd = cntp::admit_socket_fd(-1);
    assert(!fd.has_value());
    assert(fd.error() == cntp::CcError::InvalidSocketFd);

    crucible::test::pass("  test_name_admission:                     PASSED\n");
}

void test_availability_parse_and_recommendation() {
    // The kernel calls the first BBR version "bbr" outright, and
    // "bbr3" is a separate out-of-tree name.  Parsing keeps the two
    // apart rather than treating one as a prefix of the other.
    auto parsed = cntp::parse_available_congestion_control("reno cubic bbr dctcp vegas\n");
    assert(parsed.has_value());
    assert(parsed->contains(cntp::CcAlgorithm::Reno));
    assert(parsed->contains(cntp::CcAlgorithm::Cubic));
    assert(parsed->contains(cntp::CcAlgorithm::Bbr1));
    assert(!parsed->contains(cntp::CcAlgorithm::Bbr3));
    assert(!parsed->contains(cntp::CcAlgorithm::Bbr2));
    assert(parsed->contains(cntp::CcAlgorithm::Dctcp));

    auto cross = cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*parsed);
    assert(cross.has_value());
    assert(cross->value().algorithm == cntp::CcAlgorithm::Bbr1);
    assert(cross->value().kernel_name.view() == "bbr");

    auto fabric = cntp::recommend_cc<cntp::LinkClass::LosslessDatacenterFabric>(*parsed);
    assert(fabric.has_value());
    assert(fabric->value().algorithm == cntp::CcAlgorithm::Dctcp);

    auto bbr3_only = cntp::parse_available_congestion_control("reno cubic bbr3\n");
    assert(bbr3_only.has_value());
    assert(bbr3_only->contains(cntp::CcAlgorithm::Bbr3));
    assert(!bbr3_only->contains(cntp::CcAlgorithm::Bbr1));
    auto bbr3_cross = cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*bbr3_only);
    assert(bbr3_cross.has_value());
    assert(bbr3_cross->value().algorithm == cntp::CcAlgorithm::Bbr3);
    assert(bbr3_cross->value().kernel_name.view() == "bbr3");

    auto bbr2_only = cntp::parse_available_congestion_control("reno cubic bbr2\n");
    assert(bbr2_only.has_value());
    auto bbr2_cross = cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*bbr2_only);
    assert(bbr2_cross.has_value());
    assert(bbr2_cross->value().algorithm == cntp::CcAlgorithm::Bbr2);

    auto legacy = cntp::parse_available_congestion_control("reno cubic\n");
    assert(legacy.has_value());
    auto fallback = cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*legacy);
    assert(fallback.has_value());
    assert(fallback->value().algorithm == cntp::CcAlgorithm::Cubic);

    crucible::test::pass("  test_availability_parse_and_recommendation: PASSED\n");
}

void test_mint_surfaces() {
    auto cubic = cntp::mint_cc_choice<cntp::CcAlgorithm::Cubic, cntp::LinkClass::CrossDatacenter>();
    static_assert(std::same_as<decltype(cubic), cntp::DeclaredCcChoice>);
    assert(cubic.value().algorithm == cntp::CcAlgorithm::Cubic);
    assert(cubic.value().kernel_name.view() == "cubic");

    auto custom = cntp::mint_custom_cc_choice<UserCc, cntp::LinkClass::PublicInternet>();
    assert(custom.value().algorithm == cntp::CcAlgorithm::Custom);
    assert(custom.value().kernel_name.view() == "user_cc");

    crucible::test::pass("  test_mint_surfaces:                       PASSED\n");
}

void test_live_socket_roundtrip_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());

    auto fd = cntp::admit_socket_fd(socket.raw());
    assert(fd.has_value());

    ::fixy::InitLoadCtx load{fe::testing::init()};
    auto availability = cntp::read_available_congestion_control(load);
    if (!availability.has_value()) {
        ::fixy::report(::fixy::Sink::Out, "  test_live_socket_roundtrip_if_available: SKIPPED\n");
        return;
    }

    auto choice = cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*availability);
    if (!choice.has_value()) {
        ::fixy::report(::fixy::Sink::Out, "  test_live_socket_roundtrip_if_available: SKIPPED\n");
        return;
    }

    auto set = cntp::set_cc_for_socket(load, *fd, *choice);
    assert(set.has_value());

    auto queried = cntp::query_cc_for_socket(load, *fd);
    assert(queried.has_value());
    // The kernel echoes back the name it was given, and the reverse
    // mapping is one-to-one per BBR version, so the query returns the
    // algorithm that was set and not a neighbouring one.
    assert(*queried == choice->value().algorithm);

    auto selection = cntp::query_cc_selection_for_socket(load, *fd);
    assert(selection.has_value());
    assert(!selection->kernel_name.view().empty());

    crucible::test::pass("  test_live_socket_roundtrip_if_available: PASSED\n");
}

// Each gated form reaches the kernel and reports the failure of its own
// call.  Admission requires only a non-negative descriptor, so this value
// passes it, and the kernel then refuses the descriptor.
void test_gated_forms_report_kernel_refusal() {
    auto fd = cntp::admit_socket_fd(/*invalid*/ 0xFFFE);
    assert(fd.has_value());

    auto choice = cntp::mint_cc_choice<cntp::CcAlgorithm::Cubic, cntp::LinkClass::CrossDatacenter>();

    // The load context carries IO and Block, so every socket gate below
    // admits it.  The assertions at file scope show which contexts the
    // gate refuses.
    ::fixy::InitLoadCtx load{fe::testing::init()};

    auto set = cntp::set_cc_for_socket(load, *fd, choice);
    assert(!set.has_value());
    assert(set.error() == cntp::CcError::SetSockOptFailed);

    auto queried = cntp::query_cc_for_socket(load, *fd);
    assert(!queried.has_value());
    assert(queried.error() == cntp::CcError::GetSockOptFailed);

    auto selection = cntp::query_cc_selection_for_socket(load, *fd);
    assert(!selection.has_value());
    assert(selection.error() == cntp::CcError::GetSockOptFailed);

    // The list changes nothing and depends only on the running kernel, so
    // kernel_supports must answer from the same list for every algorithm.
    auto listed = cntp::read_available_congestion_control(load);
    for (auto algo :
         {cntp::CcAlgorithm::Cubic, cntp::CcAlgorithm::Reno, cntp::CcAlgorithm::Bbr1, cntp::CcAlgorithm::Bbr2,
          cntp::CcAlgorithm::Bbr3, cntp::CcAlgorithm::Dctcp, cntp::CcAlgorithm::Vegas}) {
        assert(cntp::kernel_supports(load, algo) == (listed.has_value() && listed->contains(algo)));
    }

    crucible::test::pass("  test_gated_forms_report_kernel_refusal: PASSED\n");
}

}  // namespace

// The gate is checked through the concept rather than by attempting
// the call.  A requires-expression naming the call inside a static
// assertion is a hard error under this compiler instead of a
// substitution failure, so it cannot serve as a negative witness.
static_assert(cntp::CtxFitsSocketOption<::fixy::InitLoadCtx>,
              "The startup load context carries IO and Block, so it may set a socket option.");
static_assert(cntp::CtxFitsSocketOption<::fixy::BgLoadCtx>,
              "The background load context carries IO and Block, so it may set a socket option.");
static_assert(cntp::CtxFitsSocketOption<::fixy::TestRunnerCtx>);
static_assert(!cntp::CtxFitsSocketOption<::fixy::ColdInitCtx>,
              "A cold-init context lacks Block, and a socket option call takes the socket lock.");
static_assert(!cntp::CtxFitsSocketOption<::fixy::BgCompileCtx>,
              "A background-compile context lacks Block, and a socket option call takes the socket lock.");
static_assert(!cntp::CtxFitsSocketOption<::fixy::HotFgCtx>,
              "A hot foreground context must not reach a socket option call.  That is what the gate is for.");
static_assert(!cntp::CtxFitsSocketOption<::fixy::BgDrainCtx>, "A background-drain context carries no IO.");
static_assert(::fixy::fs::CtxFitsFileMint<::fixy::InitLoadCtx, cntp::ProcFileReadMode>,
              "The startup load context carries IO and Block, so it may read the algorithm list.");
static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::ColdInitCtx, cntp::ProcFileReadMode>,
              "A cold-init context lacks Block, so the fs door refuses it the /proc read.");

int main() {
    static_assert(sizeof(cntp::SocketFd) == sizeof(int));
    static_assert(sizeof(cntp::DeclaredCcChoice) == sizeof(cntp::CcSelection));
    static_assert(cntp::CcCompatible<cntp::CcAlgorithm::Dctcp, cntp::LinkClass::LosslessDatacenterFabric>);
    static_assert(!cntp::CcCompatible<cntp::CcAlgorithm::Dctcp, cntp::LinkClass::CrossDatacenter>);
    static_assert(cntp::CustomCcModule<UserCc>);
    static_assert(std::is_trivially_copyable_v<cntp::KernelCcName>);
    static_assert(std::is_trivially_copyable_v<cntp::CcSelection>);
    static_assert(std::same_as<cntp::DeclaredCcChoice::tag_type, ::fixy::tags::source::CcAlgorithm>);

    assert(cntp::cc_algorithm_name(cntp::CcAlgorithm::Bbr3) == std::string_view{"bbr3"});
    assert(cntp::link_class_name(cntp::LinkClass::PublicInternet) == std::string_view{"public-internet"});

    ::fixy::report(::fixy::Sink::Out, "test_cntp_congestion_control:\n");
    test_name_admission();
    test_availability_parse_and_recommendation();
    test_mint_surfaces();
    test_gated_forms_report_kernel_refusal();
    test_live_socket_roundtrip_if_available();
    crucible::test::pass("test_cntp_congestion_control: all PASSED\n");
    return 0;
}
