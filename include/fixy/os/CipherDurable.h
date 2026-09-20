#pragma once

// The three durable writers the Cipher uses: a warm snapshot writer, a
// cold archive writer, and the writer that advances HEAD.  Each is a
// stance — one open mode, one flag, one sync, one commit, pinned as
// types — and a handle over a descriptor that was opened with exactly
// that stance's flags.
//
// Old spelling: include/crucible/fixy/CipherDurable.h.
//
// Deviations, each deliberate:
//
//  1. cold_writer_stance pins atomicity::Rename.  It pinned LinkAtomic,
//     which was a hard ENOSYS in the old commit body, so every cold
//     commit failed before a filesystem was consulted.  A probe against
//     the old unit on this host: LinkAtomic errno 38 unconditionally,
//     Rename OK, RenameAt2NoReplace OK on a fresh target and EEXIST on
//     an existing one.  That is the ENOSYS commit path, and it is
//     closed.
//
//  2. Three concepts that were textually identical apart from their
//     names are one CtxFitsDurableMint<Ctx, Stance, Extras...>, and it
//     folds the stance's own atoms into fs::CtxFitsFileMint rather than
//     naming the row by hand, so the known-tag checks and the derived
//     row apply to a stance the same way they apply to a bare pack.
//
//  3. A DurableStance concept constrains the handle and the mints.  A
//     stance has to name all four types, and each must be a tag the
//     file surface knows.  The two stances that had no flag name
//     flag::CloseOnExec, which mint_file folds in unconditionally, so
//     naming it changes no bit.
//
//  4. The handle wraps fixy::fs::OwnedFd rather than the old FileHandle.
//     Its constructor stays private with the three mints as its sole
//     friends: an OwnedFd is evidence of a descriptor the caller owns,
//     not of the flags it was opened with, and the stance is a claim
//     about those flags.  Only the mint that chose them can make it.

#include <fixy/Path.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>
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
} && ::fixy::fs::is_known_open_mode_v<typename S::mode_type> && ::fixy::fs::is_known_flag_v<typename S::flag_type>
    && ::fixy::fs::is_known_sync_op_v<typename S::sync_op_type>
    && ::fixy::fs::is_known_atomicity_v<typename S::atomicity_type>;

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
concept CtxFitsDurableMint =
    DurableStance<Stance>
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

    // A default-constructed handle is closed.  Only a mint opens one.
    CipherDurableHandle() noexcept = default;

    CipherDurableHandle(const CipherDurableHandle&) = delete("a descriptor is unique; copy would double-close");
    CipherDurableHandle& operator=(const CipherDurableHandle&) = delete("a descriptor is unique; copy would double-close");
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
    auto fd = ::fixy::fs::mint_file<atom_fs::mode<typename Stance::mode_type>,
                                    atom_fs::with_flag<typename Stance::flag_type>, Extras...>(ctx, std::move(path),
                                                                                              perms);
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

namespace fixy::cipher::durable::detail::durable_surface_invariants {

static_assert(DurableStance<warm_writer_stance>);
static_assert(DurableStance<cold_writer_stance>);
static_assert(DurableStance<head_advance_stance>);

static_assert(std::is_same_v<cold_writer_stance::atomicity_type, atomicity::Rename>,
              "cold_writer_stance pins Rename.  It pinned LinkAtomic, which was a hard ENOSYS, so every cold commit "
              "failed before a filesystem was consulted.");
static_assert(std::is_same_v<warm_writer_stance::atomicity_type, atomicity::RenameAt2NoReplace>);
static_assert(std::is_same_v<head_advance_stance::sync_op_type, sync_op::FsyncParentDir>);
static_assert(std::is_same_v<cold_writer_stance::flag_type, flag::FullSync>);

// A stance missing an axis, or naming a tag the file surface does not
// know, is not a stance.
struct NoAtomicity final {
    using mode_type = open_mode::WriteCreate;
    using flag_type = flag::CloseOnExec;
    using sync_op_type = sync_op::Fsync;
};
struct UnknownMode final {};
struct StanceOverUnknownMode final {
    using mode_type = UnknownMode;
    using flag_type = flag::CloseOnExec;
    using sync_op_type = sync_op::Fsync;
    using atomicity_type = atomicity::Rename;
};
static_assert(!DurableStance<NoAtomicity>);
static_assert(!DurableStance<StanceOverUnknownMode>);

static_assert(detail::extras_engage_mode_v<atom_fs::mode<open_mode::WriteCreate>>);
static_assert(!detail::extras_engage_mode_v<>);
static_assert(!detail::extras_engage_mode_v<atom_fs::with_flag<flag::NoFollow>>);
static_assert(detail::extras_engage_durable_v<atom_fs::durable<sync_op::Fsync>>);
static_assert(!detail::extras_engage_durable_v<atom_fs::with_flag<flag::Direct>>);
static_assert(detail::extras_engage_atomic_write_v<atom_fs::atomic_write<atomicity::Rename>>);
static_assert(!detail::extras_engage_atomic_write_v<atom_fs::with_flag<flag::NoFollow>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsDurableMint<IoBlockCtx, warm_writer_stance>);
static_assert(CtxFitsDurableMint<IoBlockCtx, cold_writer_stance, atom_fs::with_flag<flag::NoFollow>>);
static_assert(!CtxFitsDurableMint<IoOnlyCtx, warm_writer_stance>, "a context without Block cannot open.");
static_assert(!CtxFitsDurableMint<IoBlockCtx, warm_writer_stance, atom_fs::mode<open_mode::WriteCreate>>,
              "an extra that engages the mode the stance pins is refused.");
static_assert(!CtxFitsDurableMint<IoBlockCtx, cold_writer_stance, atom_fs::durable<sync_op::Fdatasync>>);
static_assert(!CtxFitsDurableMint<IoBlockCtx, head_advance_stance, atom_fs::atomic_write<atomicity::Rename>>);
static_assert(!CtxFitsDurableMint<IoBlockCtx, StanceOverUnknownMode>);

// The handle's constructor over a descriptor is private and the three
// mints are its sole friends.  Checked from a scope none of them
// befriends.
using WarmHandle = CipherDurableHandle<warm_writer_stance>;
static_assert(std::is_default_constructible_v<WarmHandle>);
static_assert(std::is_move_constructible_v<WarmHandle>);
static_assert(!std::is_copy_constructible_v<WarmHandle>);
static_assert(!std::is_copy_assignable_v<WarmHandle>);
static_assert(std::is_nothrow_destructible_v<WarmHandle>);
static_assert(!std::is_constructible_v<WarmHandle, ::fixy::fs::OwnedFd&&>,
              "An OwnedFd is evidence of a descriptor, not of the flags it was opened with, and the stance is a "
              "claim about those flags.  Only the mint that chose them can build the handle.");

}  // namespace fixy::cipher::durable::detail::durable_surface_invariants
