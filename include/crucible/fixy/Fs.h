#pragma once

#include <crucible/fixy/Grant.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/Path.h>
#include <crucible/safety/source/Path.h>

#include <crucible/handles/FileHandle.h>
#include <crucible/safety/Linear.h>

#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <system_error>
#include <type_traits>
#include <utility>

namespace crucible::fixy::fs {

namespace open_mode {
struct ReadOnly final {};
struct WriteCreate final {};
struct WriteAppend final {};
struct WriteTruncate final {};
struct ReadWrite final {};
// Reserved for the dirfd surface that atomicity::LinkAtomic needs.  Its
// path argument is the directory that receives the unnamed inode, so it
// does not compose with mint_file, which opens the file it is given;
// CtxFitsFileMint refuses it.  do_commit_atomic_impl returns ENOSYS for
// LinkAtomic until that surface is wired, and this tag is the placeholder
// for it.
struct TmpFile final {};
}  // namespace open_mode

template <typename Mode>
struct open_mode_flags;

template <>
struct open_mode_flags<open_mode::ReadOnly> : std::integral_constant<int, O_RDONLY> {};
template <>
struct open_mode_flags<open_mode::WriteCreate> : std::integral_constant<int, O_WRONLY | O_CREAT> {};
template <>
struct open_mode_flags<open_mode::WriteAppend> : std::integral_constant<int, O_WRONLY | O_CREAT | O_APPEND> {};
template <>
struct open_mode_flags<open_mode::WriteTruncate> : std::integral_constant<int, O_WRONLY | O_CREAT | O_TRUNC> {};
template <>
struct open_mode_flags<open_mode::ReadWrite> : std::integral_constant<int, O_RDWR | O_CREAT> {};
template <>
struct open_mode_flags<open_mode::TmpFile> : std::integral_constant<int, O_TMPFILE | O_RDWR> {};

template <typename Mode>
inline constexpr int open_mode_flags_v = open_mode_flags<Mode>::value;

namespace flag {
struct CloseOnExec final {};
struct NoFollow final {};
struct Directory final {};
struct DataSync final {};
struct FullSync final {};
struct Direct final {};
struct NonBlock final {};
struct Path final {};
}  // namespace flag

template <typename Flag>
struct flag_bits;

template <>
struct flag_bits<flag::CloseOnExec> : std::integral_constant<int, O_CLOEXEC> {};
template <>
struct flag_bits<flag::NoFollow> : std::integral_constant<int, O_NOFOLLOW> {};
template <>
struct flag_bits<flag::Directory> : std::integral_constant<int, O_DIRECTORY> {};
template <>
struct flag_bits<flag::DataSync> : std::integral_constant<int, O_DSYNC> {};
template <>
struct flag_bits<flag::FullSync> : std::integral_constant<int, O_SYNC> {};
template <>
struct flag_bits<flag::Direct> : std::integral_constant<int, O_DIRECT> {};
template <>
struct flag_bits<flag::NonBlock> : std::integral_constant<int, O_NONBLOCK> {};
template <>
struct flag_bits<flag::Path> : std::integral_constant<int, O_PATH> {};

template <typename Flag>
inline constexpr int flag_bits_v = flag_bits<Flag>::value;

namespace sync_op {
struct None final {};  // no syscall, page cache only
struct Fdatasync final {};
struct Fsync final {};
struct Msync final {};
struct FsyncParentDir final {};
}  // namespace sync_op

// Rename uses ::rename, where the last writer wins.  RenameAt2NoReplace uses
// renameat2(RENAME_NOREPLACE), which fails when the target exists instead of
// overwriting it.  LinkAtomic uses O_TMPFILE plus linkat(AT_EMPTY_PATH): the
// file is never visible under its name until the link succeeds, so no reader
// can observe a partial write.

namespace atomicity {
struct None final {};
struct Rename final {};
struct RenameAt2NoReplace final {};
struct LinkAtomic final {};
}  // namespace atomicity

template <typename Source>
using Path = ::crucible::safety::Path<Source>;

}  // namespace crucible::fixy::fs

// A which_dim specialization must appear syntactically inside namespace
// crucible::fixy::grant.  A nested namespace does not satisfy that rule.
namespace crucible::fixy::grant {

namespace fs {

template <typename Mode>
struct mode final : grant_base {};

template <typename Flag>
struct with_flag final : grant_base {};

template <typename SyncOp>
struct durable final : grant_base {};

template <typename Atomicity>
struct atomic_write final : grant_base {};

}  // namespace fs

template <typename Mode>
struct which_dim<fs::mode<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename Flag>
struct which_dim<fs::with_flag<Flag>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {
};
template <typename SyncOp>
struct which_dim<fs::durable<SyncOp>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {
};
template <typename Atomicity>
struct which_dim<fs::atomic_write<Atomicity>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::fs {

class [[nodiscard]] Dirfd {
    ::crucible::safety::FileHandle fd_;

public:
    Dirfd() noexcept = default;
    explicit Dirfd(::crucible::safety::FileHandle&& fd) noexcept : fd_{std::move(fd)} {}

    Dirfd(const Dirfd&) = delete("Dirfd holds an fd; copy would double-close");
    Dirfd& operator=(const Dirfd&) = delete("Dirfd holds an fd; copy would double-close");
    Dirfd(Dirfd&&) noexcept = default;
    Dirfd& operator=(Dirfd&&) noexcept = default;

    [[nodiscard]] int get() const noexcept { return fd_.get(); }
    [[nodiscard]] bool is_open() const noexcept { return fd_.is_open(); }
    [[nodiscard]] const ::crucible::safety::FileHandle& handle() const noexcept { return fd_; }
};

// The open, fdatasync, fsync, rename, renameat2 and linkat calls do not
// depend on any template parameter, so their bodies live in a translation
// unit rather than here.  Each call site then emits one call to the helper
// instead of an inlined syscall body per instantiation.  The templates that
// select among them stay inline because they dispatch with if constexpr on an
// explicit template parameter.

namespace detail::impl {

enum class SyncOpTag : ::std::uint8_t {
    Fdatasync,
    Fsync,
    FsyncParentDir,
    Msync,
};

enum class AtomicityTag : ::std::uint8_t {
    Rename,
    RenameAt2NoReplace,
    LinkAtomic,
    None,
};

// errno reaches the caller through std::system_category().
[[nodiscard]] ::std::expected<::crucible::safety::FileHandle, ::std::error_code>
do_open_impl(const char* path, int flags, ::mode_t perms) noexcept;

// Opens with O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY.
[[nodiscard]] ::std::expected<::crucible::safety::FileHandle, ::std::error_code>
do_open_dirfd_impl(const char* dir_path) noexcept;

// The caller checks the descriptor first.  Msync returns EINVAL: a
// memory-mapped flush goes through the mmap surface instead.
[[nodiscard]] ::std::expected<void, ::std::error_code> do_sync_impl(int fd, SyncOpTag op) noexcept;

// None succeeds without a syscall.  LinkAtomic returns ENOSYS because the
// O_TMPFILE descriptor channel it needs is not wired.
[[nodiscard]] ::std::expected<void, ::std::error_code> do_commit_atomic_impl(const char* tmp, const char* target,
                                                                             AtomicityTag atomicity) noexcept;

}  // namespace detail::impl

[[nodiscard]] inline std::expected<Dirfd, std::error_code> open_dirfd(const ::std::filesystem::path& dir) noexcept {
    auto fh = detail::impl::do_open_dirfd_impl(dir.c_str());
    if (!fh) {
        return std::unexpected{fh.error()};
    }
    return Dirfd{std::move(*fh)};
}

namespace detail {

// A grant with no specialization contributes 0.  That is deliberate: a
// durable or atomic_write grant declares intent for a later sync or commit
// call and adds nothing to the open flags.
template <typename G>
struct grant_open_flags : std::integral_constant<int, 0> {};

template <typename Mode>
struct grant_open_flags<::crucible::fixy::grant::fs::mode<Mode>>
    : std::integral_constant<int, open_mode_flags_v<Mode>> {};

template <typename Flag>
struct grant_open_flags<::crucible::fixy::grant::fs::with_flag<Flag>> : std::integral_constant<int, flag_bits_v<Flag>> {
};

template <typename G>
inline constexpr int grant_open_flags_v = grant_open_flags<G>::value;

// O_CLOEXEC is folded in unconditionally.  A descriptor that survives execve
// leaks into every child process.
template <typename... Grants>
inline constexpr int fold_open_flags() noexcept {
    int acc = O_CLOEXEC;
    ((acc |= grant_open_flags_v<Grants>), ...);
    return acc;
}

template <typename G>
struct is_mode_grant : std::false_type {};
template <typename Mode>
struct is_mode_grant<::crucible::fixy::grant::fs::mode<Mode>> : std::true_type {};

template <typename... Grants>
inline constexpr bool has_mode_v = (is_mode_grant<Grants>::value || ...);

template <typename... Grants>
inline constexpr bool has_duplicate_mode_v = (static_cast<int>(is_mode_grant<Grants>::value) + ...) > 1;

template <typename Grant>
struct is_tmpfile_mode_grant : std::false_type {};
template <>
struct is_tmpfile_mode_grant<::crucible::fixy::grant::fs::mode<open_mode::TmpFile>> : std::true_type {};

// O_TMPFILE names the directory that is to hold the new unnamed inode.
// It does not name a file to open.  mint_file takes the path of a file,
// so the two cannot be composed: passing a file path with O_TMPFILE set
// asks the kernel to treat that file as a directory, and the kernel
// answers ENOTDIR.  The tier failed on every call it was given.
template <typename... Grants>
inline constexpr bool has_tmpfile_mode_v = (is_tmpfile_mode_grant<Grants>::value || ...);

}  // namespace detail

// A filesystem syscall crosses the kernel boundary and can park the caller
// until the disk responds, so the context must admit both effects.
template <typename Ctx>
concept CtxAdmitsIoBlock =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, ::crucible::effects::Effect::IO>
    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, ::crucible::effects::Effect::Block>;

template <typename Ctx, typename... Grants>
concept CtxFitsFileMint = CtxAdmitsIoBlock<Ctx> && detail::has_mode_v<Grants...>
                       && !detail::has_duplicate_mode_v<Grants...> && !detail::has_tmpfile_mode_v<Grants...>;

// sync_op::None is excluded.  A caller that needs no durability does not
// call sync at all.
template <typename Ctx, typename SyncOp>
concept CtxFitsSync = CtxAdmitsIoBlock<Ctx> && !std::is_same_v<SyncOp, sync_op::None>;

template <typename Ctx, typename Atomicity>
concept CtxFitsCommitAtomic =
    CtxAdmitsIoBlock<Ctx>
    && (std::is_same_v<Atomicity, atomicity::None> || std::is_same_v<Atomicity, atomicity::Rename>
        || std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>
        || std::is_same_v<Atomicity, atomicity::LinkAtomic>);

// The Grants pack precedes Ctx because every explicit template argument fills
// the pack and Ctx deduces from the first function argument.
template <typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<::crucible::safety::Linear<::crucible::safety::FileHandle>, std::error_code>
mint_file(Ctx const&, Path<::crucible::safety::source::Sanitized> sanitized_path, mode_t perms = 0644) noexcept {
    constexpr int flags = detail::fold_open_flags<Grants...>();
    // Every mode that reaches here opens the path it is given.  The one
    // mode that does not, open_mode::TmpFile, is refused by
    // CtxFitsFileMint above rather than passed to ::open() and failed by
    // the kernel.
    auto fh = detail::impl::do_open_impl(sanitized_path.value().c_str(), flags, perms);
    if (!fh) {
        return std::unexpected{fh.error()};
    }
    return ::crucible::safety::Linear<::crucible::safety::FileHandle>{std::move(*fh)};
}

// Not a mint: this acts on an existing handle and synthesizes nothing.
template <typename SyncOp, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSync<Ctx, SyncOp>
[[nodiscard]] inline std::expected<void, std::error_code> sync(Ctx const&,
                                                               const ::crucible::safety::FileHandle& h) noexcept {
    if (!h.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    constexpr auto op = []() consteval {
        if constexpr (std::is_same_v<SyncOp, sync_op::Fdatasync>) {
            return detail::impl::SyncOpTag::Fdatasync;
        } else if constexpr (std::is_same_v<SyncOp, sync_op::Fsync>) {
            return detail::impl::SyncOpTag::Fsync;
        } else if constexpr (std::is_same_v<SyncOp, sync_op::FsyncParentDir>) {
            return detail::impl::SyncOpTag::FsyncParentDir;
        } else if constexpr (std::is_same_v<SyncOp, sync_op::Msync>) {
            return detail::impl::SyncOpTag::Msync;
        } else {
            static_assert(std::is_same_v<SyncOp, sync_op::Fdatasync> || std::is_same_v<SyncOp, sync_op::Fsync>
                              || std::is_same_v<SyncOp, sync_op::Msync>
                              || std::is_same_v<SyncOp, sync_op::FsyncParentDir>,
                          "sync<SyncOp>: SyncOp must be a sync_op tag");
            return detail::impl::SyncOpTag::Fdatasync;  // unreachable
        }
    }();
    return detail::impl::do_sync_impl(h.get(), op);
}

// Not a mint: this renames existing paths and synthesizes nothing.
template <typename Atomicity, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsCommitAtomic<Ctx, Atomicity>
[[nodiscard]] inline std::expected<void, std::error_code>
commit_atomic(Ctx const&, Path<::crucible::safety::source::Sanitized> tmp,
              Path<::crucible::safety::source::Sanitized> target) noexcept {
    constexpr auto at = []() consteval {
        if constexpr (std::is_same_v<Atomicity, atomicity::Rename>) {
            return detail::impl::AtomicityTag::Rename;
        } else if constexpr (std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>) {
            return detail::impl::AtomicityTag::RenameAt2NoReplace;
        } else if constexpr (std::is_same_v<Atomicity, atomicity::LinkAtomic>) {
            return detail::impl::AtomicityTag::LinkAtomic;
        } else if constexpr (std::is_same_v<Atomicity, atomicity::None>) {
            return detail::impl::AtomicityTag::None;
        } else {
            static_assert(std::is_same_v<Atomicity, atomicity::None> || std::is_same_v<Atomicity, atomicity::Rename>
                              || std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>
                              || std::is_same_v<Atomicity, atomicity::LinkAtomic>,
                          "commit_atomic<Atomicity>: Atomicity must be an atomicity tag");
            return detail::impl::AtomicityTag::None;  // unreachable
        }
    }();
    return detail::impl::do_commit_atomic_impl(tmp.value().c_str(), target.value().c_str(), at);
}

using read_only = ::crucible::fixy::grant::fs::mode<open_mode::ReadOnly>;

// The durable and atomic_write grants only declare intent.  The mint opens
// the file.  The caller still calls sync<Fsync> after writing and then
// commit_atomic<LinkAtomic> to move the result into place.
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] inline auto mint_durable_truncate_file(Ctx const& ctx, Path<::crucible::safety::source::Sanitized> p,
                                                     mode_t perms = 0644) noexcept {
    return mint_file<::crucible::fixy::grant::fs::mode<open_mode::WriteTruncate>,
                     ::crucible::fixy::grant::fs::durable<sync_op::Fsync>,
                     ::crucible::fixy::grant::fs::atomic_write<atomicity::LinkAtomic>>(ctx, std::move(p), perms);
}

// O_DSYNC makes each write(2) return only once the data is on stable storage,
// but not the metadata.  The durable<Fdatasync> grant declares that the
// caller still calls sync<Fdatasync> between batches to cover the metadata.
//
// There is no atomic_write grant.  An append-only file has no tmp-to-target
// rename phase.  The file is the target.
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] inline auto mint_durable_append_file(Ctx const& ctx, Path<::crucible::safety::source::Sanitized> p,
                                                   mode_t perms = 0644) noexcept {
    return mint_file<::crucible::fixy::grant::fs::mode<open_mode::WriteAppend>,
                     ::crucible::fixy::grant::fs::durable<sync_op::Fdatasync>,
                     ::crucible::fixy::grant::fs::with_flag<flag::DataSync>>(ctx, std::move(p), perms);
}

}  // namespace crucible::fixy::fs

namespace crucible::fixy::fs::detail::v224_self_test {

namespace om = open_mode;
namespace fl = flag;
namespace so = sync_op;
namespace at_ = atomicity;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(sizeof(om::ReadOnly) == 1);
static_assert(sizeof(om::WriteCreate) == 1);
static_assert(sizeof(om::WriteAppend) == 1);
static_assert(sizeof(om::WriteTruncate) == 1);
static_assert(sizeof(om::ReadWrite) == 1);
static_assert(sizeof(om::TmpFile) == 1);

static_assert(sizeof(fl::CloseOnExec) == 1);
static_assert(sizeof(fl::NoFollow) == 1);
static_assert(sizeof(fl::Directory) == 1);
static_assert(sizeof(fl::DataSync) == 1);

static_assert(sizeof(so::None) == 1);
static_assert(sizeof(so::Fdatasync) == 1);
static_assert(sizeof(so::Fsync) == 1);

static_assert(sizeof(at_::None) == 1);
static_assert(sizeof(at_::RenameAt2NoReplace) == 1);
static_assert(sizeof(at_::LinkAtomic) == 1);

static_assert(open_mode_flags_v<om::ReadOnly> == O_RDONLY);
static_assert(open_mode_flags_v<om::WriteTruncate> == (O_WRONLY | O_CREAT | O_TRUNC));
static_assert(open_mode_flags_v<om::TmpFile> == (O_TMPFILE | O_RDWR));
static_assert(flag_bits_v<fl::CloseOnExec> == O_CLOEXEC);
static_assert(flag_bits_v<fl::NoFollow> == O_NOFOLLOW);

using ::crucible::fixy::grant::which_dim_v;
using ::crucible::fixy::grant::IsGrantTag;
namespace gfs = ::crucible::fixy::grant::fs;

static_assert(IsGrantTag<gfs::mode<om::ReadOnly>>);
static_assert(IsGrantTag<gfs::with_flag<fl::NoFollow>>);
static_assert(IsGrantTag<gfs::durable<so::Fsync>>);
static_assert(IsGrantTag<gfs::atomic_write<at_::LinkAtomic>>);

static_assert(which_dim_v<gfs::mode<om::ReadOnly>> == D::SyscallSurface);
static_assert(which_dim_v<gfs::with_flag<fl::NoFollow>> == D::SyscallSurface);
static_assert(which_dim_v<gfs::durable<so::Fsync>> == D::SyscallSurface);
static_assert(which_dim_v<gfs::atomic_write<at_::LinkAtomic>> == D::SyscallSurface);

static_assert(!std::is_same_v<gfs::mode<om::ReadOnly>, gfs::mode<om::WriteTruncate>>);
static_assert(!std::is_same_v<gfs::with_flag<fl::NoFollow>, gfs::with_flag<fl::Direct>>);
static_assert(!std::is_same_v<gfs::durable<so::Fdatasync>, gfs::durable<so::Fsync>>);

static_assert(detail::has_mode_v<gfs::mode<om::ReadOnly>>);
static_assert(!detail::has_mode_v<gfs::with_flag<fl::NoFollow>>);
static_assert(detail::has_duplicate_mode_v<gfs::mode<om::ReadOnly>, gfs::mode<om::WriteTruncate>>);
static_assert(!detail::has_duplicate_mode_v<gfs::mode<om::ReadOnly>, gfs::with_flag<fl::NoFollow>>);

static_assert(detail::has_tmpfile_mode_v<gfs::mode<om::TmpFile>>);
static_assert(detail::has_tmpfile_mode_v<gfs::mode<om::TmpFile>, gfs::with_flag<fl::NoFollow>>);
static_assert(!detail::has_tmpfile_mode_v<gfs::mode<om::ReadWrite>>);
static_assert(!detail::has_tmpfile_mode_v<gfs::with_flag<fl::NoFollow>>);

static_assert(detail::fold_open_flags<gfs::mode<om::ReadOnly>>() == (O_RDONLY | O_CLOEXEC));
static_assert(detail::fold_open_flags<gfs::mode<om::WriteTruncate>, gfs::with_flag<fl::NoFollow>>()
              == (O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC));

}  // namespace crucible::fixy::fs::detail::v224_self_test
