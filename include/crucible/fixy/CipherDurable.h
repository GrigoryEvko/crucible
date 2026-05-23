// SPDX-License-Identifier: BUSL-1.1
#pragma once
// ============================================================================
// FIXY-V-228 — fixy/CipherDurable.h — three Cipher durability §XXI mints
// ============================================================================
//
// V-228 ships the §XXI ctx-bound mint factories that V-227's composite
// stances (CipherWarmWriterStance / CipherColdWriterStance /
// HeadAdvanceStance) gate.  Each mint:
//
//   (1) Concept-gates the caller via `CtxFits*Mint<Ctx, Extras...>`
//       which folds in `CtxAdmitsIoBlock<Ctx>` (the V-224 row-membership
//       check) PLUS three extras-do-not-shadow-stance-pinned-axis
//       predicates: the stance pins `mode<>`, `durable<>`, and
//       `atomic_write<>` (the load-bearing axes of the stance posture),
//       so extras may engage only `with_flag<>` and any non-fs:: grant
//       axis the caller wants to add.  Trying to override e.g.
//       `mode<>` via extras would silently disagree with the stance
//       posture; we refuse at the type system.
//
//   (2) Composes the stance's pinned grant set with the caller's
//       `Extras...` pack and delegates to V-224 `mint_file<>` for the
//       actual ::open() syscall.
//
//   (3) Wraps the resulting `FileHandle` in a phantom-typed
//       `CipherDurableHandle<Stance>` so downstream operations (sync,
//       commit_atomic) can dispatch on the stance's pinned
//       `sync_op_type` and `atomicity_type` — the caller CANNOT
//       accidentally call `sync<Fsync>` on a warm-tier handle because
//       the handle's `sync()` method is inherent and dispatches via
//       `Stance::sync_op_type`.
//
//   (4) Returns `std::expected<Linear<CipherDurableHandle<Stance>>,
//       std::error_code>` so the outer `Linear<>` enforces single-
//       consume at the Cipher boundary (matches V-225's
//       `Linear<OwnedMmap<Tag, Prot, Share>>` discipline).
//
// Per §XXI carve-out, the mints drop `constexpr` — they invoke
// `::open()` (genuine resource synthesis); `constexpr` would lie
// about the runtime cost.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//   InitSafe   : All CipherDurableHandle fields have NSDMI (FileHandle
//                default-constructs to closed state).
//   TypeSafe   : Stance phantom-tag is a strong type; the inherent
//                sync() and commit_atomic() dispatch via the stance's
//                pinned enumerator — no implicit conversions, no
//                accidental cross-stance enumerator mixing.
//   NullSafe   : sync() / commit_atomic() check is_open() and return
//                EBADF on a moved-from handle.
//   MemSafe    : Move-only with delete("…") reasons on copy ctor/assign;
//                inner FileHandle's RAII closes the fd on drop;
//                outer Linear<> enforces single-consume at the
//                boundary.
//   BorrowSafe : One Linear handle owns the FileHandle; no aliased state.
//   ThreadSafe : No cross-thread state in the mint or handle.
//   LeakSafe   : FileHandle destructor closes; if the mint fails after
//                ::open() succeeds (no failure path possible at present,
//                since mint_file returns immediately on success), the
//                handle's destructor would close cleanly.
//   DetSafe    : Stance-driven dispatch is compile-time-stable;
//                identical (Stance, args) always produces identical
//                runtime behavior.

#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Cipher.h>   // V-227 — stance concepts
#include <crucible/fixy/Fs.h>       // V-224 — mint_file, sync<>, commit_atomic<>
#include <crucible/handles/FileHandle.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Path.h>

namespace crucible::fixy::cipher::durable {

// Re-bind enumerator namespaces locally for stance-tag readability.
namespace open_mode = ::crucible::fixy::fs::open_mode;
namespace flag      = ::crucible::fixy::fs::flag;
namespace sync_op   = ::crucible::fixy::fs::sync_op;
namespace atomicity = ::crucible::fixy::fs::atomicity;
namespace grant_fs  = ::crucible::fixy::grant::fs;

template <typename Source>
using Path = ::crucible::safety::Path<Source>;

// ── Stance tag types — pinned enumerators per composite stance ──────
//
// Each stance tag is a phantom marker type carrying the
// load-bearing enumerators for ONE durability posture.  These tags
// parameterize `CipherDurableHandle<Stance>` and drive the
// stance-dispatched `sync()` / `commit_atomic()` member functions.
//
// The tags are `final` and have no instances — they are pure
// type-level records.

struct warm_writer_stance final {
    using mode_type      = open_mode::WriteTruncate;
    using sync_op_type   = sync_op::Fdatasync;
    using atomicity_type = atomicity::RenameAt2NoReplace;
};

struct cold_writer_stance final {
    using mode_type      = open_mode::WriteCreate;
    using flag_type      = flag::FullSync;     // O_SYNC writes
    using sync_op_type   = sync_op::Fsync;
    using atomicity_type = atomicity::LinkAtomic;
};

struct head_advance_stance final {
    using mode_type      = open_mode::WriteCreate;
    using sync_op_type   = sync_op::FsyncParentDir;
    using atomicity_type = atomicity::RenameAt2NoReplace;
};

// ── Detail layer — extras-shadow-stance-pinned-axis predicates ──────
//
// Each mint's ctx-bound concept refuses caller-supplied Extras that
// would shadow the stance's pinned axes.  Without these checks, a
// caller could pass `Extras = mode<WriteCreate>` to mint_warm_writer
// — the call would compose the stance's `mode<WriteTruncate>` with
// the caller's `mode<WriteCreate>` and `mint_file`'s
// `!has_duplicate_mode_v<>` clause would fail at a deeper layer of
// the diagnostic stack.  We catch the misuse one layer up so the
// diagnostic points directly at the rule the caller violated.

namespace detail {

template <typename G>
struct is_fs_mode_grant : std::false_type {};
template <typename Mode>
struct is_fs_mode_grant<grant_fs::mode<Mode>> : std::true_type {};

template <typename G>
inline constexpr bool is_fs_mode_grant_v = is_fs_mode_grant<G>::value;

template <typename... Grants>
inline constexpr bool extras_engage_mode_v =
    (is_fs_mode_grant_v<Grants> || ...);

template <typename G>
struct is_fs_durable_grant : std::false_type {};
template <typename SyncOp>
struct is_fs_durable_grant<grant_fs::durable<SyncOp>> : std::true_type {};

template <typename G>
inline constexpr bool is_fs_durable_grant_v = is_fs_durable_grant<G>::value;

template <typename... Grants>
inline constexpr bool extras_engage_durable_v =
    (is_fs_durable_grant_v<Grants> || ...);

template <typename G>
struct is_fs_atomic_write_grant : std::false_type {};
template <typename Atomicity>
struct is_fs_atomic_write_grant<grant_fs::atomic_write<Atomicity>>
    : std::true_type {};

template <typename G>
inline constexpr bool is_fs_atomic_write_grant_v =
    is_fs_atomic_write_grant<G>::value;

template <typename... Grants>
inline constexpr bool extras_engage_atomic_write_v =
    (is_fs_atomic_write_grant_v<Grants> || ...);

}  // namespace detail

// ── CipherDurableHandle<Stance> — phantom-typed RAII wrapper ────────
//
// Wraps a `safety::FileHandle` with a phantom-typed `Stance` tag.
// The stance pinning ensures downstream sync() / commit_atomic() use
// the correct enumerator for the durability posture — a warm-tier
// handle's sync() ALWAYS calls fdatasync; a cold-tier handle's sync()
// ALWAYS calls fsync.  Cross-mixing is structurally impossible.
//
// Move-only (delete("...") on copy).  The outer `safety::Linear<>`
// the mint returns enforces single-consume at the boundary; the
// inner FileHandle's RAII closes the fd on drop.

template <typename Stance>
class [[nodiscard]] CipherDurableHandle final {
public:
    // Default-construct = closed/invalid (FileHandle's NSDMI sets fd_=-1).
    // Mints construct via the explicit FileHandle ctor below.
    CipherDurableHandle() noexcept = default;

    // Move-only — copying would double-close the underlying fd on drop.
    CipherDurableHandle(const CipherDurableHandle&)
        = delete("fd is unique; copy would double-close on destruction");
    CipherDurableHandle& operator=(const CipherDurableHandle&)
        = delete("fd is unique; copy would double-close on destruction");

    CipherDurableHandle(CipherDurableHandle&&) noexcept            = default;
    CipherDurableHandle& operator=(CipherDurableHandle&&) noexcept = default;

    // FileHandle's RAII closes on drop.
    ~CipherDurableHandle() noexcept = default;

    // Inspection accessors.
    [[nodiscard]] bool is_open() const noexcept { return handle_.is_open(); }
    [[nodiscard]] int  get()     const noexcept { return handle_.get(); }

    // Stance-dispatched sync.  Uses `Stance::sync_op_type` —
    // warm → fdatasync, cold → fsync, head_advance → fsync (on dirfd).
    // Returns the V-224 `sync<>` result verbatim.
    template <::crucible::effects::IsExecCtx Ctx>
    [[nodiscard]] std::expected<void, std::error_code>
    sync(Ctx const& ctx) noexcept {
        return ::crucible::fixy::fs::sync<typename Stance::sync_op_type>(
            ctx, handle_);
    }

    // Stance-dispatched commit_atomic.  Uses `Stance::atomicity_type`
    // — warm/head → RENAME_NOREPLACE, cold → linkat AT_EMPTY_PATH.
    // Both source and target MUST be Path<Sanitized>.
    template <::crucible::effects::IsExecCtx Ctx>
    [[nodiscard]] std::expected<void, std::error_code>
    commit_atomic(Ctx const&                                    ctx,
                  Path<::crucible::safety::source::Sanitized>   tmp_path,
                  Path<::crucible::safety::source::Sanitized>   target_path) noexcept {
        return ::crucible::fixy::fs::commit_atomic<
            typename Stance::atomicity_type>(ctx, tmp_path, target_path);
    }

    // Friends — only the mints can construct from a raw FileHandle.
    // This keeps `CipherDurableHandle<Stance>` minted-by-§XXI-only.

    template <typename... Extras_, ::crucible::effects::IsExecCtx Ctx_>
    friend std::expected<
        ::crucible::safety::Linear<CipherDurableHandle<warm_writer_stance>>,
        std::error_code>
    mint_warm_writer(Ctx_ const&,
                     Path<::crucible::safety::source::Sanitized>,
                     ::mode_t) noexcept;

    template <typename... Extras_, ::crucible::effects::IsExecCtx Ctx_>
    friend std::expected<
        ::crucible::safety::Linear<CipherDurableHandle<cold_writer_stance>>,
        std::error_code>
    mint_cold_writer(Ctx_ const&,
                     Path<::crucible::safety::source::Sanitized>,
                     ::mode_t) noexcept;

    template <typename... Extras_, ::crucible::effects::IsExecCtx Ctx_>
    friend std::expected<
        ::crucible::safety::Linear<CipherDurableHandle<head_advance_stance>>,
        std::error_code>
    mint_head_advancer(Ctx_ const&,
                       Path<::crucible::safety::source::Sanitized>,
                       ::mode_t) noexcept;

private:
    explicit CipherDurableHandle(::crucible::safety::FileHandle h) noexcept
        : handle_{std::move(h)} {}

    ::crucible::safety::FileHandle handle_{};
};

// ── §XXI ctx-bound mint gates — single-concept requires per mint ────
//
// Each concept folds:
//   (1) `CtxAdmitsIoBlock<Ctx>` — caller's row admits IO + Block.
//   (2) Extras MUST NOT engage `mode<>` — stance pins it.
//   (3) Extras MUST NOT engage `durable<>` — stance pins it.
//   (4) Extras MUST NOT engage `atomic_write<>` — stance pins it.

template <typename Ctx, typename... Extras>
concept CtxFitsWarmWriterMint =
       ::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx>
    && !detail::extras_engage_mode_v<Extras...>
    && !detail::extras_engage_durable_v<Extras...>
    && !detail::extras_engage_atomic_write_v<Extras...>;

template <typename Ctx, typename... Extras>
concept CtxFitsColdWriterMint =
       ::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx>
    && !detail::extras_engage_mode_v<Extras...>
    && !detail::extras_engage_durable_v<Extras...>
    && !detail::extras_engage_atomic_write_v<Extras...>;

template <typename Ctx, typename... Extras>
concept CtxFitsHeadAdvancerMint =
       ::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx>
    && !detail::extras_engage_mode_v<Extras...>
    && !detail::extras_engage_durable_v<Extras...>
    && !detail::extras_engage_atomic_write_v<Extras...>;

// ── §XXI mint factories ──────────────────────────────────────────────
//
// §XXI carve-out: each mint drops `constexpr` — they invoke
// `::open()` (genuine resource synthesis); `constexpr` would lie
// about the runtime cost.  Pattern matches V-224 `mint_file` /
// V-225 `mint_mmap` / V-226 `mint_io_uring_ring`.

// ── mint_warm_writer ────────────────────────────────────────────────
//
// In-progress snapshot writer.  Opens the path with O_WRONLY |
// O_CREAT | O_TRUNC (from `mode<WriteTruncate>`) plus the caller's
// optional `with_flag<>` extras.  Returns a phantom-typed
// `CipherDurableHandle<warm_writer_stance>` whose sync() dispatches
// to fdatasync and whose commit_atomic() uses RENAME_NOREPLACE.

template <typename... Extras, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsWarmWriterMint<Ctx, Extras...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<CipherDurableHandle<warm_writer_stance>>,
    std::error_code>
mint_warm_writer(Ctx const&                                  ctx,
                 Path<::crucible::safety::source::Sanitized> path,
                 ::mode_t                                    perms = 0644) noexcept {
    auto fh_result = ::crucible::fixy::fs::mint_file<
        grant_fs::mode<typename warm_writer_stance::mode_type>,
        Extras...
    >(ctx, path, perms);
    if (!fh_result) {
        return std::unexpected{fh_result.error()};
    }
    // Unwrap the inner Linear<FileHandle>, re-wrap inside the phantom-
    // typed CipherDurableHandle, then re-wrap in the outer Linear<>.
    return ::crucible::safety::Linear<
        CipherDurableHandle<warm_writer_stance>>{
        CipherDurableHandle<warm_writer_stance>{
            std::move(*fh_result).consume()}};
}

// ── mint_cold_writer ────────────────────────────────────────────────
//
// Cold-tier durable writer.  Opens the path with O_WRONLY | O_CREAT
// | O_SYNC (from `mode<WriteCreate>` + `with_flag<FullSync>`) plus
// the caller's optional extras.  Returns a phantom-typed
// `CipherDurableHandle<cold_writer_stance>` whose sync() dispatches
// to fsync and whose commit_atomic() uses linkat AT_EMPTY_PATH.

template <typename... Extras, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsColdWriterMint<Ctx, Extras...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<CipherDurableHandle<cold_writer_stance>>,
    std::error_code>
mint_cold_writer(Ctx const&                                  ctx,
                 Path<::crucible::safety::source::Sanitized> path,
                 ::mode_t                                    perms = 0644) noexcept {
    auto fh_result = ::crucible::fixy::fs::mint_file<
        grant_fs::mode<typename cold_writer_stance::mode_type>,
        grant_fs::with_flag<typename cold_writer_stance::flag_type>,
        Extras...
    >(ctx, path, perms);
    if (!fh_result) {
        return std::unexpected{fh_result.error()};
    }
    return ::crucible::safety::Linear<
        CipherDurableHandle<cold_writer_stance>>{
        CipherDurableHandle<cold_writer_stance>{
            std::move(*fh_result).consume()}};
}

// ── mint_head_advancer ──────────────────────────────────────────────
//
// HEAD pointer advance.  Opens the path with O_WRONLY | O_CREAT
// (from `mode<WriteCreate>`) plus the caller's optional extras.
// Returns a phantom-typed `CipherDurableHandle<head_advance_stance>`
// whose sync() dispatches to fsync (on the parent-dir fd the caller
// is responsible for) and whose commit_atomic() uses RENAME_NOREPLACE.
//
// Note: HEAD-advance callers conventionally `open()` the parent
// directory separately (via `mint_file<grant_fs::with_flag<flag::Path>
// >`) and call `sync()` on the dirfd handle to flush the directory
// entry post-rename.  This mint just opens the tmp file that will
// become the new HEAD.

template <typename... Extras, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsHeadAdvancerMint<Ctx, Extras...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<CipherDurableHandle<head_advance_stance>>,
    std::error_code>
mint_head_advancer(Ctx const&                                  ctx,
                   Path<::crucible::safety::source::Sanitized> path,
                   ::mode_t                                    perms = 0644) noexcept {
    auto fh_result = ::crucible::fixy::fs::mint_file<
        grant_fs::mode<typename head_advance_stance::mode_type>,
        Extras...
    >(ctx, path, perms);
    if (!fh_result) {
        return std::unexpected{fh_result.error()};
    }
    return ::crucible::safety::Linear<
        CipherDurableHandle<head_advance_stance>>{
        CipherDurableHandle<head_advance_stance>{
            std::move(*fh_result).consume()}};
}

// ── Self-test block ──────────────────────────────────────────────────
//
// Compile-time witnesses pinned by the sentinel TU
// (test_fixy_engaged.cpp transitively pulls Wrap.h → CipherDurable.h).

namespace selftest {

// Stance tags are final, type-level, sizeof-1 phantom markers.
static_assert(std::is_empty_v<warm_writer_stance>,
              "V-228: warm_writer_stance must be empty (phantom type)");
static_assert(std::is_empty_v<cold_writer_stance>,
              "V-228: cold_writer_stance must be empty (phantom type)");
static_assert(std::is_empty_v<head_advance_stance>,
              "V-228: head_advance_stance must be empty (phantom type)");

// Stance pinned enumerator types match the V-227 stance concepts'
// expectations.  If V-227 were updated to require a different
// enumerator, the corresponding `IsCipher*Stance` static_assert
// in fixy/Cipher.h would still hold for V-228's stance grants
// because we project the stance tag's typedef into the grant pack.
static_assert(std::is_same_v<warm_writer_stance::sync_op_type,
                             sync_op::Fdatasync>,
              "V-228: warm stance must pin sync_op::Fdatasync");
static_assert(std::is_same_v<warm_writer_stance::atomicity_type,
                             atomicity::RenameAt2NoReplace>,
              "V-228: warm stance must pin atomicity::RenameAt2NoReplace");
static_assert(std::is_same_v<cold_writer_stance::sync_op_type,
                             sync_op::Fsync>,
              "V-228: cold stance must pin sync_op::Fsync");
static_assert(std::is_same_v<cold_writer_stance::atomicity_type,
                             atomicity::LinkAtomic>,
              "V-228: cold stance must pin atomicity::LinkAtomic");
static_assert(std::is_same_v<head_advance_stance::sync_op_type,
                             sync_op::FsyncParentDir>,
              "V-228: head stance must pin sync_op::FsyncParentDir");
static_assert(std::is_same_v<head_advance_stance::atomicity_type,
                             atomicity::RenameAt2NoReplace>,
              "V-228: head stance must pin atomicity::RenameAt2NoReplace");

// Extras-engagement predicates correctly identify shadowing grants.
static_assert(detail::extras_engage_mode_v<
                  grant_fs::mode<open_mode::WriteCreate>>,
              "V-228: extras_engage_mode_v must be true for a mode<> grant");
static_assert(!detail::extras_engage_mode_v<>,
              "V-228: extras_engage_mode_v must be false for empty pack");
static_assert(!detail::extras_engage_mode_v<
                  grant_fs::with_flag<flag::NoFollow>>,
              "V-228: extras_engage_mode_v must be false for with_flag<> grant");

static_assert(detail::extras_engage_durable_v<
                  grant_fs::durable<sync_op::Fsync>>,
              "V-228: extras_engage_durable_v must be true for a durable<> grant");
static_assert(!detail::extras_engage_durable_v<
                  grant_fs::with_flag<flag::Direct>>,
              "V-228: extras_engage_durable_v must be false for with_flag<> grant");

static_assert(detail::extras_engage_atomic_write_v<
                  grant_fs::atomic_write<atomicity::Rename>>,
              "V-228: extras_engage_atomic_write_v must be true for atomic_write<>");
static_assert(!detail::extras_engage_atomic_write_v<
                  grant_fs::with_flag<flag::NoFollow>>,
              "V-228: extras_engage_atomic_write_v must be false for with_flag<>");

// CipherDurableHandle properties: move-only (no copy), default-
// constructible (closed sentinel), nodiscard at the type level.
static_assert(std::is_default_constructible_v<
                  CipherDurableHandle<warm_writer_stance>>,
              "V-228: CipherDurableHandle<warm> must be default-constructible");
static_assert(std::is_move_constructible_v<
                  CipherDurableHandle<warm_writer_stance>>,
              "V-228: CipherDurableHandle<warm> must be move-constructible");
static_assert(!std::is_copy_constructible_v<
                  CipherDurableHandle<warm_writer_stance>>,
              "V-228: CipherDurableHandle<warm> must NOT be copy-constructible");
static_assert(!std::is_copy_assignable_v<
                  CipherDurableHandle<warm_writer_stance>>,
              "V-228: CipherDurableHandle<warm> must NOT be copy-assignable");
static_assert(std::is_move_assignable_v<
                  CipherDurableHandle<warm_writer_stance>>,
              "V-228: CipherDurableHandle<warm> must be move-assignable");
static_assert(std::is_nothrow_destructible_v<
                  CipherDurableHandle<warm_writer_stance>>,
              "V-228: CipherDurableHandle<warm> must be nothrow-destructible");

}  // namespace selftest

}  // namespace crucible::fixy::cipher::durable
