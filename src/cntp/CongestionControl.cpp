#include <crucible/cntp/CongestionControl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace crucible::cntp {

namespace {

[[nodiscard]] bool is_space(char c) noexcept { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }

void add_algorithm(CcAvailability& availability, CcAlgorithm algorithm) noexcept {
    // Each enum value reflects exactly one kernel-registered name.  A BBRv3
    // patch set replaces the in-tree "bbr" module rather than coexisting with
    // it, so no BBR variant implies any other.
    availability.algorithms.set(algorithm);
}

// Adds the algorithm of the characters of text from start to end.  An empty
// token adds nothing, and a token that is no kernel name is an error.
[[nodiscard]] ::fixy::Result<::fixy::Unit, CcError> add_token(CcAvailability& availability, ::fixy::TextView text,
                                                              std::size_t start, std::size_t end) noexcept {
    if (start == end) {
        return ::fixy::Unit{};
    }
    ::fixy::TextView const token = text.window(start, end - start).expect("a token lies inside its text");
    CRUCIBLE_TRY(algorithm, algorithm_from_kernel_name(token));
    if (algorithm != CcAlgorithm::Custom) {
        add_algorithm(availability, algorithm);
    }
    return ::fixy::Unit{};
}

}  // namespace

::fixy::TextView cc_algorithm_name(CcAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case CcAlgorithm::Bbr3:
            return "bbr3";
        case CcAlgorithm::Cubic:
            return "cubic";
        case CcAlgorithm::Dctcp:
            return "dctcp";
        case CcAlgorithm::Reno:
            return "reno";
        case CcAlgorithm::Vegas:
            return "vegas";
        case CcAlgorithm::Bbr2:
            return "bbr2";
        case CcAlgorithm::Bbr1:
            return "bbr1";
        case CcAlgorithm::Custom:
            return "custom";
        default:
            return "unknown";
    }
}

::fixy::TextView link_class_name(LinkClass link) noexcept {
    switch (link) {
        case LinkClass::CrossDatacenter:
            return "cross-datacenter";
        case LinkClass::LosslessDatacenterFabric:
            return "lossless-datacenter-fabric";
        case LinkClass::PublicInternet:
            return "public-internet";
        case LinkClass::LegacyKernel:
            return "legacy-kernel";
        case LinkClass::Loopback:
            return "loopback";
        default:
            return "unknown";
    }
}

::fixy::Result<CcAlgorithm, CcError> algorithm_from_kernel_name(::fixy::TextView name) noexcept {
    // Upstream Linux registers BBRv1 under the plain name "bbr".  BBRv2 and
    // BBRv3 are out-of-tree and register under their own distinct names
    // "bbr2" and "bbr3".  A kernel that exposes "bbr" is running BBRv1.
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
    if (KernelCcName::from(name).is_ok()) {
        return CcAlgorithm::Custom;
    }
    return ::fixy::err(CcError::InvalidAlgorithmName);
}

// The tokens of the list stand between the spaces.  One pass over the
// characters.
::fixy::Result<CcAvailability, CcError> parse_available_congestion_control(::fixy::TextView text) noexcept {
    CcAvailability availability{};

    std::size_t start = 0;
    std::size_t position = 0;
    for (char const character : text) {
        if (is_space(character)) {
            CRUCIBLE_TRY(_, add_token(availability, text, start, position));
            start = position + 1;
        }
        ++position;
    }
    CRUCIBLE_TRY(_, add_token(availability, text, start, position));
    return availability;
}

::fixy::Result<::fixy::Unit, CcError> detail::set_cc_for_socket_keyed(SocketOptionKey const&, SocketFd fd,
                                                                      DeclaredCcChoice choice) noexcept {
    auto const& selection = choice.value();
    ::fixy::TextView const name = selection.kernel_name.view();

    // The name holds at most max_bytes - 1 characters, so each write is in
    // the buffer, and the zero after the name stays.
    std::array<char, KernelCcName::max_bytes> optname{};
    std::size_t position = 0;
    for (char const character : name) {
        optname[position] = character;
        ++position;
    }

    const int rc =
        ::setsockopt(fd.value(), IPPROTO_TCP, TCP_CONGESTION, optname.data(), static_cast<socklen_t>(name.size() + 1u));
    if (rc != 0) {
        return ::fixy::err(CcError::SetSockOptFailed);
    }
    return ::fixy::Unit{};
}

::fixy::Result<CcSelection, CcError> detail::query_cc_selection_for_socket_keyed(SocketOptionKey const&,
                                                                                 SocketFd fd) noexcept {
    std::array<char, KernelCcName::max_bytes> optname{};
    socklen_t len = static_cast<socklen_t>(optname.size());
    const int rc = ::getsockopt(fd.value(), IPPROTO_TCP, TCP_CONGESTION, optname.data(), &len);
    if (rc != 0) {
        return ::fixy::err(CcError::GetSockOptFailed);
    }

    // The kernel writes the name and its zero, and len counts the bytes that
    // it wrote.  The name is the characters before the first zero.
    const auto bounded_len =
        static_cast<std::size_t>(len) < optname.size() ? static_cast<std::size_t>(len) : optname.size();
    ::fixy::FixedText<KernelCcName::max_bytes> received{};
    for (std::size_t index = 0; index < bounded_len; ++index) {
        char const character = optname[index];
        if (character == '\0') {
            break;
        }
        received.push(character).expect("the text holds as many characters as the buffer");
    }
    CRUCIBLE_TRY(admitted, KernelCcName::from(received.view()));
    CRUCIBLE_TRY(algorithm, algorithm_from_kernel_name(admitted.view()));

    return CcSelection{
        .algorithm = algorithm,
        .kernel_name = admitted,
    };
}

}  // namespace crucible::cntp
