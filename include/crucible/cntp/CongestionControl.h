#pragma once

#include <crucible/Platform.h>
#include <fixy/Bits.h>
#include <fixy/Path.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/Fs.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

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

using SocketFd = ::fixy::NonNegative<int>;
using CcAlgorithmMask = ::fixy::Bits<CcAlgorithm>;

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

using DeclaredCcChoice = ::fixy::Tagged<CcSelection, ::fixy::tags::source::CcAlgorithm>;

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
    return ::fixy::mint_refined<::fixy::non_negative>(fd);
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
    return ::fixy::mint_tagged<::fixy::tags::source::CcAlgorithm>(CcSelection{
        .algorithm = Algorithm,
        .kernel_name = name.value(),
    });
}

template <class Module, LinkClass Link>
    requires CustomCcModule<Module>
[[nodiscard]] constexpr DeclaredCcChoice mint_custom_cc_choice() noexcept {
    static_cast<void>(Link);
    auto name = KernelCcName::from(Module::congestion_control_name());
    return ::fixy::mint_tagged<::fixy::tags::source::CcAlgorithm>(CcSelection{
        .algorithm = CcAlgorithm::Custom,
        .kernel_name = name.value(),
    });
}

[[nodiscard]] std::expected<CcAlgorithm, CcError> algorithm_from_kernel_name(std::string_view name) noexcept;

[[nodiscard]] std::expected<CcAvailability, CcError> parse_available_congestion_control(std::string_view text) noexcept;

// The kernel lists its algorithms in a file under /proc.  The fs door
// opens it, and that door counts an open and a read as IO and Block, so
// only a context that carries both can ask what the kernel supports.
using ProcFileReadMode = ::fixy::atom::fs::mode<::fixy::fs::open_mode::ReadOnly>;

namespace detail {

inline constexpr std::string_view available_cc_path = "/proc/sys/net/ipv4/tcp_available_congestion_control";

// The list is one short line.  A longer file is read up to this bound, and
// the parse then refuses the cut name.
inline constexpr std::size_t available_cc_capacity = 512;

}  // namespace detail

// The path is a literal, and it still takes the traversal check, because
// that check is the one way to a path the door accepts.  The read goes
// through the same door, under the same context.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] std::expected<CcAvailability, CcError> read_available_congestion_control(Ctx const& ctx) noexcept {
    auto path = ::fixy::sanitize_path(
        ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{detail::available_cc_path}));
    if (!path.has_value()) {
        return std::unexpected(CcError::SysctlUnavailable);
    }
    auto opened = ::fixy::fs::mint_file<ProcFileReadMode>(ctx, std::move(*path));
    if (!opened.has_value()) {
        return std::unexpected(CcError::SysctlUnavailable);
    }
    const ::fixy::fs::OwnedFd fd = std::move(*opened).consume();
    std::array<char, detail::available_cc_capacity> text{};
    auto bytes_read = ::fixy::fs::read_full(ctx, fd, std::as_writable_bytes(std::span{text}));
    if (!bytes_read.has_value() || *bytes_read == 0) {
        return std::unexpected(CcError::SysctlUnavailable);
    }
    return parse_available_congestion_control(std::string_view{text.data(), *bytes_read});
}

template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] bool kernel_supports(Ctx const& ctx, CcAlgorithm algorithm) noexcept {
    auto availability = read_available_congestion_control(ctx);
    return availability.has_value() && availability->contains(algorithm);
}

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

// A socket option call enters the kernel in the network family of the
// syscall catalog, and that family lifts to IO and Block.  setsockopt can
// load a congestion control module, and each call takes the socket lock,
// so each call can park the caller.
using socket_option_row_t =
    ::foundation::effects::lift_row_t<::fixy::atom::syscall::family<::fixy::atom::syscall::SyscallFamily::NetworkIo>>;

template <class Ctx>
concept CtxFitsSocketOption =
    ::foundation::effects::IsExecCtx<Ctx> && ::foundation::effects::CtxAdmits<Ctx, socket_option_row_t>;

namespace detail {

class SocketOptionKey;

// The one door to a key.  It takes a context that admits the socket
// option row, so a body that takes a key runs only under such a context.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::crucible::cntp::CtxFitsSocketOption<Ctx>
[[nodiscard]] constexpr auto socket_option_key_(Ctx const&) noexcept -> SocketOptionKey;

// The proof that a context admitted the socket option row.  The
// constructor is private and socket_option_key_ is its one friend.  The
// copy and the move are deleted, so a key stays in the scope that made it,
// and the out-of-line socket option bodies below take it by reference.
class SocketOptionKey {
    constexpr SocketOptionKey() noexcept = default;

    // Trailing return type on purpose: the parser takes `>::` after a
    // leading return type as a nested-name-specifier.
    template <::foundation::effects::IsExecCtx FriendCtx>
        requires ::crucible::cntp::CtxFitsSocketOption<FriendCtx>
    friend constexpr auto socket_option_key_(FriendCtx const&) noexcept -> SocketOptionKey;

public:
    SocketOptionKey(SocketOptionKey const&) = delete("a key proves one gated scope, and a copy would carry it out");
    SocketOptionKey&
    operator=(SocketOptionKey const&) = delete("a key proves one gated scope, and a copy would carry it out");
    SocketOptionKey(SocketOptionKey&&) = delete("a key proves one gated scope, and a move would carry it out");
    SocketOptionKey&
    operator=(SocketOptionKey&&) = delete("a key proves one gated scope, and a move would carry it out");
    ~SocketOptionKey() = default;
};

template <::foundation::effects::IsExecCtx Ctx>
    requires ::crucible::cntp::CtxFitsSocketOption<Ctx>
[[nodiscard]] constexpr auto socket_option_key_(Ctx const&) noexcept -> SocketOptionKey {
    return SocketOptionKey{};
}

// The two TCP_CONGESTION bodies.  Each takes the key, so only a gated
// form below reaches setsockopt or getsockopt.
[[nodiscard]] std::expected<void, CcError> set_cc_for_socket_keyed(SocketOptionKey const&, SocketFd fd,
                                                                   DeclaredCcChoice choice) noexcept;

[[nodiscard]] std::expected<CcSelection, CcError> query_cc_selection_for_socket_keyed(SocketOptionKey const&,
                                                                                      SocketFd fd) noexcept;

}  // namespace detail

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] std::expected<void, CcError> set_cc_for_socket(Ctx const& ctx, SocketFd fd,
                                                             DeclaredCcChoice choice) noexcept {
    return detail::set_cc_for_socket_keyed(detail::socket_option_key_(ctx), fd, choice);
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] std::expected<CcSelection, CcError> query_cc_selection_for_socket(Ctx const& ctx, SocketFd fd) noexcept {
    return detail::query_cc_selection_for_socket_keyed(detail::socket_option_key_(ctx), fd);
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] std::expected<CcAlgorithm, CcError> query_cc_for_socket(Ctx const& ctx, SocketFd fd) noexcept {
    auto selection = query_cc_selection_for_socket(ctx, fd);
    if (!selection.has_value()) {
        return std::unexpected(selection.error());
    }
    return selection->algorithm;
}

namespace detail::socket_option_invariants {

using ::fixy::atom::syscall::SyscallId;
namespace fe = ::foundation::effects;

// The row that the catalog gives to the network family, stated here as
// a pin.  mint_socket reads the same row for ::socket.
static_assert(std::is_same_v<socket_option_row_t, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<socket_option_row_t, fe::lift_row_t<::fixy::atom::syscall::per<SyscallId::socket>>>);

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;
using IoOnlyCtx = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::IO>>;
using BgOnlyCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>;

static_assert(CtxFitsSocketOption<IoBlockCtx>);
static_assert(!CtxFitsSocketOption<IoOnlyCtx>, "a context without Block must not take the socket lock.");
static_assert(!CtxFitsSocketOption<BgOnlyCtx>, "a context without IO must not enter the kernel.");
static_assert(!CtxFitsSocketOption<int>);

// The key has no public constructor, no copy and no move, so a body that
// takes it is reachable only through a gated form.
static_assert(!std::is_default_constructible_v<SocketOptionKey>);
static_assert(!std::is_copy_constructible_v<SocketOptionKey>);
static_assert(!std::is_move_constructible_v<SocketOptionKey>);
static_assert(!std::is_aggregate_v<SocketOptionKey>);

}  // namespace detail::socket_option_invariants

}  // namespace crucible::cntp
