#pragma once

#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/os/Path.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Socket.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <array>
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

[[nodiscard]] ::fixy::TextView cc_algorithm_name(CcAlgorithm algorithm) noexcept;
[[nodiscard]] ::fixy::TextView link_class_name(LinkClass link) noexcept;

using SocketFd = ::fixy::NonNegative<int>;
using CcAlgorithmMask = ::fixy::Bits<CcAlgorithm>;

// The name of a congestion control module of the kernel.  from() checks each
// character and is the one writer.  The name holds at most max_bytes - 1
// characters, so a buffer of max_bytes for the kernel has room for the zero
// after the name.  set_cc_for_socket copies the name into such a buffer.
class KernelCcName {
public:
    // TCP_CA_NAME_MAX of the kernel: the characters of a name and its zero.
    static constexpr std::size_t max_bytes = 16;

    // A default-constructed name is empty, which reads as "no algorithm
    // selected".
    constexpr KernelCcName() noexcept = default;

    // The characters of the name.  The view borrows this name, so a view of a
    // temporary name is refused.
    [[nodiscard]] constexpr ::fixy::TextView view() const& noexcept { return characters_.view(); }
    ::fixy::TextView
    view() const&& = delete("a view of a temporary KernelCcName points into an object that the full expression ends");

    // A Result<KernelCcName, CcError>.  The type of a Result needs a complete
    // name, so the definition stands after the class.
    [[nodiscard]] static constexpr auto from(::fixy::TextView name) noexcept;

private:
    ::fixy::FixedText<max_bytes - 1> characters_{};
};

// A name of max_bytes characters or more does not fit, and append() refuses
// it with one compare.  Then each character takes one check.
constexpr auto KernelCcName::from(::fixy::TextView name) noexcept {
    using Admitted = ::fixy::Result<KernelCcName, CcError>;
    KernelCcName out{};
    if (name.size() == 0 || out.characters_.append(name).is_err()) {
        return Admitted{::fixy::err(CcError::InvalidAlgorithmName)};
    }
    for (char const character : name) {
        bool const is_name_character = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')
                                    || character == '_' || character == '-';
        if (!is_name_character) {
            return Admitted{::fixy::err(CcError::InvalidAlgorithmName)};
        }
    }
    return Admitted{out};
}

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

// The module gives its name as a constant TextView, and from() admits it.
template <class Module>
concept CustomCcModule = requires { Module::congestion_control_name(); }
                      && requires { requires KernelCcName::from(Module::congestion_control_name()).is_ok(); };

[[nodiscard]] constexpr std::expected<SocketFd, CcError> admit_socket_fd(int fd) noexcept {
    if (fd < 0) {
        return std::unexpected(CcError::InvalidSocketFd);
    }
    return ::fixy::mint_refined<::fixy::non_negative>(fd);
}

[[nodiscard]] constexpr ::fixy::Result<KernelCcName, CcError> kernel_name_for(CcAlgorithm algorithm) noexcept {
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
            return ::fixy::err(CcError::InvalidAlgorithmName);
        default:
            return ::fixy::err(CcError::UnknownAlgorithm);
    }
}

template <CcAlgorithm Algorithm, LinkClass Link>
    requires CcCompatible<Algorithm, Link>
[[nodiscard]] constexpr DeclaredCcChoice mint_cc_choice() noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::CcAlgorithm>(CcSelection{
        .algorithm = Algorithm,
        .kernel_name = kernel_name_for(Algorithm).expect("each algorithm other than Custom has a kernel name"),
    });
}

template <class Module, LinkClass Link>
    requires CustomCcModule<Module>
[[nodiscard]] constexpr DeclaredCcChoice mint_custom_cc_choice() noexcept {
    static_cast<void>(Link);
    return ::fixy::mint_tagged<::fixy::tags::source::CcAlgorithm>(CcSelection{
        .algorithm = CcAlgorithm::Custom,
        .kernel_name = KernelCcName::from(Module::congestion_control_name())
                           .expect("the constraint CustomCcModule admits the name"),
    });
}

[[nodiscard]] ::fixy::Result<CcAlgorithm, CcError> algorithm_from_kernel_name(::fixy::TextView name) noexcept;

[[nodiscard]] ::fixy::Result<CcAvailability, CcError>
parse_available_congestion_control(::fixy::TextView text) noexcept;

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
[[nodiscard]] ::fixy::Result<CcAvailability, CcError> read_available_congestion_control(Ctx const& ctx) noexcept {
    auto path = ::fixy::sanitize_path(
        ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{detail::available_cc_path}));
    if (!path.has_value()) {
        return ::fixy::err(CcError::SysctlUnavailable);
    }
    const ::fixy::fs::OwnedFd fd =
        ::fixy::fs::mint_file<ProcFileReadMode>(ctx, std::move(*path))
            .match(
                [](::fixy::Linear<::fixy::fs::OwnedFd> opened) -> ::fixy::fs::OwnedFd {
                    return std::move(opened).consume();
                },
                [](::foundation::os::Errno) -> ::fixy::fs::OwnedFd { return ::fixy::fs::OwnedFd{}; });
    if (!fd.is_open()) {
        return ::fixy::err(CcError::SysctlUnavailable);
    }
    std::array<char, detail::available_cc_capacity> text{};
    std::size_t const bytes_read = ::fixy::fs::read_full(ctx, fd, std::as_writable_bytes(std::span{text})).value_or(0);
    if (bytes_read == 0) {
        return ::fixy::err(CcError::SysctlUnavailable);
    }
    ::fixy::FixedText<detail::available_cc_capacity> listed{};
    for (std::size_t index = 0; index < bytes_read; ++index) {
        listed.push(text[index]).expect("read_full reads at most the size of the buffer");
    }
    return parse_available_congestion_control(listed.view());
}

template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] bool kernel_supports(Ctx const& ctx, CcAlgorithm algorithm) noexcept {
    return read_available_congestion_control(ctx).match(
        [algorithm](CcAvailability availability) noexcept { return availability.contains(algorithm); },
        [](CcError) noexcept { return false; });
}

template <LinkClass Link>
[[nodiscard]] constexpr ::fixy::Result<DeclaredCcChoice, CcError> recommend_cc(CcAvailability availability) noexcept {
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
    return ::fixy::err(CcError::AlgorithmUnavailable);
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
// and an out-of-line socket option body takes it by reference.
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

// The selection of a name that the kernel gave for a socket.  A name that
// KernelCcName::from() refuses, or that names no algorithm, is an error.
[[nodiscard]] ::fixy::Result<CcSelection, CcError> selection_of_kernel_name(::fixy::TextView name) noexcept;

// The read of TCP_CONGESTION for a body that holds a key and no context.
// It calls getsockopt, because each call of the socket door takes a context.
[[nodiscard]] ::fixy::Result<CcSelection, CcError> query_cc_selection_for_socket_keyed(SocketOptionKey const&,
                                                                                       SocketFd fd) noexcept;

}  // namespace detail

// The socket door sets TCP_CONGESTION.  The name of the choice holds at most
// 15 characters, so the door does not refuse its length.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, CcError> set_cc_for_socket(Ctx const& ctx, SocketFd fd,
                                                                      DeclaredCcChoice choice) noexcept {
    using Set = ::fixy::Result<::fixy::Unit, CcError>;
    return ::fixy::net::set_tcp_congestion(ctx, fd, choice.value().kernel_name.view())
        .match([](::fixy::Unit) -> Set { return ::fixy::Unit{}; },
               [](::foundation::os::Errno) -> Set { return ::fixy::err(CcError::SetSockOptFailed); });
}

// The socket door reads TCP_CONGESTION.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<CcSelection, CcError> query_cc_selection_for_socket(Ctx const& ctx, SocketFd fd) noexcept {
    using Queried = ::fixy::Result<CcSelection, CcError>;
    return ::fixy::net::read_tcp_congestion(ctx, fd).match(
        [](::fixy::FixedText<::fixy::net::congestion_name_bytes> name) -> Queried {
            return detail::selection_of_kernel_name(name.view());
        },
        [](::foundation::os::Errno) -> Queried { return ::fixy::err(CcError::GetSockOptFailed); });
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<CcAlgorithm, CcError> query_cc_for_socket(Ctx const& ctx, SocketFd fd) noexcept {
    CRUCIBLE_TRY(selection, query_cc_selection_for_socket(ctx, fd));
    return selection.algorithm;
}

// The contexts that the check files of this header and of IncastControl.h
// test the socket-option gates with.
namespace detail::socket_option_invariants {

using ::fixy::atom::syscall::SyscallId;
namespace fe = ::foundation::effects;

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;
using IoOnlyCtx = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::IO>>;
using BgOnlyCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>;

}  // namespace detail::socket_option_invariants

}  // namespace crucible::cntp
