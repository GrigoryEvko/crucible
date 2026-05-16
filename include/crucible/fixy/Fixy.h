#pragma once

// ── crucible::fixy — Fixy.h (umbrella, FIXY-B5) ────────────────────────
//
// Single-include entry point for the reject-by-default discipline
// layer.  Including this header brings:
//
//   * Phase A: Dim.h + Default.h + Grant.h + Reject.h + AllStrict.h
//     — the 20-dim engagement gate, strict-default catalog, grant tag
//     library, IsAccepted concept, FixyNotEngaged_* diagnostics,
//     AllStrictAcceptPack helper.
//
//   * Phase B: Resolve.h + Fn.h + Stance.h + Rules.h — fixy::fn<>
//     aggregator (with two-layer gate: IsAccepted + ValidComposition),
//     mint_fn / mint_fn_for factories, 8 canonical stances
//     (PureLinear, PureCopy, IoFunction, BgWorker, CtCrypto,
//     SecretConsumer, PublicEmit<P>, AsyncEndpoint), 12-rule §6.8
//     collision catalog re-export.
//
//   * Phase C: Sess.h + Mach.h + Contract.h — alias re-exports over
//     the existing substrate (sessions/Session.h family, safety/
//     Machine.h, safety/Contract.h Pre/Post macros).  Greenfield
//     headers use fixy::sess::*, fixy::mach::*, and the
//     CRUCIBLE_PRE / CRUCIBLE_POST macros via fixy/Contract.h so
//     the discipline-surface spelling matches across all axes.
//
//   * Phase D: Profile.h + Theory.h — sketch/release profile
//     toggle (CRUCIBLE_FIXY_SKETCH preprocessor symbol) and the
//     §30.14 Known-Unsoundness corpus (15 seeded entries:
//     10 academic + 5 Crucible-audit GAPS-*).  Corpus is data,
//     monotonically grows; per doc §9 R6 entries are 10-line
//     additions, never deletions.
//
// **Discipline pointer.** Every greenfield Crucible header added after
// 17 May 2026 composes against `fixy::*` via this umbrella, NOT
// against raw `safety::fn::Fn<...>`.  See misc/16_05_2026_fixy.md §5
// for the band-classification policy.
//
// **Single-symbol activation.** `#include <crucible/fixy/Fixy.h>`
// defines `CRUCIBLE_FIXY=1` so downstream CMake gates (greenfield
// directories that opt into `CRUCIBLE_FIXY_ONLY ON`) can detect
// engagement at the macro level.

#include <crucible/fixy/AllStrict.h>
#include <crucible/fixy/ArgPromote.h>
#include <crucible/fixy/Call.h>
#include <crucible/fixy/Contract.h>
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Federation.h>
#include <crucible/fixy/Flow.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Profile.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Resolve.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Stance.h>
#include <crucible/fixy/Theory.h>
#include <crucible/fixy/Value.h>
#include <crucible/fixy/WireGrade.h>
// FIXY-G13: stance versioning + lifecycle.
#include <crucible/fixy/stance/Migration.h>
#include <crucible/fixy/stance/Version.h>
#include <crucible/fixy/stance/WireGradeV2.h>
// FIXY-G14: seam theory matcher.
#include <crucible/fixy/theory/Seam.h>

#define CRUCIBLE_FIXY 1

// ─── Ergonomic top-level aliases ────────────────────────────────────
//
// Tighten the common consumer path: `fixy::theory_matches<F>()` and
// `fixy::theory_cite_hash_v<E>` instead of the verbose
// `fixy::theory::which_pattern_matches<F>()` / `theory::theory_cite_hash_v<E>`.
// The theory namespace stays the canonical home; these aliases are
// surface ergonomics for production call sites.

namespace crucible::fixy {

// Short-form alias for the theory matcher.  Returns the first matching
// corpus entry's cite, or empty string_view when no §30.14 pattern fires.
template <typename F>
[[nodiscard]] consteval auto theory_matches() noexcept {
    return theory::which_pattern_matches<F>();
}

// Short-form alias for cite-hash federation key contribution.
template <typename Entry>
inline constexpr auto theory_cite_hash_v = theory::theory_cite_hash_v<Entry>;

// Short-form alias for corpus cardinality.
inline constexpr auto theory_corpus_size_v = theory::corpus_size_v;

}  // namespace crucible::fixy

// ─── Version stamp ──────────────────────────────────────────────────
//
// Major: load-bearing surface change (new dim, removed dim, stance
//        rename, mint factory renamed).
// Minor: additive (new grant tag, new stance, new diagnostic).
// Patch: bug fix, doc-comment edit, internal refactor.

#define CRUCIBLE_FIXY_VERSION_MAJOR 0
#define CRUCIBLE_FIXY_VERSION_MINOR 5  // 0.5.0: theory matcher + cite_hash + strict_fn/sketch_fn pins
#define CRUCIBLE_FIXY_VERSION_PATCH 0
