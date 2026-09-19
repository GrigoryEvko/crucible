// SPDX-License-Identifier: BUSL-1.1
#pragma once
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/Cipher.h>
#include <crucible/fixy/Fs.h>
#include <crucible/handles/FileHandle.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/Path.h>

namespace crucible::fixy::cipher::durable {

namespace open_mode = ::crucible::fixy::fs::open_mode;
namespace flag = ::crucible::fixy::fs::flag;
namespace sync_op = ::crucible::fixy::fs::sync_op;
namespace atomicity = ::crucible::fixy::fs::atomicity;
namespace grant_fs = ::crucible::fixy::grant::fs;

template <typename Source>
using Path = ::crucible::safety::Path<Source>;

struct warm_writer_stance final {
    using mode_type = open_mode::WriteTruncate;
    using sync_op_type = sync_op::Fdatasync;
    using atomicity_type = atomicity::RenameAt2NoReplace;
};

struct cold_writer_stance final {
    using mode_type = open_mode::WriteCreate;
    using flag_type = flag::FullSync;  // O_SYNC writes
    using sync_op_type = sync_op::Fsync;
    using atomicity_type = atomicity::LinkAtomic;
};

struct head_advance_stance final {
    using mode_type = open_mode::WriteCreate;
    using sync_op_type = sync_op::FsyncParentDir;
    using atomicity_type = atomicity::RenameAt2NoReplace;
};

// A stance pins the mode, durability and atomicity axes, so caller-supplied
// extras must not engage them. Composing a shadowing extra would still fail,
// but one layer deeper on a duplicate-grant clause. Refusing here points the
// diagnostic at the rule the caller actually violated.

namespace detail {

template <typename G>
struct is_fs_mode_grant : std::false_type {};
template <typename Mode>
struct is_fs_mode_grant<grant_fs::mode<Mode>> : std::true_type {};

template <typename G>
inline constexpr bool is_fs_mode_grant_v = is_fs_mode_grant<G>::value;

template <typename... Grants>
inline constexpr bool extras_engage_mode_v = (is_fs_mode_grant_v<Grants> || ...);

template <typename G>
struct is_fs_durable_grant : std::false_type {};
template <typename SyncOp>
struct is_fs_durable_grant<grant_fs::durable<SyncOp>> : std::true_type {};

template <typename G>
inline constexpr bool is_fs_durable_grant_v = is_fs_durable_grant<G>::value;

template <typename... Grants>
inline constexpr bool extras_engage_durable_v = (is_fs_durable_grant_v<Grants> || ...);

template <typename G>
struct is_fs_atomic_write_grant : std::false_type {};
template <typename Atomicity>
struct is_fs_atomic_write_grant<grant_fs::atomic_write<Atomicity>> : std::true_type {};

template <typename G>
inline constexpr bool is_fs_atomic_write_grant_v = is_fs_atomic_write_grant<G>::value;

template <typename... Grants>
inline constexpr bool extras_engage_atomic_write_v = (is_fs_atomic_write_grant_v<Grants> || ...);

}  // namespace detail

template <typename Stance>
class [[nodiscard]] CipherDurableHandle final {
public:
    // A default-constructed handle is closed. Only a mint opens one.
    CipherDurableHandle() noexcept = default;

    CipherDurableHandle(const CipherDurableHandle&) = delete("fd is unique; copy would double-close on destruction");
    CipherDurableHandle&
    operator=(const CipherDurableHandle&) = delete("fd is unique; copy would double-close on destruction");

    CipherDurableHandle(CipherDurableHandle&&) noexcept = default;
    CipherDurableHandle& operator=(CipherDurableHandle&&) noexcept = default;

    // The defaulted destructor is sufficient. The inner FileHandle closes the
    // descriptor when it drops.
    ~CipherDurableHandle() noexcept = default;

    [[nodiscard]] bool is_open() const noexcept { return handle_.is_open(); }
    [[nodiscard]] int get() const noexcept { return handle_.get(); }

    template <::crucible::effects::IsExecCtx Ctx>
    [[nodiscard]] std::expected<void, std::error_code> sync(Ctx const& ctx) noexcept {
        return ::crucible::fixy::fs::sync<typename Stance::sync_op_type>(ctx, handle_);
    }

    template <::crucible::effects::IsExecCtx Ctx>
    [[nodiscard]] std::expected<void, std::error_code>
    commit_atomic(Ctx const& ctx, Path<::crucible::safety::source::Sanitized> tmp_path,
                  Path<::crucible::safety::source::Sanitized> target_path) noexcept {
        return ::crucible::fixy::fs::commit_atomic<typename Stance::atomicity_type>(ctx, tmp_path, target_path);
    }

    template <typename... Extras_, ::crucible::effects::IsExecCtx Ctx_>
    friend std::expected<::crucible::safety::Linear<CipherDurableHandle<warm_writer_stance>>, std::error_code>
    mint_warm_writer(Ctx_ const&, Path<::crucible::safety::source::Sanitized>, ::mode_t) noexcept;

    template <typename... Extras_, ::crucible::effects::IsExecCtx Ctx_>
    friend std::expected<::crucible::safety::Linear<CipherDurableHandle<cold_writer_stance>>, std::error_code>
    mint_cold_writer(Ctx_ const&, Path<::crucible::safety::source::Sanitized>, ::mode_t) noexcept;

    template <typename... Extras_, ::crucible::effects::IsExecCtx Ctx_>
    friend std::expected<::crucible::safety::Linear<CipherDurableHandle<head_advance_stance>>, std::error_code>
    mint_head_advancer(Ctx_ const&, Path<::crucible::safety::source::Sanitized>, ::mode_t) noexcept;

private:
    explicit CipherDurableHandle(::crucible::safety::FileHandle h) noexcept : handle_{std::move(h)} {}

    ::crucible::safety::FileHandle handle_{};
};

template <typename Ctx, typename... Extras>
concept CtxFitsWarmWriterMint =
    ::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx> && !detail::extras_engage_mode_v<Extras...>
    && !detail::extras_engage_durable_v<Extras...> && !detail::extras_engage_atomic_write_v<Extras...>;

template <typename Ctx, typename... Extras>
concept CtxFitsColdWriterMint =
    ::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx> && !detail::extras_engage_mode_v<Extras...>
    && !detail::extras_engage_durable_v<Extras...> && !detail::extras_engage_atomic_write_v<Extras...>;

template <typename Ctx, typename... Extras>
concept CtxFitsHeadAdvancerMint =
    ::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx> && !detail::extras_engage_mode_v<Extras...>
    && !detail::extras_engage_durable_v<Extras...> && !detail::extras_engage_atomic_write_v<Extras...>;

template <typename... Extras, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsWarmWriterMint<Ctx, Extras...>
[[nodiscard]] inline std::expected<::crucible::safety::Linear<CipherDurableHandle<warm_writer_stance>>, std::error_code>
mint_warm_writer(Ctx const& ctx, Path<::crucible::safety::source::Sanitized> path, ::mode_t perms = 0644) noexcept {
    auto fh_result = ::crucible::fixy::fs::mint_file<grant_fs::mode<typename warm_writer_stance::mode_type>, Extras...>(
        ctx, path, perms);
    if (!fh_result) {
        return std::unexpected{fh_result.error()};
    }
    return ::crucible::safety::Linear<CipherDurableHandle<warm_writer_stance>>{
        CipherDurableHandle<warm_writer_stance>{std::move(*fh_result).consume()}};
}

template <typename... Extras, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsColdWriterMint<Ctx, Extras...>
[[nodiscard]] inline std::expected<::crucible::safety::Linear<CipherDurableHandle<cold_writer_stance>>, std::error_code>
mint_cold_writer(Ctx const& ctx, Path<::crucible::safety::source::Sanitized> path, ::mode_t perms = 0644) noexcept {
    auto fh_result =
        ::crucible::fixy::fs::mint_file<grant_fs::mode<typename cold_writer_stance::mode_type>,
                                        grant_fs::with_flag<typename cold_writer_stance::flag_type>, Extras...>(
            ctx, path, perms);
    if (!fh_result) {
        return std::unexpected{fh_result.error()};
    }
    return ::crucible::safety::Linear<CipherDurableHandle<cold_writer_stance>>{
        CipherDurableHandle<cold_writer_stance>{std::move(*fh_result).consume()}};
}

// This mint opens only the temporary file that becomes the new HEAD. The
// caller opens the parent directory separately and calls sync on that handle,
// which is what flushes the directory entry after the rename.

template <typename... Extras, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsHeadAdvancerMint<Ctx, Extras...>
[[nodiscard]] inline std::expected<::crucible::safety::Linear<CipherDurableHandle<head_advance_stance>>,
                                   std::error_code>
mint_head_advancer(Ctx const& ctx, Path<::crucible::safety::source::Sanitized> path, ::mode_t perms = 0644) noexcept {
    auto fh_result =
        ::crucible::fixy::fs::mint_file<grant_fs::mode<typename head_advance_stance::mode_type>, Extras...>(ctx, path,
                                                                                                            perms);
    if (!fh_result) {
        return std::unexpected{fh_result.error()};
    }
    return ::crucible::safety::Linear<CipherDurableHandle<head_advance_stance>>{
        CipherDurableHandle<head_advance_stance>{std::move(*fh_result).consume()}};
}

namespace selftest {

static_assert(std::is_empty_v<warm_writer_stance>, "warm_writer_stance must be empty (phantom type)");
static_assert(std::is_empty_v<cold_writer_stance>, "cold_writer_stance must be empty (phantom type)");
static_assert(std::is_empty_v<head_advance_stance>, "head_advance_stance must be empty (phantom type)");

static_assert(std::is_same_v<warm_writer_stance::sync_op_type, sync_op::Fdatasync>,
              "warm stance must pin sync_op::Fdatasync");
static_assert(std::is_same_v<warm_writer_stance::atomicity_type, atomicity::RenameAt2NoReplace>,
              "warm stance must pin atomicity::RenameAt2NoReplace");
static_assert(std::is_same_v<cold_writer_stance::sync_op_type, sync_op::Fsync>, "cold stance must pin sync_op::Fsync");
static_assert(std::is_same_v<cold_writer_stance::atomicity_type, atomicity::LinkAtomic>,
              "cold stance must pin atomicity::LinkAtomic");
static_assert(std::is_same_v<head_advance_stance::sync_op_type, sync_op::FsyncParentDir>,
              "head stance must pin sync_op::FsyncParentDir");
static_assert(std::is_same_v<head_advance_stance::atomicity_type, atomicity::RenameAt2NoReplace>,
              "head stance must pin atomicity::RenameAt2NoReplace");

static_assert(detail::extras_engage_mode_v<grant_fs::mode<open_mode::WriteCreate>>,
              "extras_engage_mode_v must be true for a mode<> grant");
static_assert(!detail::extras_engage_mode_v<>, "extras_engage_mode_v must be false for empty pack");
static_assert(!detail::extras_engage_mode_v<grant_fs::with_flag<flag::NoFollow>>,
              "extras_engage_mode_v must be false for with_flag<> grant");

static_assert(detail::extras_engage_durable_v<grant_fs::durable<sync_op::Fsync>>,
              "extras_engage_durable_v must be true for a durable<> grant");
static_assert(!detail::extras_engage_durable_v<grant_fs::with_flag<flag::Direct>>,
              "extras_engage_durable_v must be false for with_flag<> grant");

static_assert(detail::extras_engage_atomic_write_v<grant_fs::atomic_write<atomicity::Rename>>,
              "extras_engage_atomic_write_v must be true for atomic_write<>");
static_assert(!detail::extras_engage_atomic_write_v<grant_fs::with_flag<flag::NoFollow>>,
              "extras_engage_atomic_write_v must be false for with_flag<>");

static_assert(std::is_default_constructible_v<CipherDurableHandle<warm_writer_stance>>,
              "CipherDurableHandle<warm> must be default-constructible");
static_assert(std::is_move_constructible_v<CipherDurableHandle<warm_writer_stance>>,
              "CipherDurableHandle<warm> must be move-constructible");
static_assert(!std::is_copy_constructible_v<CipherDurableHandle<warm_writer_stance>>,
              "CipherDurableHandle<warm> must NOT be copy-constructible");
static_assert(!std::is_copy_assignable_v<CipherDurableHandle<warm_writer_stance>>,
              "CipherDurableHandle<warm> must NOT be copy-assignable");
static_assert(std::is_move_assignable_v<CipherDurableHandle<warm_writer_stance>>,
              "CipherDurableHandle<warm> must be move-assignable");
static_assert(std::is_nothrow_destructible_v<CipherDurableHandle<warm_writer_stance>>,
              "CipherDurableHandle<warm> must be nothrow-destructible");

}  // namespace selftest

}  // namespace crucible::fixy::cipher::durable
