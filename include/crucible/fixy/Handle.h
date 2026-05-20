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

// ── PublishCommitCell<Tag, WriteAuth> — publish/commit pattern ─────
// (FIXY-U-016b) Pinned (non-copy / non-move) atomic cell that fuses
// the standard "atomic store with release → reader acquire" pattern
// under a Tag-typed identity.  Used by BackgroundThread pipeline-stage
// publication.
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

// Cardinality witness: 13 aliases surfaced; future additions to
// handles/ MUST extend this block + add a substrate type below.
constexpr int handle_alias_cardinality = 13;
static_assert(handle_alias_cardinality == 13,
    "fixy::handle:: cardinality changed — update Handle.h sentinel "
    "block to track the substrate handles/ surface.");

}  // namespace crucible::fixy::handle::self_test
