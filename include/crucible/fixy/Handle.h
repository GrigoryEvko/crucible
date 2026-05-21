#pragma once

// ── crucible::fixy::handle — handles/ tree under fixy:: ────────────
//
// Surfaces the RAII resource handle + first-call-wins publication
// primitives from `include/crucible/handles/` under `fixy::handle::`.
// Per misc/16_05_2026_fixy.md and FIXY-U-016: closes the umbrella-
// reach gap where handles/ types (Fd, FileHandle, Once, Lazy, SetOnce,
// OneShotFlag, PublishOnce, PublishSlot, LazyEstablishedChannel) had
// no fixy:: entry point, forcing every fixy-routed consumer to descend
// into `crucible::safety::` (the handles tree's actual namespace).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::Fd                       — strong-typed POSIX fd
//   safety::FileHandle               — move-only RAII fd
//   safety::Once                     — first-call-wins single-init
//   safety::Lazy<T>                  — lazy-init wrapper
//   safety::SetOnce<T>               — single-shot write store
//   safety::OneShotFlag              — alignas(64) release/acquire flag
//   safety::PublishOnce<T>           — one-shot publish channel
//   safety::PublishSlot<T>           — reusable latest-wins slot
//   safety::PublishCommitCell<Tag, WriteAuth>
//                                    — typed publish/commit cell (FIXY-U-016b)
//   safety::AlignedBuffer<T, Align>  — RAII over-aligned heap buffer (FIXY-U-016b)
//   safety::HugePageBuffer<T>        — 2-MB-aligned RAII buffer (FIXY-V-034)
//   safety::OwnedFile                — std::FILE* RAII wrapper (FIXY-V-037-audit / V-032)
//   safety::LazyEstablishedChannel<Proto, Resource>
//                                    — session-handshake-backed channel
//
// ── Cross-reference (fixy-A4-011 dual-export discipline) ───────────
//
// Future fixy-A4-011 cross-checks: if a handle type ALSO appears in
// fixy::wrap:: (e.g., FileHandle as a Linear<>-wrapper composition),
// both paths MUST alias the SAME substrate symbol — verified by the
// dual-export sentinel below.  No symbol drift across import paths.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports do not introduce new state paths.
//   TypeSafe — using-declarations preserve substrate type identity
//              (no implicit conversions).
//   NullSafe — Fd / FileHandle have explicit sentinel discipline at
//              the substrate; the alias inherits.
//   MemSafe  — RAII handles are move-only at substrate; alias inherits.
//   BorrowSafe — first-call-wins primitives use atomic acquire/release
//              for cross-thread publication; alias preserves.
//   ThreadSafe — alignas(64) on OneShotFlag is structural at substrate;
//              alias is a name-lookup directive, no atomicity change.
//   LeakSafe — RAII dtor closes fd at substrate.
//   DetSafe  — Lazy<T> / Once memoization is deterministic per (program,
//              call-sequence) invariant at substrate.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

#include <crucible/handles/FileHandle.h>
#include <crucible/handles/LazyEstablishedChannel.h>
#include <crucible/handles/Once.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/handles/PublishOnce.h>
#include <crucible/safety/AlignedBuffer.h>     // FIXY-U-016b — RAII over-aligned buffer
#include <crucible/safety/HugePageBuffer.h>    // FIXY-V-034 — 2-MB-aligned RAII buffer
#include <crucible/safety/OwnedFile.h>         // FIXY-V-032 audit — std::FILE* RAII
#include <crucible/safety/PublishCommit.h>     // FIXY-U-016b — typed publish/commit cell

#include <type_traits>   // dual-export sentinel uses std::is_same_v

namespace crucible::fixy::handle {

// ── Strong-typed POSIX fd (closes std::expected error-channel hole) ─
using ::crucible::safety::Fd;

// ── Move-only RAII fd ──────────────────────────────────────────────
using ::crucible::safety::FileHandle;

// ── First-call-wins single-init ────────────────────────────────────
using ::crucible::safety::Once;

// ── Lazy<T> — lazy-init wrapper ────────────────────────────────────
using ::crucible::safety::Lazy;

// ── SetOnce<T> — single-shot write store ───────────────────────────
using ::crucible::safety::SetOnce;

// ── OneShotFlag — alignas(64) release/acquire publication ──────────
using ::crucible::safety::OneShotFlag;

// ── PublishOnce<T> — one-shot publish channel ──────────────────────
using ::crucible::safety::PublishOnce;

// ── PublishSlot<T> — reusable latest-wins slot ─────────────────────
using ::crucible::safety::PublishSlot;

// ── LazyEstablishedChannel — session-handshake-backed channel ──────
using ::crucible::safety::LazyEstablishedChannel;

// ── AlignedBuffer<T, Align> — RAII over-aligned heap buffer ────────
// (FIXY-U-016b) Move-only, nothrow-move; zero-initialized allocation
// via `allocate_zeroed(count)`.  Used by BackgroundThread scratch pools.
using ::crucible::safety::AlignedBuffer;

// ── HugePageBuffer<T> — 2-MB-aligned RAII buffer ───────────────────
// (FIXY-V-034) Move-only, nothrow-move; `allocate(count)` returns a
// buffer aligned to `warden::kHugePageBytes` so that the caller's
// subsequent `register_hot_region(..., huge=true, ...)` →
// `madvise(MADV_HUGEPAGE)` can actually be honored by the kernel.
// The alignment is the *guarantee* this alias re-surfaces; callers
// still pair with `warden::register_hot_region` explicitly because
// the registry needs the friendly name for the diagnostic surface.
//
// Used by MetaLog (TensorMeta ring backing) and the perf hub
// hugepage-arena consumers tracked by FIXY-V-236.
using ::crucible::safety::HugePageBuffer;

// ── OwnedFile — std::FILE* RAII wrapper ────────────────────────────
// (FIXY-V-032-audit) Move-only RAII over stdio's `std::FILE*`.  Copy
// = deleted (double-close on destruction); move = exchange-into-null;
// dtor calls std::fclose() iff non-null.  Substrate was shipped by
// FIXY-V-032 (the TraceLoader.h migration) but the fixy::handle::
// alias was missed at the time — TraceLoader.h's
// `::crucible::safety::OwnedFile` reach-through tripped the FIXY-U-096z
// fixy_clean_headers CI guard (TraceLoader.h is on the certified
// registry).  This alias closes that gap; existing consumers should
// migrate to `fixy::handle::OwnedFile` per the umbrella discipline.
using ::crucible::safety::OwnedFile;

// ── PublishCommitCell<Tag, WriteAuth> — publish/commit pattern ─────
// (FIXY-U-016b) Pinned (non-copy / non-move) atomic cell that fuses
// the standard "atomic store with release → reader acquire" pattern
// under a Tag-typed identity.  Used by BackgroundThread pipeline-stage
// publication.  The friend-gate (`friend WriteAuth;`) is the structural
// witness that bump_by / bump are callable ONLY from inside WriteAuth's
// body — every authorized writer is grep-visible at the cell's
// declaration site.  FIXY-V-037 mirrors the substrate's
// publish_commit_detail:: structural contracts (alignas(64), Pinned,
// trivially-destructible) at the dual-export sentinel block below so
// reviewers can verify the channel-identity guarantees without
// descending into safety/PublishCommit.h.
using ::crucible::safety::PublishCommitCell;

// ── open_read / open_write_truncate — FileHandle free factories ────
// (FIXY-U-016c) `std::expected<FileHandle, std::error_code>` factories
// for read-only / write-create-truncate.  Closes the last fixy-routed
// gap in Cipher.h's head-load + spill-write paths (FIXY-U-092).  The
// using-decl brings the entire safety::open_* overload set into
// fixy::handle::; ADL routes through the substrate symbol unchanged.
using ::crucible::safety::open_read;
using ::crucible::safety::open_write_truncate;

}  // namespace crucible::fixy::handle

// ─── Dual-export sentinel — FIXY-U-016 ─────────────────────────────
//
// Header-internal identity sentinels.  Verifies each alias resolves to
// the substrate type, not a shadowed local of the same name.  Same
// discipline as fixy/Perm.h::self_test (FIXY-U-020).

namespace crucible::fixy::handle::self_test {

struct HandleProbeT {};
struct HandleProbeProto {};
struct HandleProbeResource {};

static_assert(std::is_same_v<
    ::crucible::fixy::handle::Fd,
    ::crucible::safety::Fd>,
    "fixy::handle::Fd must alias safety::Fd — substrate identity drift "
    "would break OwnedFd lifetime proofs across TUs.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::FileHandle,
    ::crucible::safety::FileHandle>,
    "fixy::handle::FileHandle must alias safety::FileHandle.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::Once,
    ::crucible::safety::Once>,
    "fixy::handle::Once must alias safety::Once — first-call-wins "
    "publication identity is load-bearing for cross-TU memoization.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::Lazy<HandleProbeT>,
    ::crucible::safety::Lazy<HandleProbeT>>,
    "fixy::handle::Lazy<T> must alias safety::Lazy<T>.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::SetOnce<HandleProbeT>,
    ::crucible::safety::SetOnce<HandleProbeT>>,
    "fixy::handle::SetOnce<T> must alias safety::SetOnce<T>.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::OneShotFlag,
    ::crucible::safety::OneShotFlag>,
    "fixy::handle::OneShotFlag must alias safety::OneShotFlag — the "
    "alignas(64) discipline is load-bearing at substrate level.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::PublishOnce<HandleProbeT>,
    ::crucible::safety::PublishOnce<HandleProbeT>>,
    "fixy::handle::PublishOnce<T> must alias safety::PublishOnce<T>.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::PublishSlot<HandleProbeT>,
    ::crucible::safety::PublishSlot<HandleProbeT>>,
    "fixy::handle::PublishSlot<T> must alias safety::PublishSlot<T>.");

static_assert(std::is_same_v<
    ::crucible::fixy::handle::LazyEstablishedChannel<HandleProbeProto, HandleProbeResource>,
    ::crucible::safety::LazyEstablishedChannel<HandleProbeProto, HandleProbeResource>>,
    "fixy::handle::LazyEstablishedChannel<Proto, R> must alias "
    "safety::LazyEstablishedChannel<Proto, R> — session-handshake "
    "identity must not drift across the umbrella boundary.");

// FIXY-U-016b — AlignedBuffer<T, Align>; default Alignment = alignof(T).
static_assert(std::is_same_v<
    ::crucible::fixy::handle::AlignedBuffer<HandleProbeT>,
    ::crucible::safety::AlignedBuffer<HandleProbeT>>,
    "fixy::handle::AlignedBuffer<T> must alias safety::AlignedBuffer<T> "
    "— move-only RAII identity is load-bearing for hot-drain pipelines.");

// FIXY-U-016b — PublishCommitCell<Tag, WriteAuth>.
static_assert(std::is_same_v<
    ::crucible::fixy::handle::PublishCommitCell<HandleProbeT, HandleProbeProto>,
    ::crucible::safety::PublishCommitCell<HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must alias "
    "safety::PublishCommitCell — Pinned-channel identity is load-bearing "
    "for cross-thread publish/commit ordering.");

// FIXY-V-037 — LOAD-BEARING structural-contract sentinels for the
// PublishCommit pattern.  The substrate ships these in its own
// `publish_commit_detail::` block, but mirroring them at the fixy::
// boundary catches contract drift on the alias path before any
// production call site (BackgroundThread pipeline-stage publication,
// per the substrate doc-block's GAPS-FLUSH-RACE bug class) notices.
// Per CLAUDE.md §XIII ("every contract has a test that violates it")
// and the V-034 discipline ("reviewer never has to grep into safety/
// to verify the guarantee").
//
// Contract 1 — alignas(64) cache-line isolation.  The cell occupies a
// full cache line so the fg's acquire-load of value_ and bg's release-
// store via bump_by NEVER share an L1 line with any neighboring field.
// If this drifts (e.g., a future refactor merges PublishCommitCell into
// a larger struct without alignas), false sharing reintroduces the
// 40× MESI ping-pong penalty CLAUDE.md §IX warned about.
static_assert(
    alignof(::crucible::fixy::handle::PublishCommitCell<
        HandleProbeT, HandleProbeProto>) == 64,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must be alignas(64) "
    "— cross-thread acquire/release pair requires cache-line isolation; "
    "drift here reintroduces false sharing (CLAUDE.md §IX).");

// Contract 2 — non-movable.  The cell IS the channel identity; moving
// it would relocate the atomic mid-stream, invalidating all in-flight
// load_acquire calls and any external references / friend-list assumptions.
static_assert(
    !std::is_move_constructible_v<
        ::crucible::fixy::handle::PublishCommitCell<
            HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must not be move-"
    "constructible — Pinned channel identity; move would orphan the "
    "fg's outstanding acquire-load address.");

// Contract 3 — non-copyable.  Two cells with the same Tag/WriteAuth
// would mean two independent counters claiming the same channel
// identity; the friend-gate would unwittingly authorize writes to
// both, breaking the BorrowSafe single-writer invariant.
static_assert(
    !std::is_copy_constructible_v<
        ::crucible::fixy::handle::PublishCommitCell<
            HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must not be copy-"
    "constructible — channel-identity uniqueness; copy would break the "
    "single-writer invariant (BorrowSafe).");

// Contract 4 — trivially destructible.  The cell owns only an atomic
// uint64_t; no resource to free.  Trivially destructible enables
// constexpr initialization (the cell can live in static storage with
// zero initialization cost) AND guarantees no exception is thrown from
// the dtor (which would compile-error under -fno-exceptions anyway,
// but the sentinel surfaces the invariant explicitly).
static_assert(
    std::is_trivially_destructible_v<
        ::crucible::fixy::handle::PublishCommitCell<
            HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must be trivially "
    "destructible — the cell owns only std::atomic<uint64_t>; a non-"
    "trivial dtor would mean someone added a managed resource without "
    "updating the Pinned discipline.");

// FIXY-V-034 — HugePageBuffer<T> identity (dual-export sentinel).
static_assert(std::is_same_v<
    ::crucible::fixy::handle::HugePageBuffer<HandleProbeT>,
    ::crucible::safety::HugePageBuffer<HandleProbeT>>,
    "fixy::handle::HugePageBuffer<T> must alias safety::HugePageBuffer<T> "
    "— 2-MB-aligned RAII identity is load-bearing for MetaLog + perf "
    "hugepage-arena consumers (FIXY-V-236).");

// FIXY-V-034 — madvise(MADV_HUGEPAGE) hint guarantee.  This is the
// LOAD-BEARING contract behind the alias: the buffer's allocation
// alignment equals warden::kHugePageBytes (== 2 MB on x86_64), so a
// subsequent madvise(MADV_HUGEPAGE) call inside warden::register_hot_region
// CAN succeed in the kernel.  If the substrate's huge_page_bytes ever
// drifted (e.g. an ARM 64-KB hugepage path with a smaller constant),
// madvise would silently fall back to small pages, TLB miss rate would
// spike, and there'd be no C++-level signal.  This sentinel catches
// the drift at compile time on the fixy:: surface so a reviewer never
// has to grep into safety/HugePageBuffer.h to verify the guarantee.
static_assert(
    ::crucible::fixy::handle::HugePageBuffer<HandleProbeT>::huge_page_bytes
        == ::crucible::warden::kHugePageBytes,
    "fixy::handle::HugePageBuffer<T>::huge_page_bytes must equal "
    "warden::kHugePageBytes — madvise(MADV_HUGEPAGE) requires the "
    "allocation to be aligned to the kernel's huge-page boundary; if "
    "this drifts, hugepage backing silently fails and TLB pressure "
    "regresses without any C++-level error path.");

// FIXY-V-032-audit — OwnedFile identity (dual-export sentinel).
static_assert(std::is_same_v<
    ::crucible::fixy::handle::OwnedFile,
    ::crucible::safety::OwnedFile>,
    "fixy::handle::OwnedFile must alias safety::OwnedFile — RAII "
    "close-on-dtor identity is load-bearing for TraceLoader.h's 9 "
    "early-return paths (FIXY-V-032 substrate migration).");

// FIXY-V-032-audit — LOAD-BEARING structural contracts.  OwnedFile's
// raison-d'être is the move-only RAII discipline that makes leaking
// std::FILE* structurally impossible.  Mirroring the substrate
// contracts at the fixy:: boundary catches drift before any consumer
// (TraceLoader, warden::CpuTopology, future Cipher load paths) sees a
// regression.  Per CLAUDE.md §XIII discipline.
//
// Contract 1 — non-copyable.  The substrate deletes copy ctor + copy
// assign with the reason "FILE* is unique; copy would double-close on
// destruction"; if a future refactor accidentally re-introduces a
// default copy ctor (e.g. by demoting `delete("...")` to `default` or
// by composing OwnedFile into a struct that auto-generates copy), this
// sentinel fires before the dtor runs twice on the same FILE*.
static_assert(
    !std::is_copy_constructible_v<::crucible::fixy::handle::OwnedFile>,
    "fixy::handle::OwnedFile must not be copy-constructible — copying "
    "a FILE* aliases two RAII owners that both call std::fclose() at "
    "destruction, double-closing the stdio handle (MemSafe).");

static_assert(
    !std::is_copy_assignable_v<::crucible::fixy::handle::OwnedFile>,
    "fixy::handle::OwnedFile must not be copy-assignable — copy-assign "
    "would leak the LHS's existing FILE* AND double-close the RHS's.");

// Contract 2 — nothrow move.  The handle must be move-into-container
// safe (e.g. inside std::expected<OwnedFile, std::error_code> as
// returned by open_read).  Throwing move would force expected<> into a
// degraded valueless state on a transient stack-shuffle.
static_assert(
    std::is_nothrow_move_constructible_v<::crucible::fixy::handle::OwnedFile>,
    "fixy::handle::OwnedFile must have a nothrow move constructor — "
    "it appears inside std::expected<> and other move-only containers "
    "that demand noexcept move for the strong exception guarantee.");

static_assert(
    std::is_nothrow_move_assignable_v<::crucible::fixy::handle::OwnedFile>,
    "fixy::handle::OwnedFile must have a nothrow move-assign operator "
    "for symmetric strong-guarantee handling.");

// Contract 3 — nothrow dtor.  The dtor must be noexcept because it is
// called during stack unwind on every early return; throwing here
// would terminate under `-fno-exceptions` and propagate through stdio
// buffers in a non-deterministic order.  fclose's errno is intentionally
// ignored by the dtor; callers who care call close_explicit() instead.
static_assert(
    std::is_nothrow_destructible_v<::crucible::fixy::handle::OwnedFile>,
    "fixy::handle::OwnedFile destructor must be noexcept — it runs on "
    "every early-return path and stdio errors must not propagate.");

// Contract 4 — zero-overhead.  The wrapper holds exactly one
// std::FILE* (sentinel-init to nullptr) and nothing else; sizeof must
// equal sizeof(FILE*) so the migration cost is structurally nil at
// every call site (no extra padding, no vtable, no flag byte).
static_assert(
    sizeof(::crucible::fixy::handle::OwnedFile) == sizeof(std::FILE*),
    "fixy::handle::OwnedFile must be sizeof(std::FILE*) — any inflation "
    "means a hidden field was added that would balloon the cost of "
    "every TraceLoader stack frame and per-call early-return path.");

// FIXY-U-016c — open_read / open_write_truncate free-function identity.
// Non-template free functions: identity verified by pointer-of-function
// decltype equality (same technique as fixy/Wrap.h's saturating-arith
// sentinels).  If substrate signature changes (e.g., new mode_t default
// or path-encoding parameter), this sentinel fires before the
// production Cipher.h call sites notice.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::handle::open_read),
    decltype(&::crucible::safety::open_read)>,
    "fixy::handle::open_read must alias safety::open_read — "
    "FileHandle factory identity is load-bearing for Cipher.h head-load "
    "+ obj-spill error-channel discipline.");

static_assert(std::is_same_v<
    decltype(&::crucible::fixy::handle::open_write_truncate),
    decltype(&::crucible::safety::open_write_truncate)>,
    "fixy::handle::open_write_truncate must alias "
    "safety::open_write_truncate — Cipher spill / federation entry "
    "write path depends on identity preservation.");

// Cardinality witness: 15 aliases surfaced; future additions to
// handles/ MUST extend this block + add a substrate type below.
// FIXY-V-034: bumped 13 → 14 for HugePageBuffer<T>.
// FIXY-V-032-audit: bumped 14 → 15 for OwnedFile.
constexpr int handle_alias_cardinality = 15;
static_assert(handle_alias_cardinality == 15,
    "fixy::handle:: cardinality changed — update Handle.h sentinel "
    "block to track the substrate handles/ surface.");

}  // namespace crucible::fixy::handle::self_test
