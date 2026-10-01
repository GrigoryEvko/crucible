#pragma once

// The three durable writers the Cipher uses: a warm snapshot writer, a
// cold archive writer, and the writer that advances HEAD.  Each is a
// stance — one open mode, one flag, one sync, one commit, pinned as
// types — and a handle over a descriptor that was opened with exactly
// that stance's flags.
//
// Design notes:
//
//  1. cold_writer_stance commits with atomicity::Rename, so a cold
//     commit replaces an existing target.  The warm stance and the HEAD
//     stance commit with atomicity::RenameAt2NoReplace, which refuses an
//     existing target with EEXIST.
//
//  2. One concept, CtxFitsDurableMint<Ctx, Stance, Extras...>, gates
//     the three mints.  It folds the stance's own atoms into
//     fs::CtxFitsFileMint and does not name the row by hand, so the
//     known-tag checks and the derived row apply to a stance the same
//     way they apply to a bare pack.
//
//  3. A DurableStance concept constrains the handle and the mints.  A
//     stance names all four types, and each is a tag the file surface
//     knows.  The warm stance and the HEAD stance name
//     flag::CloseOnExec, which mint_file folds in unconditionally, so
//     naming it changes no bit.
//
//  4. The handle wraps fixy::fs::OwnedFd.  Its constructor is private,
//     and the three mints are its sole friends: an OwnedFd is evidence
//     of a descriptor the caller owns, not of the flags it was opened
//     with, and the stance is a claim about those flags.  Only the mint
//     that chose them can make it.

#include <fixy/Path.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <sys/stat.h>

#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fixy::cipher::durable {

namespace eff = ::foundation::effects;
namespace open_mode = ::fixy::fs::open_mode;
namespace flag = ::fixy::fs::flag;
namespace sync_op = ::fixy::fs::sync_op;
namespace atomicity = ::fixy::fs::atomicity;
namespace atom_fs = ::fixy::atom::fs;

template <typename Source>
using Path = ::fixy::Path<Source>;

struct warm_writer_stance final {
    using mode_type = open_mode::WriteTruncate;
    using flag_type = flag::CloseOnExec;
    using sync_op_type = sync_op::Fdatasync;
    using atomicity_type = atomicity::RenameAt2NoReplace;
};

struct cold_writer_stance final {
    using mode_type = open_mode::WriteCreate;
    using flag_type = flag::FullSync;  // O_SYNC writes
    using sync_op_type = sync_op::Fsync;
    using atomicity_type = atomicity::Rename;
};

struct head_advance_stance final {
    using mode_type = open_mode::WriteCreate;
    using flag_type = flag::CloseOnExec;
    using sync_op_type = sync_op::FsyncParentDir;
    using atomicity_type = atomicity::RenameAt2NoReplace;
};

// A stance names all four axes, each with a tag the file surface knows,
// and carries nothing per instance.
template <typename S>
concept DurableStance = std::is_empty_v<S> && requires {
    typename S::mode_type;
    typename S::flag_type;
    typename S::sync_op_type;
    typename S::atomicity_type;
} && ::fixy::fs::MappedOpenMode<typename S::mode_type> && ::fixy::fs::MappedFlag<typename S::flag_type> && ::fixy::fs::KnownSyncOp<typename S::sync_op_type> && ::fixy::fs::KnownAtomicity<typename S::atomicity_type>;

// A stance pins the mode, durability and atomicity axes, so
// caller-supplied extras must not engage them.  Composing a shadowing
// extra would still fail, one layer deeper on the duplicate-mode
// clause; refusing here points the diagnostic at the rule the caller
// actually violated.
namespace detail {

template <typename... Extras>
inline constexpr bool extras_engage_mode_v = (::fixy::fs::detail::is_mode_atom_v<Extras> || ...);

template <typename... Extras>
inline constexpr bool extras_engage_durable_v = (::fixy::fs::detail::is_durable_atom_v<Extras> || ...);

template <typename... Extras>
inline constexpr bool extras_engage_atomic_write_v = (::fixy::fs::detail::is_atomic_write_atom_v<Extras> || ...);

}  // namespace detail

template <typename Ctx, typename Stance, typename... Extras>
concept CtxFitsDurableMint = DurableStance<Stance>
                          && ::fixy::fs::CtxFitsFileMint<Ctx, atom_fs::mode<typename Stance::mode_type>,
                                                         atom_fs::with_flag<typename Stance::flag_type>, Extras...>
                          && !detail::extras_engage_mode_v<Extras...> && !detail::extras_engage_durable_v<Extras...>
                          && !detail::extras_engage_atomic_write_v<Extras...>;

template <DurableStance Stance>
class CipherDurableHandle;

// Declared before the handle so it can name them as the sole friends of
// its only constructor; defined after it.  The default argument belongs
// to these declarations and is absent from the friend declarations and
// the definitions.
//
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::fixy::cipher::durable::CtxFitsDurableMint<Ctx, warm_writer_stance, Extras...>
[[nodiscard]] std::expected<Linear<CipherDurableHandle<warm_writer_stance>>, std::error_code>
mint_warm_writer(Ctx const&, Path<tags::source::Sanitized>, ::mode_t perms = 0644) noexcept;

// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::fixy::cipher::durable::CtxFitsDurableMint<Ctx, cold_writer_stance, Extras...>
[[nodiscard]] std::expected<Linear<CipherDurableHandle<cold_writer_stance>>, std::error_code>
mint_cold_writer(Ctx const&, Path<tags::source::Sanitized>, ::mode_t perms = 0644) noexcept;

// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::fixy::cipher::durable::CtxFitsDurableMint<Ctx, head_advance_stance, Extras...>
[[nodiscard]] std::expected<Linear<CipherDurableHandle<head_advance_stance>>, std::error_code>
mint_head_advancer(Ctx const&, Path<tags::source::Sanitized>, ::mode_t perms = 0644) noexcept;

template <DurableStance Stance>
class [[nodiscard]] CipherDurableHandle final {
    ::fixy::fs::OwnedFd handle_{};

    explicit CipherDurableHandle(::fixy::fs::OwnedFd&& fd) noexcept : handle_{std::move(fd)} {}

    // Trailing return types on purpose: the leading form ends in `>`
    // and the parser takes `>::` as a nested-name-specifier.  Documented
    // at fixy/os/CpuPinned.h.  Each mint is a friend of every stance's
    // handle, and each builds only the one its return type names.
    template <typename... FriendExtras, eff::IsExecCtx FriendCtx>
        requires ::fixy::cipher::durable::CtxFitsDurableMint<FriendCtx, warm_writer_stance, FriendExtras...>
    friend auto mint_warm_writer(FriendCtx const&, Path<tags::source::Sanitized>, ::mode_t) noexcept
        -> std::expected<Linear<CipherDurableHandle<warm_writer_stance>>, std::error_code>;

    template <typename... FriendExtras, eff::IsExecCtx FriendCtx>
        requires ::fixy::cipher::durable::CtxFitsDurableMint<FriendCtx, cold_writer_stance, FriendExtras...>
    friend auto mint_cold_writer(FriendCtx const&, Path<tags::source::Sanitized>, ::mode_t) noexcept
        -> std::expected<Linear<CipherDurableHandle<cold_writer_stance>>, std::error_code>;

    template <typename... FriendExtras, eff::IsExecCtx FriendCtx>
        requires ::fixy::cipher::durable::CtxFitsDurableMint<FriendCtx, head_advance_stance, FriendExtras...>
    friend auto mint_head_advancer(FriendCtx const&, Path<tags::source::Sanitized>, ::mode_t) noexcept
        -> std::expected<Linear<CipherDurableHandle<head_advance_stance>>, std::error_code>;

public:
    using stance_type = Stance;
    using row_discipline = CipherDurableHandle;
    using row_payload = ::foundation::diag::row_payloads<>;

    // A default-constructed handle is closed.  Only a mint opens one.
    CipherDurableHandle() noexcept = default;

    CipherDurableHandle(const CipherDurableHandle&) = delete("a descriptor is unique; copy would double-close");
    CipherDurableHandle&
    operator=(const CipherDurableHandle&) = delete("a descriptor is unique; copy would double-close");
    CipherDurableHandle(CipherDurableHandle&&) noexcept = default;
    CipherDurableHandle& operator=(CipherDurableHandle&&) noexcept = default;

    // The inner handle closes the descriptor when it drops.
    ~CipherDurableHandle() noexcept = default;

    [[nodiscard]] bool is_open() const noexcept { return handle_.is_open(); }
    [[nodiscard]] int get() const noexcept { return handle_.get(); }
    [[nodiscard]] const ::fixy::fs::OwnedFd& handle() const noexcept { return handle_; }

    template <eff::IsExecCtx Ctx>
        requires ::fixy::fs::CtxFitsSync<Ctx, typename Stance::sync_op_type>
    [[nodiscard]] std::expected<void, std::error_code> sync(Ctx const& ctx) const noexcept {
        return ::fixy::fs::sync<typename Stance::sync_op_type>(ctx, handle_);
    }

    template <eff::IsExecCtx Ctx>
        requires ::fixy::fs::CtxFitsCommitAtomic<Ctx, typename Stance::atomicity_type>
    [[nodiscard]] std::expected<void, std::error_code>
    commit_atomic(Ctx const& ctx, Path<tags::source::Sanitized> tmp_path,
                  Path<tags::source::Sanitized> target_path) const noexcept {
        return ::fixy::fs::commit_atomic<typename Stance::atomicity_type>(ctx, std::move(tmp_path),
                                                                          std::move(target_path));
    }
};

namespace detail {

// One body for the three mints.  It is not a friend of anything: it
// receives the handle the calling mint built, which is the only place
// the private constructor is reachable, and wraps it.
template <DurableStance Stance, typename... Extras, eff::IsExecCtx Ctx>
[[nodiscard]] inline std::expected<::fixy::fs::OwnedFd, std::error_code>
open_with_stance_(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms) noexcept {
    auto fd =
        ::fixy::fs::mint_file<atom_fs::mode<typename Stance::mode_type>, atom_fs::with_flag<typename Stance::flag_type>,
                              Extras...>(ctx, std::move(path), perms);
    if (!fd) {
        return std::unexpected{fd.error()};
    }
    return std::move(*fd).consume();
}

}  // namespace detail

template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::fixy::cipher::durable::CtxFitsDurableMint<Ctx, warm_writer_stance, Extras...>
[[nodiscard]] std::expected<Linear<CipherDurableHandle<warm_writer_stance>>, std::error_code>
mint_warm_writer(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms) noexcept {
    auto fd = detail::open_with_stance_<warm_writer_stance, Extras...>(ctx, std::move(path), perms);
    if (!fd) {
        return std::unexpected{fd.error()};
    }
    return mint_linear<CipherDurableHandle<warm_writer_stance>>(
        CipherDurableHandle<warm_writer_stance>{std::move(*fd)});
}

template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::fixy::cipher::durable::CtxFitsDurableMint<Ctx, cold_writer_stance, Extras...>
[[nodiscard]] std::expected<Linear<CipherDurableHandle<cold_writer_stance>>, std::error_code>
mint_cold_writer(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms) noexcept {
    auto fd = detail::open_with_stance_<cold_writer_stance, Extras...>(ctx, std::move(path), perms);
    if (!fd) {
        return std::unexpected{fd.error()};
    }
    return mint_linear<CipherDurableHandle<cold_writer_stance>>(
        CipherDurableHandle<cold_writer_stance>{std::move(*fd)});
}

// This mint opens only the temporary file that becomes the new HEAD.
// The caller opens the parent directory separately with open_dirfd and
// calls sync on that, which is what flushes the directory entry after
// the rename.
template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::fixy::cipher::durable::CtxFitsDurableMint<Ctx, head_advance_stance, Extras...>
[[nodiscard]] std::expected<Linear<CipherDurableHandle<head_advance_stance>>, std::error_code>
mint_head_advancer(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms) noexcept {
    auto fd = detail::open_with_stance_<head_advance_stance, Extras...>(ctx, std::move(path), perms);
    if (!fd) {
        return std::unexpected{fd.error()};
    }
    return mint_linear<CipherDurableHandle<head_advance_stance>>(
        CipherDurableHandle<head_advance_stance>{std::move(*fd)});
}

}  // namespace fixy::cipher::durable
