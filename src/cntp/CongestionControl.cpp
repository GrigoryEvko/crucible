#include <crucible/cntp/CongestionControl.h>

#include <crucible/handles/FileHandle.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace crucible::cntp {

namespace {

constexpr char available_cc_path[] =
    "/proc/sys/net/ipv4/tcp_available_congestion_control";

// fixy-V-235: per-TU LocalFd shim consolidated into safety::FileHandle.
using LocalFd = ::crucible::safety::FileHandle;

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

[[nodiscard]] std::size_t bounded_c_string_len(char const* data,
                                               std::size_t cap) noexcept {
    std::size_t len = 0;
    while (len < cap && data[len] != '\0') {
        ++len;
    }
    return len;
}

void add_algorithm(CcAvailability& availability,
                   CcAlgorithm algorithm) noexcept {
    // fixy-A5-018: BBR variants do NOT imply each other.  BBRv3 patches
    // typically REPLACE the in-tree "bbr" module rather than coexisting
    // — a kernel that registers "bbr3" usually does not also expose
    // "bbr" as BBRv1, and vice versa.  Each enum value reflects exactly
    // one kernel-registered name; the recommendation engine handles
    // cross-variant fallback in recommend_cc.
    availability.algorithms.set(algorithm);
}

}  // namespace

std::string_view cc_algorithm_name(CcAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case CcAlgorithm::Bbr3:   return "bbr3";
        case CcAlgorithm::Cubic:  return "cubic";
        case CcAlgorithm::Dctcp:  return "dctcp";
        case CcAlgorithm::Reno:   return "reno";
        case CcAlgorithm::Vegas:  return "vegas";
        case CcAlgorithm::Bbr2:   return "bbr2";
        case CcAlgorithm::Bbr1:   return "bbr1";
        case CcAlgorithm::Custom: return "custom";
        default:                  return "unknown";
    }
}

std::string_view link_class_name(LinkClass link) noexcept {
    switch (link) {
        case LinkClass::CrossDatacenter:           return "cross-datacenter";
        case LinkClass::LosslessDatacenterFabric:  return "lossless-datacenter-fabric";
        case LinkClass::PublicInternet:            return "public-internet";
        case LinkClass::LegacyKernel:              return "legacy-kernel";
        case LinkClass::Loopback:                  return "loopback";
        default:                                   return "unknown";
    }
}

std::expected<CcAlgorithm, CcError>
algorithm_from_kernel_name(std::string_view name) noexcept {
    // fixy-A5-018: upstream Linux registers BBRv1 as "bbr".  BBRv2 and
    // BBRv3 are out-of-tree (Google-maintained) and register under their
    // own distinct names "bbr2" and "bbr3".  Mapping "bbr" → Bbr3 was a
    // misread: a kernel that exposes plain "bbr" is running BBRv1 in the
    // overwhelming majority of production fleets.
    if (name == "bbr") {
        return CcAlgorithm::Bbr1;
    }
    if (name == "bbr2") {
        return CcAlgorithm::Bbr2;
    }
    if (name == "bbr3") {
        return CcAlgorithm::Bbr3;
    }
    if (name == "cubic") {
        return CcAlgorithm::Cubic;
    }
    if (name == "dctcp") {
        return CcAlgorithm::Dctcp;
    }
    if (name == "reno") {
        return CcAlgorithm::Reno;
    }
    if (name == "vegas") {
        return CcAlgorithm::Vegas;
    }
    if (KernelCcName::from(name).has_value()) {
        return CcAlgorithm::Custom;
    }
    return std::unexpected(CcError::InvalidAlgorithmName);
}

std::expected<CcAvailability, CcError>
parse_available_congestion_control(std::string_view text) noexcept {
    CcAvailability availability{};

    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && is_space(text[pos])) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && !is_space(text[pos])) {
            ++pos;
        }
        if (start == pos) {
            continue;
        }

        const auto token = text.substr(start, pos - start);
        auto admitted_name = KernelCcName::from(token);
        if (!admitted_name.has_value()) {
            return std::unexpected(admitted_name.error());
        }

        auto parsed = algorithm_from_kernel_name(token);
        if (!parsed.has_value()) {
            return std::unexpected(parsed.error());
        }
        if (*parsed != CcAlgorithm::Custom) {
            add_algorithm(availability, *parsed);
        }
    }

    return availability;
}

std::expected<CcAvailability, CcError>
read_available_congestion_control() noexcept {
    LocalFd fd{::open(available_cc_path, O_RDONLY | O_CLOEXEC)};
    if (!fd.is_open()) {
        return std::unexpected(CcError::SysctlUnavailable);
    }

    std::array<char, 512> buffer{};
    const auto nread = ::read(fd.get(), buffer.data(), buffer.size() - 1);
    if (nread <= 0) {
        return std::unexpected(CcError::SysctlUnavailable);
    }
    return parse_available_congestion_control(
        std::string_view{buffer.data(), static_cast<std::size_t>(nread)});
}

bool kernel_supports(CcAlgorithm algorithm) noexcept {
    auto availability = read_available_congestion_control();
    return availability.has_value() && availability->contains(algorithm);
}

std::expected<void, CcError>
set_cc_for_socket(SocketFd fd, DeclaredCcChoice choice) noexcept {
    auto const& selection = choice.value();
    auto const name = selection.kernel_name.view();
    std::array<char, KernelCcName::max_bytes> optname{};
    std::memcpy(optname.data(), name.data(), name.size());

    const int rc = ::setsockopt(
        fd.value(),
        IPPROTO_TCP,
        TCP_CONGESTION,
        optname.data(),
        static_cast<socklen_t>(name.size() + 1u));
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(CcError::SetSockOptFailed);
    }
    return {};
}

std::expected<CcAlgorithm, CcError>
query_cc_for_socket(SocketFd fd) noexcept {
    auto selection = query_cc_selection_for_socket(fd);
    if (!selection.has_value()) {
        return std::unexpected(selection.error());
    }
    return selection->algorithm;
}

std::expected<CcSelection, CcError>
query_cc_selection_for_socket(SocketFd fd) noexcept {
    std::array<char, KernelCcName::max_bytes> optname{};
    socklen_t len = static_cast<socklen_t>(optname.size());
    const int rc = ::getsockopt(
        fd.value(),
        IPPROTO_TCP,
        TCP_CONGESTION,
        optname.data(),
        &len);
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(CcError::GetSockOptFailed);
    }

    const auto bounded_len = static_cast<std::size_t>(len) < optname.size()
        ? static_cast<std::size_t>(len)
        : optname.size();
    const auto actual_len = bounded_c_string_len(optname.data(), bounded_len);
    auto admitted = KernelCcName::from(
        std::string_view{optname.data(), actual_len});
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }

    auto algorithm = algorithm_from_kernel_name(admitted->view());
    if (!algorithm.has_value()) {
        return std::unexpected(algorithm.error());
    }

    return CcSelection{
        .algorithm = *algorithm,
        .kernel_name = *admitted,
    };
}

}  // namespace crucible::cntp
