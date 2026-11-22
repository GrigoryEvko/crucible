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
using ::fixy::os::RightSet;
static_assert(CtxFitsDescriptorSync<IoBlockCtx, sync_op::Fdatasync, RightSet::sync>
              && CtxFitsDescriptorSync<IoBlockCtx, sync_op::Fsync, RightSet::sync>);
static_assert(!CtxFitsDescriptorSync<IoBlockCtx, sync_op::FsyncParentDir, RightSet::sync>,
              "the fsync of a file descriptor flushes no entry of a directory.");
static_assert(!CtxFitsDescriptorSync<IoBlockCtx, sync_op::Fsync, RightSet::read>,
              "a file with no sync right takes no fsync.");
static_assert(CtxFitsDirectorySync<IoBlockCtx, sync_op::FsyncParentDir, directory_rights>);
static_assert(!CtxFitsDirectorySync<IoBlockCtx, sync_op::Fsync, directory_rights>
              && !CtxFitsDirectorySync<IoBlockCtx, sync_op::None, directory_rights>);
static_assert(!CtxFitsSync<IoOnlyCtx, sync_op::Fsync>);

// The rights of each open mode (the rights of fixy/os/Handle.h).  A map
// reads its file, so the map right goes only with the read right, and an
// open for an append has no seek right.
using A_Append = ::fixy::atom::fs::mode<open_mode::WriteAppend>;
using A_ReadWrite = ::fixy::atom::fs::mode<open_mode::ReadWrite>;
static_assert(fold_rights<A_RO>() == (RightSet::read | RightSet::seek | RightSet::sync | RightSet::map));
static_assert(fold_rights<A_Trunc, A_NoFollow, A_Fsync>() == (RightSet::write | RightSet::seek | RightSet::sync));
static_assert(fold_rights<A_Append>() == (RightSet::write | RightSet::sync));
static_assert(fold_rights<A_ReadWrite>()
              == (RightSet::read | RightSet::write | RightSet::seek | RightSet::sync | RightSet::map));
static_assert(open_at_needs<A_RO>() == RightSet::lookup);
static_assert(open_at_needs<A_Trunc>() == (RightSet::lookup | RightSet::create));
static_assert(CtxFitsFileMintAt<IoBlockCtx, directory_rights, A_Trunc>);
static_assert(!CtxFitsFileMintAt<IoBlockCtx, RightSet::lookup, A_Trunc>,
              "an open that makes a missing file needs the create right of the directory.");
static_assert(CtxFitsFileMintAt<IoBlockCtx, RightSet::lookup, A_RO>);
static_assert(!CtxFitsHandleCall<IoBlockCtx, FileOf<A_RO>::rights, RightSet::write>,
              "a file of a read-only open takes no write.");
static_assert(!CtxFitsHandleCall<IoBlockCtx, FileOf<A_Append>::rights, RightSet::read>,
              "a file of an append takes no read.");
struct NotASyncOp final {};
static_assert(!KnownSyncOp<NotASyncOp> && !CtxFitsSync<IoBlockCtx, NotASyncOp>);

static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::Rename>);
static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::RenameAt2NoReplace>);
static_assert(!CtxFitsCommitAtomic<IoBlockCtx, atomicity::None>, "None renames nothing, so it commits nothing.");
static_assert(!CtxFitsCommitAtomic<IoOnlyCtx, atomicity::Rename>);
struct NotAnAtomicity final {};
static_assert(!KnownAtomicity<NotAnAtomicity> && !CtxFitsCommitAtomic<IoBlockCtx, NotAnAtomicity>);

static_assert(!std::is_constructible_v<DirectoryHandle, TemporaryFile&&>,
              "A file is no directory.  open_directory is the door of a directory.");
static_assert(!std::is_constructible_v<DirectoryHandle, int>, "The handle of a descriptor comes only from a door.");
static_assert(std::is_default_constructible_v<DirectoryHandle>);
static_assert(!std::is_copy_constructible_v<DirectoryHandle>);
static_assert(!std::is_default_constructible_v<FileDoor> && !std::is_copy_constructible_v<FileDoor>
                  && !std::is_move_constructible_v<FileDoor>,
              "No object of the file door exists.  Its private members are the only calls to ::open.");

// The split of a path for a call of one final name.  The directory part
// keeps the slash after it, and the final name keeps the slashes at the end
// of the path.  A last part "." or "..", and a path of slashes only, open the
// whole path, so no final name is "..".
[[nodiscard]] consteval bool splits(::foundation::core::TextView path, ::foundation::core::TextView parent,
                                    std::size_t final_at, bool is_whole) {
    FinalSplit const split = split_final_name_(path);
    return split.parent == parent && split.final_at == final_at && split.is_whole == is_whole;
}

static_assert(splits("name", "", 0, false));
static_assert(splits("a/b/name", "a/b/", 4, false));
static_assert(splits("a//name//", "a//", 3, false));
static_assert(splits("/name", "/", 1, false));
static_assert(splits("..name", "", 0, false));
static_assert(splits("", "", 0, false));
static_assert(splits("..", "..", 0, true));
static_assert(splits("a/..", "a/..", 0, true));
static_assert(splits("a/./", "a/./", 0, true));
static_assert(splits("///", "///", 0, true));

// Each door takes the borrow of one kind of handle: a door of a file takes
// the borrow of a File, and a door under a directory the borrow of a
// Directory.  No conversion joins the borrows of two kinds, so no door takes
// the other kind.  The fixtures neg_os_fs_dirfd_handle_read_full and
// neg_os_fs_dirfd_handle_fsync hold a call with the wrong kind.  The sync
// door is a template over the op, so the gates above and the fixture
// neg_os_fs_sync_parent_dir_owned_fd hold the op of each kind.
static_assert(!std::is_convertible_v<DirectoryRef<directory_rights>, FileRef<directory_rights>>
                  && !std::is_constructible_v<FileRef<directory_rights>, DirectoryRef<directory_rights>>,
              "a door of a file must not take a directory.");
static_assert(!std::is_convertible_v<FileRef<temporary_file_rights>, DirectoryRef<temporary_file_rights>>
                  && !std::is_constructible_v<DirectoryRef<temporary_file_rights>, FileRef<temporary_file_rights>>,
              "a door under a directory must not take a file.");

}  // namespace fixy::fs::detail::fs_surface_invariants
