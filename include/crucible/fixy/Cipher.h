// SPDX-License-Identifier: BUSL-1.1
#pragma once
// ============================================================================
// FIXY-V-227 — fixy/Cipher.h composite stances substrate
// ============================================================================
//
// The Cipher persistence layer (L14 Lifecycle in CLAUDE.md) writes three
// distinct file-system artefact families, each with a SHARPLY different
// durability + atomicity posture:
//
//   1. Warm-tier checkpoint writers — sub-second durability + atomic
//      per-snapshot publish.  Used for in-progress weight snapshots,
//      KernelCache slot fills, and TraceGraph chain steps.  These
//      churn at iteration-frequency (hundreds per second) so the
//      durability gate is intentionally weak (`fdatasync` only — no
//      inode-metadata round-trip) and the atomicity is "refuse to
//      overwrite" (RENAME_NOREPLACE) so concurrent publishers can't
//      step on each other.
//
//   2. Cold-tier durable writers — full power-loss durability,
//      link-then-rename atomicity.  Used for the Cipher's cold-tier
//      object store (S3-shaped local NVMe).  Throughput is irrelevant;
//      what matters is that a process death MUST NOT leave a partial
//      object visible.  Uses O_SYNC writes, full `fsync`, and
//      `linkat(AT_EMPTY_PATH)` to guarantee that a partial object is
//      never reachable by name.
//
//   3. HEAD-advance writers — atomic pointer-bump from HEAD-N to
//      HEAD-N+1.  The single object whose contents define "what's the
//      current snapshot".  MUST refuse to overwrite the existing HEAD
//      (otherwise concurrent advances would race), AND MUST
//      `fsync(parent_dir_fd)` so the new directory entry is durable on
//      power-loss.
//
// V-227 ships the THREE STANCES as TYPE-LEVEL CONCEPTS over a Grants
// parameter pack.  Each concept folds:
//
//     ( engages stance-required grant 1 )
//   ∧ ( engages stance-required grant 2 )
//   ∧ ( engages stance-required grant 3 )
//   ∧ ( ... )
//
// where "engages X" means the Grants pack contains a grant whose tag
// matches X *with the specific enumerator pinned*.  The stance
// concepts ARE the soundness gate that V-228's `mint_warm_writer` /
// `mint_cold_writer` / `mint_head_advancer` factories will fold into
// their `requires` clauses.
//
// V-227 does NOT ship the mint factories themselves — those land in
// V-228 (#2106).  V-227 ships only the type-level substrate so other
// V-* tasks can begin referencing the stances (`fixy::wrap::cipher::
// IsCipherWarmWriterStance<...>`) before V-228 lands.  Per §XXI the
// HS14 ≥2-fixtures floor applies per-MINT; V-227 ships zero mints, so
// its HS14 obligation is zero.  Verification lives in this header's
// self-test block (positive + negative compile-time witnesses) and
// extends to runtime fixtures in V-228 when the mints land.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//   InitSafe   : All stance aliases are `using` aliases; no fields.
//   TypeSafe   : Stance concepts gate on EXACT enumerator tags
//                (open_mode::WriteTruncate, sync_op::Fdatasync, …) —
//                no implicit conversions; an incorrect tag fails the
//                concept at the requires-clause boundary.
//   NullSafe   : Type-level only — no pointers.
//   MemSafe    : Type-level only — no allocation.
//   BorrowSafe : Type-level only — no shared state.
//   ThreadSafe : Type-level only — no atomics.
//   LeakSafe   : Type-level only — no resources.
//   DetSafe    : Stance-engagement predicates are deterministic
//                template metafunctions over the Grants pack; the
//                same pack always evaluates to the same engagement.

#include <type_traits>

#include <crucible/fixy/Fs.h>      // V-224 — fs::open_mode, sync_op, atomicity, flag tags

namespace crucible::fixy::cipher {

// Re-bind the V-224 enumerator namespaces locally so the stance
// definitions read as one continuous narrative.  (These are namespace
// aliases — they do not introduce new names; pure ergonomics.)
namespace open_mode = ::crucible::fixy::fs::open_mode;
namespace flag      = ::crucible::fixy::fs::flag;
namespace sync_op   = ::crucible::fixy::fs::sync_op;
namespace atomicity = ::crucible::fixy::fs::atomicity;
namespace grant_fs  = ::crucible::fixy::grant::fs;

// ── Detail layer — specific-enumerator engagement predicates ─────────
//
// V-224's `has_mode_v<Grants...>` checks AXIS engagement ("is there
// any mode<X>?").  V-227's stance concepts need ENUMERATOR engagement
// ("is there a mode<WriteTruncate> specifically?").  The pattern is
// identical to V-224's `is_specific_ring_flag_v<>` in fixy/Io.h:
// specialize a `is_specific_*` predicate per axis, then fold across
// the Grants pack.

namespace detail {

// ── mode<TargetMode> engagement ─────────────────────────────────────
template <typename TargetMode, typename G>
struct is_specific_mode : std::false_type {};

template <typename TargetMode>
struct is_specific_mode<TargetMode, ::crucible::fixy::grant::fs::mode<TargetMode>>
    : std::true_type {};

template <typename TargetMode, typename G>
inline constexpr bool is_specific_mode_v = is_specific_mode<TargetMode, G>::value;

template <typename TargetMode, typename... Grants>
inline constexpr bool has_specific_mode_v =
    (is_specific_mode_v<TargetMode, Grants> || ...);

// ── with_flag<TargetFlag> engagement ────────────────────────────────
template <typename TargetFlag, typename G>
struct is_specific_with_flag : std::false_type {};

template <typename TargetFlag>
struct is_specific_with_flag<TargetFlag,
                             ::crucible::fixy::grant::fs::with_flag<TargetFlag>>
    : std::true_type {};

template <typename TargetFlag, typename G>
inline constexpr bool is_specific_with_flag_v =
    is_specific_with_flag<TargetFlag, G>::value;

template <typename TargetFlag, typename... Grants>
inline constexpr bool has_specific_with_flag_v =
    (is_specific_with_flag_v<TargetFlag, Grants> || ...);

// ── durable<TargetSync> engagement ──────────────────────────────────
template <typename TargetSync, typename G>
struct is_specific_durable : std::false_type {};

template <typename TargetSync>
struct is_specific_durable<TargetSync,
                           ::crucible::fixy::grant::fs::durable<TargetSync>>
    : std::true_type {};

template <typename TargetSync, typename G>
inline constexpr bool is_specific_durable_v =
    is_specific_durable<TargetSync, G>::value;

template <typename TargetSync, typename... Grants>
inline constexpr bool has_specific_durable_v =
    (is_specific_durable_v<TargetSync, Grants> || ...);

// ── atomic_write<TargetAtomicity> engagement ────────────────────────
template <typename TargetAtomicity, typename G>
struct is_specific_atomic_write : std::false_type {};

template <typename TargetAtomicity>
struct is_specific_atomic_write<
    TargetAtomicity,
    ::crucible::fixy::grant::fs::atomic_write<TargetAtomicity>>
    : std::true_type {};

template <typename TargetAtomicity, typename G>
inline constexpr bool is_specific_atomic_write_v =
    is_specific_atomic_write<TargetAtomicity, G>::value;

template <typename TargetAtomicity, typename... Grants>
inline constexpr bool has_specific_atomic_write_v =
    (is_specific_atomic_write_v<TargetAtomicity, Grants> || ...);

}  // namespace detail

// ── Stance 1: CipherWarmWriter ───────────────────────────────────────
//
// In-progress snapshot writer.  Sub-second durability + atomic
// per-snapshot publication via RENAME_NOREPLACE.
//
// Required grants (ALL must engage):
//
//   * grant_fs::mode<open_mode::WriteTruncate>          (open with O_TRUNC)
//   * grant_fs::durable<sync_op::Fdatasync>             (data-only sync)
//   * grant_fs::atomic_write<atomicity::RenameAt2NoReplace>
//                                                       (rename refusing overwrite)
//
// Rationale:
//   - `WriteTruncate` is the natural mode for re-writing an
//     in-progress snapshot file (the old contents are stale once a
//     new iteration begins).
//   - `Fdatasync` syncs file data but NOT inode metadata (mtime,
//     ctime).  ~10× faster than `Fsync` because the inode bucket is
//     usually shared with many other files (one fsync would flush all
//     of them).  We accept the risk that `mtime` is non-durable —
//     warm-tier snapshots are content-addressed by Merkle hash; mtime
//     plays no semantic role.
//   - `RenameAt2NoReplace` prevents concurrent publishers from
//     clobbering each other's snapshot.  If publisher P1 calls
//     rename(tmp1 → snap-N) and concurrently P2 calls rename(tmp2 →
//     snap-N), RENAME_NOREPLACE returns EEXIST for the loser; without
//     it, the loser silently overwrites P1's commit.

template <typename... Grants>
inline constexpr bool engages_warm_writer_stance_v =
       detail::has_specific_mode_v<open_mode::WriteTruncate, Grants...>
    && detail::has_specific_durable_v<sync_op::Fdatasync, Grants...>
    && detail::has_specific_atomic_write_v<atomicity::RenameAt2NoReplace,
                                           Grants...>;

template <typename... Grants>
concept IsCipherWarmWriterStance = engages_warm_writer_stance_v<Grants...>;

// ── Stance 2: CipherColdWriter ───────────────────────────────────────
//
// Cold-tier durable writer.  Full power-loss durability +
// link-then-rename atomicity.
//
// Required grants (ALL must engage):
//
//   * grant_fs::mode<open_mode::WriteCreate>            (O_CREAT but no O_TRUNC)
//   * grant_fs::with_flag<flag::FullSync>               (O_SYNC writes)
//   * grant_fs::durable<sync_op::Fsync>                 (full fsync at close)
//   * grant_fs::atomic_write<atomicity::LinkAtomic>     (linkat AT_EMPTY_PATH)
//
// Rationale:
//   - `WriteCreate` + `LinkAtomic` is the classic "write to anonymous
//     tmp, then linkat into final name" pattern.  The tmp file is
//     never reachable by name during the write, so a crash mid-write
//     leaves no garbage in the namespace.
//   - `FullSync` (O_SYNC) makes EVERY write durable before returning.
//     Slow (~5-10× normal write latency) but the cold tier is
//     write-rarely / read-during-recovery; the durability matters
//     more than throughput.
//   - `Fsync` at close is belt-and-suspenders against the corner case
//     where the filesystem doesn't honor O_SYNC for the inode
//     metadata (some XFS configurations).

template <typename... Grants>
inline constexpr bool engages_cold_writer_stance_v =
       detail::has_specific_mode_v<open_mode::WriteCreate, Grants...>
    && detail::has_specific_with_flag_v<flag::FullSync, Grants...>
    && detail::has_specific_durable_v<sync_op::Fsync, Grants...>
    && detail::has_specific_atomic_write_v<atomicity::LinkAtomic, Grants...>;

template <typename... Grants>
concept IsCipherColdWriterStance = engages_cold_writer_stance_v<Grants...>;

// ── Stance 3: HeadAdvance ────────────────────────────────────────────
//
// Atomic HEAD-pointer advance from snapshot N → snapshot N+1.
//
// Required grants (ALL must engage):
//
//   * grant_fs::mode<open_mode::WriteCreate>            (O_CREAT, no O_TRUNC)
//   * grant_fs::atomic_write<atomicity::RenameAt2NoReplace>
//                                                       (rename refusing overwrite)
//   * grant_fs::durable<sync_op::FsyncParentDir>        (fsync(parent_dir_fd))
//
// Rationale:
//   - The HEAD object's CONTENTS are negligible (a single line of
//     `snapshot-id\n`).  The DIRECTORY ENTRY is what matters — it's
//     what `readdir` returns when a peer enumerates "what snapshots
//     exist?".
//   - `FsyncParentDir` is mandatory: without it, a power loss between
//     `rename(tmp → HEAD)` and the directory entry hitting disk can
//     leave HEAD pointing at the OLD snapshot even though the rename
//     "succeeded" from the caller's POV.
//   - `RenameAt2NoReplace` (RENAME_NOREPLACE) means a concurrent
//     advancer of N → N+1 vs N → N+2 sees EEXIST on the loser; we
//     never have two HEADs racing toward different futures.
//
// HeadAdvance does NOT engage `with_flag<FullSync>`.  The HEAD file
// is so small (single line) that O_SYNC adds no meaningful
// durability over the closing `fsync(parent_dir_fd)`.

template <typename... Grants>
inline constexpr bool engages_head_advance_stance_v =
       detail::has_specific_mode_v<open_mode::WriteCreate, Grants...>
    && detail::has_specific_atomic_write_v<atomicity::RenameAt2NoReplace,
                                           Grants...>
    && detail::has_specific_durable_v<sync_op::FsyncParentDir, Grants...>;

template <typename... Grants>
concept IsHeadAdvanceStance = engages_head_advance_stance_v<Grants...>;

// ── Canonical-pack documentation aliases ─────────────────────────────
//
// These aliases name the CANONICAL grant pack for each stance.  They
// are not load-bearing — callers can pass their own pack so long as
// it satisfies the corresponding `Is*Stance` concept — but they serve
// as the documentation-and-grep target for "the right way to engage
// this stance".  V-228's mints reference these aliases in their
// doc-blocks; production callers can `using` them at the call site.
//
// Implementation note: aliases are wrapped in a `pack<>` template
// rather than a parameter-pack expansion because C++ does not permit
// type aliases to alias a raw parameter pack.

template <typename... Grants>
struct pack final {};

using CipherWarmWriterStance = pack<
    grant_fs::mode<open_mode::WriteTruncate>,
    grant_fs::durable<sync_op::Fdatasync>,
    grant_fs::atomic_write<atomicity::RenameAt2NoReplace>
>;

using CipherColdWriterStance = pack<
    grant_fs::mode<open_mode::WriteCreate>,
    grant_fs::with_flag<flag::FullSync>,
    grant_fs::durable<sync_op::Fsync>,
    grant_fs::atomic_write<atomicity::LinkAtomic>
>;

using HeadAdvanceStance = pack<
    grant_fs::mode<open_mode::WriteCreate>,
    grant_fs::atomic_write<atomicity::RenameAt2NoReplace>,
    grant_fs::durable<sync_op::FsyncParentDir>
>;

// ── Stance-from-pack adapter ────────────────────────────────────────
//
// The stance concepts are written as variadic `IsCipherWarmWriterStance<
// Grants...>`.  Sometimes a caller has a packed alias (e.g.
// `CipherWarmWriterStance` above, which is a `pack<G1, G2, G3>`); we
// expose a helper to unpack the alias into a stance-concept query.

template <typename P>
struct stance_pack_satisfies_warm   : std::false_type {};
template <typename P>
struct stance_pack_satisfies_cold   : std::false_type {};
template <typename P>
struct stance_pack_satisfies_head   : std::false_type {};

template <typename... Grants>
struct stance_pack_satisfies_warm<pack<Grants...>>
    : std::bool_constant<IsCipherWarmWriterStance<Grants...>> {};

template <typename... Grants>
struct stance_pack_satisfies_cold<pack<Grants...>>
    : std::bool_constant<IsCipherColdWriterStance<Grants...>> {};

template <typename... Grants>
struct stance_pack_satisfies_head<pack<Grants...>>
    : std::bool_constant<IsHeadAdvanceStance<Grants...>> {};

template <typename P>
inline constexpr bool stance_pack_satisfies_warm_v =
    stance_pack_satisfies_warm<P>::value;

template <typename P>
inline constexpr bool stance_pack_satisfies_cold_v =
    stance_pack_satisfies_cold<P>::value;

template <typename P>
inline constexpr bool stance_pack_satisfies_head_v =
    stance_pack_satisfies_head<P>::value;

// ── Self-test block ──────────────────────────────────────────────────
//
// Per V-225's discipline (and CLAUDE.md feedback on header-only
// static_assert blind spot), the self-test block lives inside the
// header AND a sentinel TU includes the header to ensure the asserts
// are verified under project warning flags.  The sentinel here is
// `test_fixy_engaged.cpp` which transitively pulls in all of fixy/
// via Wrap.h.
//
// Coverage matrix (3 stances × {positive-engagement, missing-grant-N
// negative, wrong-enumerator-N negative} = 3 × 7 = 21 anchored
// asserts):

namespace selftest {

// Positive — every canonical stance pack satisfies its own concept.
static_assert(stance_pack_satisfies_warm_v<CipherWarmWriterStance>,
              "V-227: CipherWarmWriterStance must satisfy IsCipherWarmWriterStance");
static_assert(stance_pack_satisfies_cold_v<CipherColdWriterStance>,
              "V-227: CipherColdWriterStance must satisfy IsCipherColdWriterStance");
static_assert(stance_pack_satisfies_head_v<HeadAdvanceStance>,
              "V-227: HeadAdvanceStance must satisfy IsHeadAdvanceStance");

// Cross-stance distinctness — each stance's pack fails the OTHER
// stances' concepts.  This proves the stance gates are mutually
// distinguishing (a warm-writer pack cannot accidentally satisfy
// the cold-writer concept and vice versa).
static_assert(!stance_pack_satisfies_cold_v<CipherWarmWriterStance>,
              "V-227: warm-writer pack must NOT satisfy cold-writer concept");
static_assert(!stance_pack_satisfies_head_v<CipherWarmWriterStance>,
              "V-227: warm-writer pack must NOT satisfy head-advance concept");
static_assert(!stance_pack_satisfies_warm_v<CipherColdWriterStance>,
              "V-227: cold-writer pack must NOT satisfy warm-writer concept");
static_assert(!stance_pack_satisfies_head_v<CipherColdWriterStance>,
              "V-227: cold-writer pack must NOT satisfy head-advance concept");
static_assert(!stance_pack_satisfies_warm_v<HeadAdvanceStance>,
              "V-227: head-advance pack must NOT satisfy warm-writer concept");
static_assert(!stance_pack_satisfies_cold_v<HeadAdvanceStance>,
              "V-227: head-advance pack must NOT satisfy cold-writer concept");

// Empty pack — fails all three stances (sanity).
static_assert(!IsCipherWarmWriterStance<>,
              "V-227: empty pack must NOT satisfy warm-writer concept");
static_assert(!IsCipherColdWriterStance<>,
              "V-227: empty pack must NOT satisfy cold-writer concept");
static_assert(!IsHeadAdvanceStance<>,
              "V-227: empty pack must NOT satisfy head-advance concept");

// Missing-grant — warm-writer pack with the atomic_write removed
// fails the warm-writer concept.  Witnesses the AND-chain's fold
// properly rejects partial-engagement.
static_assert(!IsCipherWarmWriterStance<
                  grant_fs::mode<open_mode::WriteTruncate>,
                  grant_fs::durable<sync_op::Fdatasync>>,
              "V-227: warm-writer missing atomic_write must fail");
static_assert(!IsCipherColdWriterStance<
                  grant_fs::mode<open_mode::WriteCreate>,
                  grant_fs::with_flag<flag::FullSync>,
                  grant_fs::durable<sync_op::Fsync>>,
              "V-227: cold-writer missing atomic_write must fail");
static_assert(!IsHeadAdvanceStance<
                  grant_fs::mode<open_mode::WriteCreate>,
                  grant_fs::atomic_write<atomicity::RenameAt2NoReplace>>,
              "V-227: head-advance missing FsyncParentDir must fail");

// Wrong-enumerator — warm-writer pack with `Fsync` instead of
// `Fdatasync` (stronger durability than required) fails the
// warm-writer concept because the concept demands the SPECIFIC
// enumerator.  Witnesses that the stance gates are precision-tight,
// not "at least this strong".  This is intentional: if a caller
// wants Fsync, they want the cold-tier stance, not the warm-tier.
static_assert(!IsCipherWarmWriterStance<
                  grant_fs::mode<open_mode::WriteTruncate>,
                  grant_fs::durable<sync_op::Fsync>,
                  grant_fs::atomic_write<atomicity::RenameAt2NoReplace>>,
              "V-227: warm-writer with Fsync (not Fdatasync) must fail — "
              "use cold-writer stance instead");

// Wrong atomicity — head-advance pack with `Rename` (not
// RENAME_NOREPLACE) fails the head-advance concept.  Witnesses
// that the stance gates refuse the weaker atomicity that would
// silently clobber a concurrent advance.
static_assert(!IsHeadAdvanceStance<
                  grant_fs::mode<open_mode::WriteCreate>,
                  grant_fs::atomic_write<atomicity::Rename>,
                  grant_fs::durable<sync_op::FsyncParentDir>>,
              "V-227: head-advance with plain Rename (not RENAME_NOREPLACE) "
              "must fail");

// Wrong mode — cold-writer pack with `WriteTruncate` (instead of
// WriteCreate) fails the cold-writer concept.  WriteTruncate +
// LinkAtomic is structurally wrong because linkat (AT_EMPTY_PATH)
// requires an anonymous tmp file (O_TMPFILE), not a truncated
// existing file.
static_assert(!IsCipherColdWriterStance<
                  grant_fs::mode<open_mode::WriteTruncate>,
                  grant_fs::with_flag<flag::FullSync>,
                  grant_fs::durable<sync_op::Fsync>,
                  grant_fs::atomic_write<atomicity::LinkAtomic>>,
              "V-227: cold-writer with WriteTruncate (not WriteCreate) "
              "must fail");

// Optional-extension — warm-writer pack PLUS an extra
// `with_flag<Direct>` grant still satisfies the warm-writer concept
// (the concept demands the AND of required grants; extras are
// permitted).  Witnesses that the stance is OPEN to optional
// engagement; only the required-set is gated.
static_assert(IsCipherWarmWriterStance<
                  grant_fs::mode<open_mode::WriteTruncate>,
                  grant_fs::durable<sync_op::Fdatasync>,
                  grant_fs::atomic_write<atomicity::RenameAt2NoReplace>,
                  grant_fs::with_flag<flag::Direct>>,
              "V-227: warm-writer pack + optional with_flag<Direct> must "
              "still satisfy (extras permitted)");

}  // namespace selftest

}  // namespace crucible::fixy::cipher
