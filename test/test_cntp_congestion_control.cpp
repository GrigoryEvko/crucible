#include <crucible/cntp/CongestionControl.h>
#include <crucible/effects/ExecCtx.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace cntp = crucible::cntp;
namespace saf = crucible::safety;
namespace eff = crucible::effects;

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
    static consteval std::string_view congestion_control_name() noexcept {
        return "user_cc";
    }
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

    std::printf("  test_name_admission:                     PASSED\n");
}

void test_availability_parse_and_recommendation() {
    // fixy-A5-018: upstream Linux exposes BBRv1 as "bbr"; "bbr3" is the
    // out-of-tree variant.  Parsing must keep them distinct.
    auto parsed = cntp::parse_available_congestion_control(
        "reno cubic bbr dctcp vegas\n");
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

    auto fabric =
        cntp::recommend_cc<cntp::LinkClass::LosslessDatacenterFabric>(*parsed);
    assert(fabric.has_value());
    assert(fabric->value().algorithm == cntp::CcAlgorithm::Dctcp);

    auto bbr3_only = cntp::parse_available_congestion_control(
        "reno cubic bbr3\n");
    assert(bbr3_only.has_value());
    assert(bbr3_only->contains(cntp::CcAlgorithm::Bbr3));
    assert(!bbr3_only->contains(cntp::CcAlgorithm::Bbr1));
    auto bbr3_cross =
        cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*bbr3_only);
    assert(bbr3_cross.has_value());
    assert(bbr3_cross->value().algorithm == cntp::CcAlgorithm::Bbr3);
    assert(bbr3_cross->value().kernel_name.view() == "bbr3");

    auto bbr2_only = cntp::parse_available_congestion_control(
        "reno cubic bbr2\n");
    assert(bbr2_only.has_value());
    auto bbr2_cross =
        cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*bbr2_only);
    assert(bbr2_cross.has_value());
    assert(bbr2_cross->value().algorithm == cntp::CcAlgorithm::Bbr2);

    auto legacy = cntp::parse_available_congestion_control("reno cubic\n");
    assert(legacy.has_value());
    auto fallback =
        cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*legacy);
    assert(fallback.has_value());
    assert(fallback->value().algorithm == cntp::CcAlgorithm::Cubic);

    std::printf("  test_availability_parse_and_recommendation: PASSED\n");
}

void test_mint_surfaces() {
    auto cubic =
        cntp::mint_cc_choice<cntp::CcAlgorithm::Cubic,
                             cntp::LinkClass::CrossDatacenter>();
    static_assert(std::same_as<decltype(cubic), cntp::DeclaredCcChoice>);
    assert(cubic.value().algorithm == cntp::CcAlgorithm::Cubic);
    assert(cubic.value().kernel_name.view() == "cubic");

    auto custom =
        cntp::mint_custom_cc_choice<UserCc, cntp::LinkClass::PublicInternet>();
    assert(custom.value().algorithm == cntp::CcAlgorithm::Custom);
    assert(custom.value().kernel_name.view() == "user_cc");

    std::printf("  test_mint_surfaces:                       PASSED\n");
}

void test_live_socket_roundtrip_if_available() {
    TestSocket socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    assert(socket.valid());

    auto fd = cntp::admit_socket_fd(socket.raw());
    assert(fd.has_value());

    auto availability = cntp::read_available_congestion_control();
    if (!availability.has_value()) {
        std::printf("  test_live_socket_roundtrip_if_available: SKIPPED\n");
        return;
    }

    auto choice =
        cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*availability);
    if (!choice.has_value()) {
        std::printf("  test_live_socket_roundtrip_if_available: SKIPPED\n");
        return;
    }

    auto set = cntp::set_cc_for_socket(*fd, *choice);
    assert(set.has_value());

    auto queried = cntp::query_cc_for_socket(*fd);
    assert(queried.has_value());
    // fixy-A5-018: kernel echoes the name we set; the reverse map is
    // now bijective per BBR variant ("bbr" → Bbr1, "bbr3" → Bbr3), so
    // the OR-clause that masked the misread is gone.
    assert(*queried == choice->value().algorithm);

    auto selection = cntp::query_cc_selection_for_socket(*fd);
    assert(selection.has_value());
    assert(!selection->kernel_name.view().empty());

    std::printf("  test_live_socket_roundtrip_if_available: PASSED\n");
}

// fixy-A5-016 HS14 fixture #1: positive runtime witness that the
// Ctx-gated overload of set_cc_for_socket dispatches correctly when
// the caller's ExecCtx carries Effect::IO in its row.  We can't make
// a real setsockopt() succeed without an actual TCP socket, but we
// can prove the overload IS selectable + IS callable + agrees with
// the unparameterized form on error returns (both return
// SetSockOptFailed on an unsupported fd, same path).
void test_io_gated_overload_dispatches() {
    // Closed fd; admit_socket_fd checks for >= 0 so any non-negative
    // value passes admission; the syscall then fails downstream.
    // What we're proving here is that the Ctx-gated overload reaches
    // the syscall site, not that the syscall succeeds.
    auto fd = cntp::admit_socket_fd(/*invalid*/ 0xFFFE);
    assert(fd.has_value());

    auto choice = cntp::DeclaredCcChoice{cntp::CcSelection{
        .algorithm = cntp::CcAlgorithm::Cubic,
        .kernel_name = cntp::KernelCcName::from("cubic").value(),
    }};

    // Gated form: same expected<void, CcError> shape.  BgDrainCtx has
    // row = Row<Bg, Alloc> — no IO!  So this Ctx CANNOT reach the
    // gate; ColdInitCtx has Row<Init, Alloc, IO> and IS accepted.
    eff::ColdInitCtx init{};
    auto gated = cntp::set_cc_for_socket(init, *fd, choice);
    auto bare  = cntp::set_cc_for_socket(*fd, choice);
    // Either both succeed (impossible on closed fd) or both produce
    // the same error code — the two overloads agree.
    assert(gated.has_value() == bare.has_value());
    if (!gated.has_value()) {
        assert(gated.error() == bare.error());
    }

    std::printf("  test_io_gated_overload_dispatches:  PASSED\n");
}

}  // namespace

// fixy-A5-016 HS14 fixture #2: compile-time negative-witness that the
// Ctx-gated overload REJECTS a Ctx whose row lacks Effect::IO.
//
// HotFgCtx::row_type == Row<>   — NO IO atom — must reject
// BgDrainCtx::row_type == Row<Bg, Alloc> — NO IO atom — must reject
// ColdInitCtx::row_type == Row<Init, Alloc, IO> — IO present — accepts
// BgCompileCtx::row_type == Row<Bg, Alloc, IO> — IO present — accepts
//
// We verify via the concept directly (the GCC 16 limitation noted in
// test_effects.cpp test_variadic_row_membership_lifts applies here
// too — `requires(c) { fn(c); }` inside a static_assert produces a
// hard error rather than SFINAE).
static_assert( eff::CtxOwnsCapability<eff::ColdInitCtx,  eff::Effect::IO>,
    "fixy-A5-016: ColdInitCtx::row = Row<Init, Alloc, IO> — IO must be present");
static_assert( eff::CtxOwnsCapability<eff::BgCompileCtx, eff::Effect::IO>,
    "fixy-A5-016: BgCompileCtx::row = Row<Bg, Alloc, IO> — IO must be present");
static_assert(!eff::CtxOwnsCapability<eff::HotFgCtx,     eff::Effect::IO>,
    "fixy-A5-016: HotFgCtx::row = Row<> — IO MUST be absent (this is "
    "the whole point of the gate: hot-path code cannot reach the "
    "setsockopt syscall)");
static_assert(!eff::CtxOwnsCapability<eff::BgDrainCtx,   eff::Effect::IO>,
    "fixy-A5-016: BgDrainCtx::row = Row<Bg, Alloc> — IO MUST be absent "
    "(bg-drain context does not authorize IO without explicit promotion)");

int main() {
    static_assert(sizeof(cntp::SocketFd) == sizeof(int));
    static_assert(sizeof(cntp::DeclaredCcChoice) == sizeof(cntp::CcSelection));
    static_assert(cntp::CcCompatible<
                  cntp::CcAlgorithm::Dctcp,
                  cntp::LinkClass::LosslessDatacenterFabric>);
    static_assert(!cntp::CcCompatible<
                  cntp::CcAlgorithm::Dctcp,
                  cntp::LinkClass::CrossDatacenter>);
    static_assert(cntp::CustomCcModule<UserCc>);
    static_assert(std::is_trivially_copyable_v<cntp::KernelCcName>);
    static_assert(std::is_trivially_copyable_v<cntp::CcSelection>);
    static_assert(std::same_as<
                  cntp::DeclaredCcChoice::tag_type,
                  saf::source::CcAlgorithm>);

    assert(cntp::cc_algorithm_name(cntp::CcAlgorithm::Bbr3) ==
           std::string_view{"bbr3"});
    assert(cntp::link_class_name(cntp::LinkClass::PublicInternet) ==
           std::string_view{"public-internet"});

    std::printf("test_cntp_congestion_control:\n");
    test_name_admission();
    test_availability_parse_and_recommendation();
    test_mint_surfaces();
    test_io_gated_overload_dispatches();
    test_live_socket_roundtrip_if_available();
    std::printf("test_cntp_congestion_control: all PASSED\n");
    return 0;
}
