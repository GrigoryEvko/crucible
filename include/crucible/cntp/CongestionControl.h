#pragma once

#include <crucible/Platform.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_Tagged.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class CcAlgorithm : std::uint16_t {
    Bbr3 = 1u << 0,
    Cubic = 1u << 1,
    Dctcp = 1u << 2,
    Reno = 1u << 3,
    Vegas = 1u << 4,
    Bbr2 = 1u << 5,
    Bbr1 = 1u << 6,
    Custom = 1u << 7,
};

enum class LinkClass : std::uint8_t {
    CrossDatacenter,
    LosslessDatacenterFabric,
    PublicInternet,
    LegacyKernel,
    Loopback,
};

enum class CcError : std::uint8_t {
    InvalidSocketFd,
    InvalidAlgorithmName,
    UnknownAlgorithm,
    AlgorithmUnavailable,
    SysctlUnavailable,
    SetSockOptFailed,
    GetSockOptFailed,
};

[[nodiscard]] std::string_view cc_algorithm_name(CcAlgorithm algorithm) noexcept;
[[nodiscard]] std::string_view link_class_name(LinkClass link) noexcept;

using SocketFd = safety::NonNegative<int>;
using CcAlgorithmMask = safety::Bits<CcAlgorithm>;

// The stored length is private and from() is its only writer, so every
// KernelCcName satisfies view().size() < max_bytes by construction.
// set_cc_for_socket relies on that: it copies view() into a
// max_bytes-wide buffer and hands the kernel view().size() + 1 as the
// socklen_t, so a length the validation never saw would both overflow
// that buffer and send the kernel a length past the end of it.
//
// This mirrors cntp::NicInterfaceName, which carries the same shape for
// the same reason.  Neither type is an aggregate: while the members were
// public, `KernelCcName n{}; n.size = 200;` was well-formed.
class KernelCcName {
public:
    static constexpr std::size_t max_bytes = 16;

    // A default-constructed name is empty, which reads as "no algorithm
    // selected" and copies zero bytes.
    constexpr KernelCcName() noexcept = default;

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {bytes_.data(), size_}; }

    [[nodiscard]] static constexpr std::expected<KernelCcName, CcError> from(std::string_view name) noexcept {
        if (name.empty() || name.size() >= max_bytes) {
            return std::unexpected(CcError::InvalidAlgorithmName);
        }

        KernelCcName out{};
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) {
                return std::unexpected(CcError::InvalidAlgorithmName);
            }
            out.bytes_[i] = c;
        }
        out.size_ = static_cast<std::uint8_t>(name.size());
        return out;
    }

private:
    std::array<char, max_bytes> bytes_{};
    std::uint8_t size_ = 0;
};

static_assert(sizeof(SocketFd) == sizeof(int));
static_assert(std::is_trivially_copyable_v<KernelCcName>);
static_assert(!std::is_aggregate_v<KernelCcName>,
              "KernelCcName must not be an aggregate: from() is the only path that may set the length");

struct CcSelection {
    CcAlgorithm algorithm = CcAlgorithm::Cubic;
    KernelCcName kernel_name{};
};

using DeclaredCcChoice = safety::Tagged<CcSelection, safety::source::CcAlgorithm>;

struct CcAvailability {
    CcAlgorithmMask algorithms{};

    [[nodiscard]] constexpr bool contains(CcAlgorithm algorithm) const noexcept { return algorithms.test(algorithm); }
};

template <CcAlgorithm Algorithm, LinkClass Link>
concept CcCompatible = Algorithm != CcAlgorithm::Custom
                    && (Algorithm != CcAlgorithm::Dctcp || Link == LinkClass::LosslessDatacenterFabric);

template <class Module>
concept CustomCcModule = requires {
    { Module::congestion_control_name() } -> std::convertible_to<std::string_view>;
} && requires { requires KernelCcName::from(Module::congestion_control_name()).has_value(); };

[[nodiscard]] constexpr std::expected<SocketFd, CcError> admit_socket_fd(int fd) noexcept {
    if (fd < 0) {
        return std::unexpected(CcError::InvalidSocketFd);
    }
    return SocketFd{fd, typename SocketFd::Trusted{}};
}

[[nodiscard]] constexpr std::expected<KernelCcName, CcError> kernel_name_for(CcAlgorithm algorithm) noexcept {
    // The in-tree "bbr" module is BBRv1, so Bbr1 owns that literal name.
    // BBRv3 registers as "bbr3" and comes from an out-of-tree patchset.
    switch (algorithm) {
        case CcAlgorithm::Bbr3:
            return KernelCcName::from("bbr3");
        case CcAlgorithm::Cubic:
            return KernelCcName::from("cubic");
        case CcAlgorithm::Dctcp:
            return KernelCcName::from("dctcp");
        case CcAlgorithm::Reno:
            return KernelCcName::from("reno");
        case CcAlgorithm::Vegas:
            return KernelCcName::from("vegas");
        case CcAlgorithm::Bbr2:
            return KernelCcName::from("bbr2");
        case CcAlgorithm::Bbr1:
            return KernelCcName::from("bbr");
        case CcAlgorithm::Custom:
            return std::unexpected(CcError::InvalidAlgorithmName);
        default:
            return std::unexpected(CcError::UnknownAlgorithm);
    }
}

template <CcAlgorithm Algorithm, LinkClass Link>
    requires CcCompatible<Algorithm, Link>
[[nodiscard]] constexpr DeclaredCcChoice mint_cc_choice() noexcept {
    auto name = kernel_name_for(Algorithm);
    return DeclaredCcChoice{CcSelection{
        .algorithm = Algorithm,
        .kernel_name = name.value(),
    }};
}

template <class Module, LinkClass Link>
    requires CustomCcModule<Module>
[[nodiscard]] constexpr DeclaredCcChoice mint_custom_cc_choice() noexcept {
    static_cast<void>(Link);
    auto name = KernelCcName::from(Module::congestion_control_name());
    return DeclaredCcChoice{CcSelection{
        .algorithm = CcAlgorithm::Custom,
        .kernel_name = name.value(),
    }};
}

[[nodiscard]] std::expected<CcAlgorithm, CcError> algorithm_from_kernel_name(std::string_view name) noexcept;

[[nodiscard]] std::expected<CcAvailability, CcError> parse_available_congestion_control(std::string_view text) noexcept;

[[nodiscard]] std::expected<CcAvailability, CcError> read_available_congestion_control() noexcept;

[[nodiscard]] bool kernel_supports(CcAlgorithm algorithm) noexcept;

template <LinkClass Link>
[[nodiscard]] constexpr std::expected<DeclaredCcChoice, CcError> recommend_cc(CcAvailability availability) noexcept {
    if constexpr (Link == LinkClass::LosslessDatacenterFabric) {
        if (availability.contains(CcAlgorithm::Dctcp)) {
            return mint_cc_choice<CcAlgorithm::Dctcp, Link>();
        }
    }

    // Walk the whole BBR family before falling back to a loss-based
    // algorithm.  A host carrying only BBRv2, or only stock BBRv1, would
    // otherwise land on Cubic and lose the BBR behaviour.
    if (availability.contains(CcAlgorithm::Bbr3)) {
        return mint_cc_choice<CcAlgorithm::Bbr3, Link>();
    }
    if (availability.contains(CcAlgorithm::Bbr2)) {
        return mint_cc_choice<CcAlgorithm::Bbr2, Link>();
    }
    if (availability.contains(CcAlgorithm::Bbr1)) {
        return mint_cc_choice<CcAlgorithm::Bbr1, Link>();
    }
    if (availability.contains(CcAlgorithm::Cubic)) {
        return mint_cc_choice<CcAlgorithm::Cubic, Link>();
    }
    if (availability.contains(CcAlgorithm::Reno)) {
        return mint_cc_choice<CcAlgorithm::Reno, Link>();
    }
    return std::unexpected(CcError::AlgorithmUnavailable);
}

[[nodiscard]] std::expected<void, CcError> set_cc_for_socket(SocketFd fd, DeclaredCcChoice choice) noexcept;

[[nodiscard]] std::expected<CcAlgorithm, CcError> query_cc_for_socket(SocketFd fd) noexcept;

[[nodiscard]] std::expected<CcSelection, CcError> query_cc_selection_for_socket(SocketFd fd) noexcept;

// The four operations below cross the kernel boundary, through setsockopt,
// getsockopt or a read under /proc, so each needs a context carrying
// Effect::IO.  The unparameterized forms above stay for callers that thread no
// context, which means the gate can still be bypassed by calling those.
template <effects::IsExecCtx Ctx>
    requires effects::CtxOwnsCapability<Ctx, effects::Effect::IO>
[[nodiscard]] std::expected<void, CcError> set_cc_for_socket(Ctx const&, SocketFd fd,
                                                             DeclaredCcChoice choice) noexcept {
    return set_cc_for_socket(fd, choice);
}

template <effects::IsExecCtx Ctx>
    requires effects::CtxOwnsCapability<Ctx, effects::Effect::IO>
[[nodiscard]] std::expected<CcAlgorithm, CcError> query_cc_for_socket(Ctx const&, SocketFd fd) noexcept {
    return query_cc_for_socket(fd);
}

template <effects::IsExecCtx Ctx>
    requires effects::CtxOwnsCapability<Ctx, effects::Effect::IO>
[[nodiscard]] std::expected<CcSelection, CcError> query_cc_selection_for_socket(Ctx const&, SocketFd fd) noexcept {
    return query_cc_selection_for_socket(fd);
}

template <effects::IsExecCtx Ctx>
    requires effects::CtxOwnsCapability<Ctx, effects::Effect::IO>
[[nodiscard]] std::expected<CcAvailability, CcError> read_available_congestion_control(Ctx const&) noexcept {
    return read_available_congestion_control();
}

}  // namespace crucible::cntp
