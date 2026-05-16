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
#include <crucible/fixy/Contract.h>
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Resolve.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Stance.h>

#define CRUCIBLE_FIXY 1

// ─── Version stamp ──────────────────────────────────────────────────
//
// Major: load-bearing surface change (new dim, removed dim, stance
//        rename, mint factory renamed).
// Minor: additive (new grant tag, new stance, new diagnostic).
// Patch: bug fix, doc-comment edit, internal refactor.

#define CRUCIBLE_FIXY_VERSION_MAJOR 0
#define CRUCIBLE_FIXY_VERSION_MINOR 3  // 0.3.0: Phase A + B + C alias re-exports
#define CRUCIBLE_FIXY_VERSION_PATCH 0
