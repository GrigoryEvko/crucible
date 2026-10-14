// The compile-time checks of fixy/os/Fs.h.

#include <fixy/os/Fs.h>

namespace fixy::fs::detail::fs_surface_invariants {

// The words of foundation/os/OpenFlags.h, which src/foundation/os/Syscall.cpp
// compares with the O_* words of <linux/fcntl.h>.
namespace word = ::foundation::os;

static_assert(open_mode_flags_of(^^open_mode::ReadOnly) == word::kOpenReadOnly);
static_assert(open_mode_flags_of(^^open_mode::WriteTruncate)
              == (word::kOpenWriteOnly | word::kOpenCreate | word::kOpenTruncate));
static_assert(open_mode_flags_of(^^open_mode::WriteAppend)
              == (word::kOpenWriteOnly | word::kOpenCreate | word::kOpenAppend));
static_assert(flag_bits_of(^^flag::CloseOnExec) == word::kOpenCloseOnExec);
static_assert(flag_bits_of(^^flag::NoFollow) == word::kOpenNoFollow);
static_assert(flag_bits_of(^^flag::FullSync) == word::kOpenFullSync);

using A_RO = ::fixy::atom::fs::mode<open_mode::ReadOnly>;
using A_Trunc = ::fixy::atom::fs::mode<open_mode::WriteTruncate>;
using A_NoFollow = ::fixy::atom::fs::with_flag<flag::NoFollow>;
using A_Fsync = ::fixy::atom::fs::durable<sync_op::Fsync>;
using A_Rename = ::fixy::atom::fs::atomic_write<atomicity::Rename>;

static_assert(::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, A_RO, A_NoFollow>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, A_NoFollow>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, A_RO, A_Trunc>);

static_assert(fold_open_flags<A_RO>() == (word::kOpenReadOnly | word::kOpenCloseOnExec));
static_assert(fold_open_flags<A_Trunc, A_NoFollow>()
              == (word::kOpenWriteOnly | word::kOpenCreate | word::kOpenTruncate | word::kOpenNoFollow
                  | word::kOpenCloseOnExec));
static_assert(fold_open_flags<A_Trunc, A_Fsync, A_Rename>()
                  == (word::kOpenWriteOnly | word::kOpenCreate | word::kOpenTruncate | word::kOpenCloseOnExec),
              "durable and atomic_write atoms declare intent and add no open flag.");

// A tag with no row in a table has no bits.  A read of them is a
// compile error, so an unknown mode cannot open for reading and an
// unknown flag cannot vanish from the open word.
struct NotAMode final {};
struct NotAFlag final {};
static_assert(!MappedOpenMode<NotAMode>, "a mode with no row must have no bits, not O_RDONLY.");
static_assert(!MappedFlag<NotAFlag>, "a flag with no row must have no bits, not zero.");
static_assert(!MappedOpenMode<void> && !MappedFlag<void>);
static_assert(MappedOpenMode<open_mode::ReadOnly> && MappedFlag<flag::NoFollow>);
static_assert(!MappedOpenMode<const open_mode::ReadOnly>, "a mode with a cv-qualifier is another type.");
static_assert(all_atom_tags_known_v<A_RO, A_NoFollow, A_Fsync, A_Rename>);
static_assert(!all_atom_tags_known_v<::fixy::atom::fs::mode<NotAMode>>);
static_assert(!all_atom_tags_known_v<A_RO, ::fixy::atom::fs::with_flag<NotAFlag>>);

// The row derived from the pack is IO and Block, and this is the pin on
// that.
using ExpectedFsRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_RO>, ExpectedFsRow>);
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Trunc, A_Fsync, A_Rename>, ExpectedFsRow>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsFileMint<IoBlockCtx, A_RO>);
static_assert(!CtxFitsFileMint<IoOnlyCtx, A_RO>, "a context without Block must not open a file: open can park.");
static_assert(!CtxFitsFileMint<IoBlockCtx>, "an empty atom pack names no mode.");
static_assert(!CtxFitsFileMint<IoBlockCtx, A_RO, A_Trunc>, "two modes in one pack.");
static_assert(!CtxFitsFileMint<IoBlockCtx, ::fixy::atom::fs::mode<NotAMode>>,
              "a mode with no O_* mapping must be refused, not opened O_RDONLY.");

static_assert(CtxFitsSync<IoBlockCtx, sync_op::Fdatasync>);
static_assert(CtxFitsSync<IoBlockCtx, sync_op::FsyncParentDir>);
static_assert(!CtxFitsSync<IoBlockCtx, sync_op::None>, "None has no syscall behind it.");
static_assert(!CtxFitsSync<IoOnlyCtx, sync_op::Fsync>);
struct NotASyncOp final {};
static_assert(!KnownSyncOp<NotASyncOp> && !CtxFitsSync<IoBlockCtx, NotASyncOp>);

static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::Rename>);
static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::RenameAt2NoReplace>);
static_assert(!CtxFitsCommitAtomic<IoBlockCtx, atomicity::None>, "None renames nothing, so it commits nothing.");
static_assert(!CtxFitsCommitAtomic<IoOnlyCtx, atomicity::Rename>);
struct NotAnAtomicity final {};
static_assert(!KnownAtomicity<NotAnAtomicity> && !CtxFitsCommitAtomic<IoBlockCtx, NotAnAtomicity>);

static_assert(!std::is_constructible_v<Dirfd, OwnedFd&&>,
              "An OwnedFd is evidence of a descriptor, not of a directory; open_dirfd is the only door.");
static_assert(std::is_default_constructible_v<Dirfd>);
static_assert(!std::is_copy_constructible_v<Dirfd>);
static_assert(!std::is_default_constructible_v<FileDoor> && !std::is_copy_constructible_v<FileDoor>
                  && !std::is_move_constructible_v<FileDoor>,
              "No object of the file door exists.  Its private members are the only calls to ::open.");

}  // namespace fixy::fs::detail::fs_surface_invariants
