#pragma once

// ── crucible::fixy — Contract.h (FIXY-C, alias over safety/Contract.h) ──
//
// Stable surface for the contract macro pair.  `safety/Contract.h` is
// the existing umbrella that pulls in Pre.h + Post.h (the consteval-
// aware contract macros that close the GCC 16.1.1 P2900 bypass-on-
// foldable-bodies hole; see misc/08_05_2026_harness.md §4).
//
// **Macros, not types.**  Unlike Sess.h and Mach.h which alias
// templates and functions, Contract.h's surface is preprocessor:
// `CRUCIBLE_PRE(cond)` and `CRUCIBLE_POST(retvar, cond)`.  The
// macros expand identically under any include path; this header
// re-exports the include with a fixy-namespace doc-comment but no
// macro renaming (renaming preprocessor symbols across the codebase
// would break every existing call site).
//
// **Future Cipher migration.**  Doc §4 Phase C envisioned
// `fixy::contract::version<N>`, `migration<from, to>`, `format<Wire>`,
// `access<Mode>` aliases over Cipher's serialization + migration
// machinery.  The substrate types for those don't exist yet —
// Cipher's binary format versioning currently lives as inline
// constants in `CDAG_VERSION` (Tagged<uint32_t, source::FormatVersion>
// per WRAP-Serialize-4) and not as standalone version<N> templates.
// When those substrate primitives ship (Phase D / Cipher migration
// epic), they'll be aliased in here.  Until then, this header is
// strictly the macro umbrella.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   #include <crucible/fixy/Contract.h>
//
//   constexpr T fn(StructType const& s) noexcept {
//       CRUCIBLE_PRE(invariant(s));
//       T r = compute(s);
//       CRUCIBLE_POST(r, post_invariant(r));
//       return r;
//   }
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase C — Contract.h deliverable
//   misc/08_05_2026_harness.md §4      — Tier-0 VC harness rationale
//   safety/Contract.h                   — substrate umbrella (Pre.h + Post.h)
//   safety/Pre.h                        — consteval-fire pre-condition macro
//   safety/Post.h                       — consteval-fire post-condition macro

#include <crucible/safety/Contract.h>
#include <crucible/safety/Decide.h>

// No namespace re-open for the macros: CRUCIBLE_PRE / CRUCIBLE_POST
// are preprocessor symbols.  Their include path through fixy::contract
// IS the discipline surface promise; the call form remains identical.

// ─── decide::* named-predicate catalog re-export ──────────────────────
//
// The substrate's `crucible::decide::*` catalog (14 named VC
// predicates per CLAUDE.md §X / feedback_decide_catalog) is the
// canonical surface every CONTRACT-* migration cites at `CRUCIBLE_PRE`
// / `CRUCIBLE_POST` sites.  Re-exporting under `fixy::decide` keeps
// fixy-only call sites one namespace path away from the load-bearing
// predicates.
//
// **Surface guarantee.**  `fixy::decide::*` IS `crucible::decide::*`
// by namespace-alias semantics — every function template, every
// `Interval<T>` instantiation, every predicate constant is the
// substrate symbol.  A future substrate rename surfaces here first
// (rather than at every greenfield call site).

namespace crucible::fixy {
namespace decide = ::crucible::decide;
}  // namespace crucible::fixy
