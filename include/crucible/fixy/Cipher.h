// SPDX-License-Identifier: BUSL-1.1
#pragma once
#include <type_traits>

#include <crucible/fixy/Fs.h>

namespace crucible::fixy::cipher {

namespace open_mode = ::crucible::fixy::fs::open_mode;
namespace flag = ::crucible::fixy::fs::flag;
namespace sync_op = ::crucible::fixy::fs::sync_op;
namespace atomicity = ::crucible::fixy::fs::atomicity;
namespace grant_fs = ::crucible::fixy::grant::fs;

// The axis-level engagement predicates answer whether any grant on an axis is
// present. A stance needs to know that one specific enumerator is present, so
// each predicate below matches the exact tag.

namespace detail {

template <typename TargetMode, typename G>
struct is_specific_mode : std::false_type {};

template <typename TargetMode>
struct is_specific_mode<TargetMode, ::crucible::fixy::grant::fs::mode<TargetMode>> : std::true_type {};

template <typename TargetMode, typename G>
inline constexpr bool is_specific_mode_v = is_specific_mode<TargetMode, G>::value;

template <typename TargetMode, typename... Grants>
inline constexpr bool has_specific_mode_v = (is_specific_mode_v<TargetMode, Grants> || ...);

template <typename TargetFlag, typename G>
struct is_specific_with_flag : std::false_type {};

template <typename TargetFlag>
struct is_specific_with_flag<TargetFlag, ::crucible::fixy::grant::fs::with_flag<TargetFlag>> : std::true_type {};

template <typename TargetFlag, typename G>
inline constexpr bool is_specific_with_flag_v = is_specific_with_flag<TargetFlag, G>::value;

template <typename TargetFlag, typename... Grants>
inline constexpr bool has_specific_with_flag_v = (is_specific_with_flag_v<TargetFlag, Grants> || ...);

template <typename TargetSync, typename G>
struct is_specific_durable : std::false_type {};

template <typename TargetSync>
struct is_specific_durable<TargetSync, ::crucible::fixy::grant::fs::durable<TargetSync>> : std::true_type {};

template <typename TargetSync, typename G>
inline constexpr bool is_specific_durable_v = is_specific_durable<TargetSync, G>::value;

template <typename TargetSync, typename... Grants>
inline constexpr bool has_specific_durable_v = (is_specific_durable_v<TargetSync, Grants> || ...);

template <typename TargetAtomicity, typename G>
struct is_specific_atomic_write : std::false_type {};

template <typename TargetAtomicity>
struct is_specific_atomic_write<TargetAtomicity, ::crucible::fixy::grant::fs::atomic_write<TargetAtomicity>>
    : std::true_type {};

template <typename TargetAtomicity, typename G>
inline constexpr bool is_specific_atomic_write_v = is_specific_atomic_write<TargetAtomicity, G>::value;

template <typename TargetAtomicity, typename... Grants>
inline constexpr bool has_specific_atomic_write_v = (is_specific_atomic_write_v<TargetAtomicity, Grants> || ...);

}  // namespace detail

// The in-progress snapshot writer.
//
// Fdatasync syncs file data but not inode metadata. It is cheaper than a full
// fsync because the inode bucket is usually shared with many other files, and
// one fsync would flush all of them. The lost mtime durability carries no
// meaning here, because a warm-tier snapshot is addressed by content hash.
//
// RENAME_NOREPLACE returns EEXIST to the loser of a concurrent publish.
// Without it the loser silently overwrites the winner's commit.

template <typename... Grants>
inline constexpr bool engages_warm_writer_stance_v =
    detail::has_specific_mode_v<open_mode::WriteTruncate, Grants...>
    && detail::has_specific_durable_v<sync_op::Fdatasync, Grants...>
    && detail::has_specific_atomic_write_v<atomicity::RenameAt2NoReplace, Grants...>;

template <typename... Grants>
concept IsCipherWarmWriterStance = engages_warm_writer_stance_v<Grants...>;

// The cold-tier durable writer.
//
// WriteCreate with LinkAtomic writes to an anonymous temporary and then links
// it into its final name. The temporary is never reachable by name during the
// write, so a crash mid-write leaves nothing behind in the namespace.
//
// O_SYNC makes every write durable before it returns. The cold tier is
// written rarely and read during recovery, so durability outranks throughput.
// The closing fsync covers filesystems that do not honor O_SYNC for the inode
// metadata, which some XFS configurations do not.

template <typename... Grants>
inline constexpr bool engages_cold_writer_stance_v =
    detail::has_specific_mode_v<open_mode::WriteCreate, Grants...>
    && detail::has_specific_with_flag_v<flag::FullSync, Grants...>
    && detail::has_specific_durable_v<sync_op::Fsync, Grants...>
    && detail::has_specific_atomic_write_v<atomicity::LinkAtomic, Grants...>;

template <typename... Grants>
concept IsCipherColdWriterStance = engages_cold_writer_stance_v<Grants...>;

// The atomic advance of the HEAD pointer from one snapshot to the next.
//
// The contents of the HEAD object are a single line. The directory entry is
// what matters, because that is what a peer sees when it enumerates the
// snapshots. A power loss between the rename and the directory entry reaching
// the disk leaves HEAD on the old snapshot even though the rename returned
// success, so the fsync of the parent directory is mandatory.
//
// There is no O_SYNC grant here. The HEAD file is one line, so O_SYNC adds
// nothing over the closing fsync of the parent directory.

template <typename... Grants>
inline constexpr bool engages_head_advance_stance_v =
    detail::has_specific_mode_v<open_mode::WriteCreate, Grants...>
    && detail::has_specific_atomic_write_v<atomicity::RenameAt2NoReplace, Grants...>
    && detail::has_specific_durable_v<sync_op::FsyncParentDir, Grants...>;

template <typename... Grants>
concept IsHeadAdvanceStance = engages_head_advance_stance_v<Grants...>;

// The aliases below name the canonical pack for each stance. They are not
// load-bearing. A caller may pass any pack that satisfies the matching
// concept. They are wrapped in a class template because a type alias cannot
// alias a raw parameter pack.

template <typename... Grants>
struct pack final {};

using CipherWarmWriterStance = pack<grant_fs::mode<open_mode::WriteTruncate>, grant_fs::durable<sync_op::Fdatasync>,
                                    grant_fs::atomic_write<atomicity::RenameAt2NoReplace>>;

using CipherColdWriterStance = pack<grant_fs::mode<open_mode::WriteCreate>, grant_fs::with_flag<flag::FullSync>,
                                    grant_fs::durable<sync_op::Fsync>, grant_fs::atomic_write<atomicity::LinkAtomic>>;

using HeadAdvanceStance =
    pack<grant_fs::mode<open_mode::WriteCreate>, grant_fs::atomic_write<atomicity::RenameAt2NoReplace>,
         grant_fs::durable<sync_op::FsyncParentDir>>;

template <typename P>
struct stance_pack_satisfies_warm : std::false_type {};
template <typename P>
struct stance_pack_satisfies_cold : std::false_type {};
template <typename P>
struct stance_pack_satisfies_head : std::false_type {};

template <typename... Grants>
struct stance_pack_satisfies_warm<pack<Grants...>> : std::bool_constant<IsCipherWarmWriterStance<Grants...>> {};

template <typename... Grants>
struct stance_pack_satisfies_cold<pack<Grants...>> : std::bool_constant<IsCipherColdWriterStance<Grants...>> {};

template <typename... Grants>
struct stance_pack_satisfies_head<pack<Grants...>> : std::bool_constant<IsHeadAdvanceStance<Grants...>> {};

template <typename P>
inline constexpr bool stance_pack_satisfies_warm_v = stance_pack_satisfies_warm<P>::value;

template <typename P>
inline constexpr bool stance_pack_satisfies_cold_v = stance_pack_satisfies_cold<P>::value;

template <typename P>
inline constexpr bool stance_pack_satisfies_head_v = stance_pack_satisfies_head<P>::value;

namespace selftest {

static_assert(stance_pack_satisfies_warm_v<CipherWarmWriterStance>,
              "CipherWarmWriterStance must satisfy IsCipherWarmWriterStance");
static_assert(stance_pack_satisfies_cold_v<CipherColdWriterStance>,
              "CipherColdWriterStance must satisfy IsCipherColdWriterStance");
static_assert(stance_pack_satisfies_head_v<HeadAdvanceStance>, "HeadAdvanceStance must satisfy IsHeadAdvanceStance");

static_assert(!stance_pack_satisfies_cold_v<CipherWarmWriterStance>,
              "warm-writer pack must NOT satisfy cold-writer concept");
static_assert(!stance_pack_satisfies_head_v<CipherWarmWriterStance>,
              "warm-writer pack must NOT satisfy head-advance concept");
static_assert(!stance_pack_satisfies_warm_v<CipherColdWriterStance>,
              "cold-writer pack must NOT satisfy warm-writer concept");
static_assert(!stance_pack_satisfies_head_v<CipherColdWriterStance>,
              "cold-writer pack must NOT satisfy head-advance concept");
static_assert(!stance_pack_satisfies_warm_v<HeadAdvanceStance>,
              "head-advance pack must NOT satisfy warm-writer concept");
static_assert(!stance_pack_satisfies_cold_v<HeadAdvanceStance>,
              "head-advance pack must NOT satisfy cold-writer concept");

static_assert(!IsCipherWarmWriterStance<>, "empty pack must NOT satisfy warm-writer concept");
static_assert(!IsCipherColdWriterStance<>, "empty pack must NOT satisfy cold-writer concept");
static_assert(!IsHeadAdvanceStance<>, "empty pack must NOT satisfy head-advance concept");

static_assert(
    !IsCipherWarmWriterStance<grant_fs::mode<open_mode::WriteTruncate>, grant_fs::durable<sync_op::Fdatasync>>,
    "warm-writer missing atomic_write must fail");
static_assert(!IsCipherColdWriterStance<grant_fs::mode<open_mode::WriteCreate>, grant_fs::with_flag<flag::FullSync>,
                                        grant_fs::durable<sync_op::Fsync>>,
              "cold-writer missing atomic_write must fail");
static_assert(
    !IsHeadAdvanceStance<grant_fs::mode<open_mode::WriteCreate>, grant_fs::atomic_write<atomicity::RenameAt2NoReplace>>,
    "head-advance missing FsyncParentDir must fail");

// Each gate demands its exact enumerator. A stronger durability grant fails
// too, because a caller who wants it wants a different stance.
static_assert(!IsCipherWarmWriterStance<grant_fs::mode<open_mode::WriteTruncate>, grant_fs::durable<sync_op::Fsync>,
                                        grant_fs::atomic_write<atomicity::RenameAt2NoReplace>>,
              "warm-writer with Fsync (not Fdatasync) must fail — "
              "use cold-writer stance instead");

static_assert(!IsHeadAdvanceStance<grant_fs::mode<open_mode::WriteCreate>, grant_fs::atomic_write<atomicity::Rename>,
                                   grant_fs::durable<sync_op::FsyncParentDir>>,
              "head-advance with plain Rename (not RENAME_NOREPLACE) "
              "must fail");

// linkat with AT_EMPTY_PATH needs an anonymous temporary opened with
// O_TMPFILE, not a truncated existing file, so the pairing is refused.
static_assert(
    !IsCipherColdWriterStance<grant_fs::mode<open_mode::WriteTruncate>, grant_fs::with_flag<flag::FullSync>,
                              grant_fs::durable<sync_op::Fsync>, grant_fs::atomic_write<atomicity::LinkAtomic>>,
    "cold-writer with WriteTruncate (not WriteCreate) "
    "must fail");

static_assert(
    IsCipherWarmWriterStance<grant_fs::mode<open_mode::WriteTruncate>, grant_fs::durable<sync_op::Fdatasync>,
                             grant_fs::atomic_write<atomicity::RenameAt2NoReplace>, grant_fs::with_flag<flag::Direct>>,
    "warm-writer pack + optional with_flag<Direct> must "
    "still satisfy (extras permitted)");

}  // namespace selftest

}  // namespace crucible::fixy::cipher
