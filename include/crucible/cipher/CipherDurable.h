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

#include <fixy/Core.h>
#include <fixy/os/Path.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fs.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <type_traits>
#include <utility>

namespace crucible::cipher::durable {

namespace eff = ::foundation::effects;
namespace open_mode = ::fixy::fs::open_mode;
namespace flag = ::fixy::fs::flag;
namespace sync_op = ::fixy::fs::sync_op;
namespace atomicity = ::fixy::fs::atomicity;
namespace atom_fs = ::fixy::atom::fs;
namespace source = ::fixy::tags::source;

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
// actually violated.  Each fold reads the public concept IsAtomOf, which
// no unit can specialize.
template <typename Ctx, typename Stance, typename... Extras>
concept CtxFitsDurableMint = DurableStance<Stance>
                          && ::fixy::fs::CtxFitsFileMint<Ctx, atom_fs::mode<typename Stance::mode_type>,
                                                         atom_fs::with_flag<typename Stance::flag_type>, Extras...>
                          && !(::fixy::atom_pack::IsAtomOf<Extras, ^^::fixy::atom::fs::mode> || ...)
                          && !(::fixy::atom_pack::IsAtomOf<Extras, ^^::fixy::atom::fs::durable> || ...)
                          && !(::fixy::atom_pack::IsAtomOf<Extras, ^^::fixy::atom::fs::atomic_write> || ...);

template <DurableStance Stance>
class CipherDurableHandle;

// Declared before the handle so it can name them as the sole friends of
// its only constructor; defined after it.  The default argument belongs
// to these declarations and is absent from the friend declarations and
// the definitions.  Each mint gives a ::fixy::Result of the
// Linear handle or the error of the open.  The return type is deduced,
// because a Result of the handle needs the complete class.
template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::crucible::cipher::durable::CtxFitsDurableMint<Ctx, warm_writer_stance, Extras...>
[[nodiscard]] auto mint_warm_writer(Ctx const&, Path<source::Sanitized> const&, ::mode_t perms = 0644) noexcept;

template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::crucible::cipher::durable::CtxFitsDurableMint<Ctx, cold_writer_stance, Extras...>
[[nodiscard]] auto mint_cold_writer(Ctx const&, Path<source::Sanitized> const&, ::mode_t perms = 0644) noexcept;

template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::crucible::cipher::durable::CtxFitsDurableMint<Ctx, head_advance_stance, Extras...>
[[nodiscard]] auto mint_head_advancer(Ctx const&, Path<source::Sanitized> const&, ::mode_t perms = 0644) noexcept;

template <DurableStance Stance>
class [[nodiscard]] CipherDurableHandle final {
    ::fixy::fs::OwnedFd handle_{};

    explicit CipherDurableHandle(::fixy::fs::OwnedFd&& fd) noexcept : handle_{std::move(fd)} {}

    // Each mint is a friend of every stance's handle, and each builds only
    // the one its return type names.
    template <typename... FriendExtras, eff::IsExecCtx FriendCtx>
        requires ::crucible::cipher::durable::CtxFitsDurableMint<FriendCtx, warm_writer_stance, FriendExtras...>
    friend auto mint_warm_writer(FriendCtx const&, Path<source::Sanitized> const&, ::mode_t) noexcept;

    template <typename... FriendExtras, eff::IsExecCtx FriendCtx>
        requires ::crucible::cipher::durable::CtxFitsDurableMint<FriendCtx, cold_writer_stance, FriendExtras...>
    friend auto mint_cold_writer(FriendCtx const&, Path<source::Sanitized> const&, ::mode_t) noexcept;

    template <typename... FriendExtras, eff::IsExecCtx FriendCtx>
        requires ::crucible::cipher::durable::CtxFitsDurableMint<FriendCtx, head_advance_stance, FriendExtras...>
    friend auto mint_head_advancer(FriendCtx const&, Path<source::Sanitized> const&, ::mode_t) noexcept;

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
    [[nodiscard]] ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> sync(Ctx const& ctx) const noexcept {
        return ::fixy::fs::sync<typename Stance::sync_op_type>(ctx, handle_);
    }

    template <eff::IsExecCtx Ctx>
        requires ::fixy::fs::CtxFitsCommitAtomic<Ctx, typename Stance::atomicity_type>
    [[nodiscard]] ::fixy::Result<::fixy::Unit, ::foundation::os::Errno>
    commit_atomic(Ctx const& ctx, Path<source::Sanitized> const& tmp_path,
                  Path<source::Sanitized> const& target_path) const noexcept {
        return ::fixy::fs::commit_atomic<typename Stance::atomicity_type>(ctx, tmp_path, target_path);
    }
};

namespace detail {

// One body for the three mints.  It is not a friend of anything: it
// receives the handle the calling mint built, which is the only place
// the private constructor is reachable, and wraps it.  The transform
// takes the result of mint_file as an rvalue, so the linear descriptor
// reaches its consumer on the value path and no local holds it.
template <DurableStance Stance, typename... Extras, eff::IsExecCtx Ctx>
[[nodiscard]] inline ::fixy::Result<::fixy::fs::OwnedFd, ::foundation::os::Errno>
open_with_stance_(Ctx const& ctx, Path<source::Sanitized> const& path, ::mode_t perms) noexcept {
    using Opened = ::fixy::Result<::fixy::fs::OwnedFd, ::foundation::os::Errno>;
    return ::fixy::fs::mint_file<atom_fs::mode<typename Stance::mode_type>,
                                 atom_fs::with_flag<typename Stance::flag_type>, Extras...>(ctx, path, perms)
        .match(
            [](::fixy::Linear<::fixy::fs::OwnedFd> opened) noexcept -> Opened { return std::move(opened).consume(); },
            [](::foundation::os::Errno error) noexcept -> Opened { return ::fixy::err(error); });
}

}  // namespace detail

template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::crucible::cipher::durable::CtxFitsDurableMint<Ctx, warm_writer_stance, Extras...>
[[nodiscard]] auto mint_warm_writer(Ctx const& ctx, Path<source::Sanitized> const& path, ::mode_t perms) noexcept {
    using Minted = ::fixy::Result<::fixy::Linear<CipherDurableHandle<warm_writer_stance>>, ::foundation::os::Errno>;
    return detail::open_with_stance_<warm_writer_stance, Extras...>(ctx, path, perms)
        .match(
            [](::fixy::fs::OwnedFd fd) noexcept -> Minted {
                return ::fixy::mint_linear<CipherDurableHandle<warm_writer_stance>>(
                    CipherDurableHandle<warm_writer_stance>{std::move(fd)});
            },
            [](::foundation::os::Errno error) noexcept -> Minted { return ::fixy::err(error); });
}

template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::crucible::cipher::durable::CtxFitsDurableMint<Ctx, cold_writer_stance, Extras...>
[[nodiscard]] auto mint_cold_writer(Ctx const& ctx, Path<source::Sanitized> const& path, ::mode_t perms) noexcept {
    using Minted = ::fixy::Result<::fixy::Linear<CipherDurableHandle<cold_writer_stance>>, ::foundation::os::Errno>;
    return detail::open_with_stance_<cold_writer_stance, Extras...>(ctx, path, perms)
        .match(
            [](::fixy::fs::OwnedFd fd) noexcept -> Minted {
                return ::fixy::mint_linear<CipherDurableHandle<cold_writer_stance>>(
                    CipherDurableHandle<cold_writer_stance>{std::move(fd)});
            },
            [](::foundation::os::Errno error) noexcept -> Minted { return ::fixy::err(error); });
}

// This mint opens only the temporary file that becomes the new HEAD.
// The caller opens the parent directory separately with open_dirfd and
// calls sync on that, which is what flushes the directory entry after
// the rename.
template <typename... Extras, eff::IsExecCtx Ctx>
    requires ::crucible::cipher::durable::CtxFitsDurableMint<Ctx, head_advance_stance, Extras...>
[[nodiscard]] auto mint_head_advancer(Ctx const& ctx, Path<source::Sanitized> const& path, ::mode_t perms) noexcept {
    using Minted = ::fixy::Result<::fixy::Linear<CipherDurableHandle<head_advance_stance>>, ::foundation::os::Errno>;
    return detail::open_with_stance_<head_advance_stance, Extras...>(ctx, path, perms)
        .match(
            [](::fixy::fs::OwnedFd fd) noexcept -> Minted {
                return ::fixy::mint_linear<CipherDurableHandle<head_advance_stance>>(
                    CipherDurableHandle<head_advance_stance>{std::move(fd)});
            },
            [](::foundation::os::Errno error) noexcept -> Minted { return ::fixy::err(error); });
}

}  // namespace crucible::cipher::durable
