#include <crucible/cntp/CongestionControl.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

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
    auto parsed = cntp::parse_available_congestion_control(
        "reno cubic bbr dctcp vegas\n");
    assert(parsed.has_value());
    assert(parsed->contains(cntp::CcAlgorithm::Reno));
    assert(parsed->contains(cntp::CcAlgorithm::Cubic));
    assert(parsed->contains(cntp::CcAlgorithm::Bbr3));
    assert(parsed->contains(cntp::CcAlgorithm::Bbr1));
    assert(parsed->contains(cntp::CcAlgorithm::Dctcp));

    auto cross = cntp::recommend_cc<cntp::LinkClass::CrossDatacenter>(*parsed);
    assert(cross.has_value());
    assert(cross->value().algorithm == cntp::CcAlgorithm::Bbr3);
    assert(cross->value().kernel_name.view() == "bbr");

    auto fabric =
        cntp::recommend_cc<cntp::LinkClass::LosslessDatacenterFabric>(*parsed);
    assert(fabric.has_value());
    assert(fabric->value().algorithm == cntp::CcAlgorithm::Dctcp);

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
    assert(*queried == choice->value().algorithm ||
           (*queried == cntp::CcAlgorithm::Bbr3 &&
            choice->value().kernel_name.view() == "bbr"));

    auto selection = cntp::query_cc_selection_for_socket(*fd);
    assert(selection.has_value());
    assert(!selection->kernel_name.view().empty());

    std::printf("  test_live_socket_roundtrip_if_available: PASSED\n");
}

}  // namespace

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
    test_live_socket_roundtrip_if_available();
    std::printf("test_cntp_congestion_control: all PASSED\n");
    return 0;
}
