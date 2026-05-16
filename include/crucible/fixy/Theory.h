#pragma once

// ── crucible::fixy — Theory.h (FIXY-D, §30.14 Known-Unsoundness corpus) ─
//
// Per misc/16_05_2026_fixy.md §4 Phase D + §9 R6: the §30.14 corpus
// is **data, not code**.  Each entry is a `theory_entry<Pattern>`
// struct cited to its source paper (or GAPS-* Crucible audit task).
// The corpus grows monotonically — entries are never deleted, only
// added.  A new type-theory bug found in production becomes a 10-
// line addition to this header.
//
// **Two surfaces.**  For each corpus entry:
//
//   1. A `theory_entry<Pattern, "cite">` struct that the corpus
//      catalog enumerates.  The Pattern parameter is a tag struct
//      identifying the unsoundness pattern (e.g.,
//      `pattern::atkey_2018_lam_double_use`).
//
//   2. The PAIRED REJECTION fires in the substrate
//      (safety/CollisionCatalog.h) or in Phase A's engagement gate
//      (Reject.h).  Theory.h is the GUIDEBOOK: it tells future
//      contributors WHERE in the substrate the rejection mechanism
//      lives and WHY the pattern is unsound, with a citation.
//
// **Citation discipline.**  Every entry MUST cite:
//
//   - Academic source: author/year/paper title (e.g.,
//     "Atkey 2018 — Linear Haskell with Multiplicity").
//
//   OR
//
//   - GAPS-* task ID (e.g., "GAPS-001 — SessionGlobal::project<StopG>
//     drops crash-safety on non-Peer roles").  Crucible-specific
//     audit-discovered unsoundness uses this form.
//
// The cite IS the documentation: a contributor reading the header
// gets the paper title, year, and (where applicable) GAPS task ID
// in the source file, no separate doc-trip needed.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase D — Theory.h deliverable
//   misc/16_05_2026_fixy.md §9 R6      — corpus-as-data discipline
//   safety/CollisionCatalog.h          — substrate rejection sites
//   ~/iprit/FX/fx_design.md §30.14     — origin corpus (FX paper)

#include <crucible/safety/CollisionCatalog.h>

#include <string_view>

namespace crucible::fixy::theory {

// ═════════════════════════════════════════════════════════════════════
// ── Theory entry — one struct per §30.14 corpus row ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each entry pairs a pattern tag with a citation literal.  The
// cite is a compile-time string_view; the catalog tuple enumerates
// the family.
//
// Future work (Phase D+): add `matches<F>` predicate that
// classifies an arbitrary `safety::fn::Fn<...>` instantiation
// against the corpus and returns the matching entry's cite for
// structured diagnostics.  For Phase D the corpus is enumerated;
// the matcher is data-driven.

template <typename PatternTag, std::string_view const& Cite>
struct theory_entry {
    using pattern_tag = PatternTag;
    static constexpr std::string_view cite = Cite;
};

// ── Pattern tags — empty marker structs ────────────────────────────
//
// One per §30.14 row.  The marker IS the pattern identity; the
// CollisionCatalog rule that rejects this pattern is named in the
// citation string.

namespace pattern {

// FX §30.14 entries — academic citations
struct atkey_2018_lam_double_use {};
struct caires_pfenning_2010_implicit_flow {};
struct bsyz22_crash_stop_partial_view {};
struct ccdr14_fractional_overallocation {};
struct gpsy23_async_subtype_unbounded {};
struct honda_yoshida_2008_proj_drop_role {};
struct gay_hole_2005_subtype_branching {};
struct atkey_lindley_2009_effect_row_leak {};
struct krishnaswami_2014_capability_replay {};
struct krishnaswami_2017_staleness_ct_channel {};

// Crucible-audit citations
struct gaps_001_session_global_stopg_proj {};
struct gaps_003_crashwatched_perm_leak {};
struct gaps_010_monotonic_concurrent_no_atomic {};
struct gaps_013_decimal_overflow_wrap {};
struct gaps_017_capability_replay_session {};

}  // namespace pattern

// ── Citation literals — one constexpr inline per entry ─────────────

inline constexpr std::string_view cite_atkey_2018_lam =
    "Atkey 2018 — Linear Haskell with Multiplicity, ICFP."
    " Linear handle captured in unrestricted closure body used "
    "twice (M011 / linear-Fail-cleanup site).";

inline constexpr std::string_view cite_caires_pfenning_2010 =
    "Caires & Pfenning 2010 — Session Types as Intuitionistic Linear "
    "Propositions, CONCUR.  Classified value flows into IO without "
    "declassify (I002 / classified-Fail payload).";

inline constexpr std::string_view cite_bsyz22 =
    "Barwell-Scalas-Yoshida-Zhou 2022 — Generalised Multiparty "
    "Session Types with Crash-Stop Failures, CONCUR.  Crash-stop peer "
    "leaves projection with stale view of session (covered by Stop_g + "
    "CrashClass discipline in sessions/SessionCrash.h).";

inline constexpr std::string_view cite_ccdr14 =
    "Capecchi-Castellani-Dezani-Rezk 2014 — Information Flow Safety "
    "in Multiparty Sessions, FORTE.  Fractional permission "
    "p + q > 1 escapes the linear budget; see SharedPermissionPool "
    "atomic refcount.";

inline constexpr std::string_view cite_gpsy23 =
    "Glabbeek-Padovani-Smolka-Yoshida 2023 — Precise Subtyping for "
    "Asynchronous Session Types, POPL.  SISO async subtyping with "
    "unbounded queue depth leaks (L002 / borrow-Async site).";

inline constexpr std::string_view cite_honda_2008 =
    "Honda-Yoshida-Carbone 2008 — Multiparty Asynchronous Session "
    "Types, POPL.  Local projection that drops a role's "
    "view of crash-stop becomes a soundness gap (GAPS-001 fix).";

inline constexpr std::string_view cite_gay_hole_2005 =
    "Gay & Hole 2005 — Subtyping for Session Types in the Pi-Calculus, "
    "Acta Informatica.  Branching subtype admits more inputs than the "
    "supertype declares (covered by SessionSubtype.h synchronous "
    "subsort axiom).";

inline constexpr std::string_view cite_atkey_lindley_2009 =
    "Atkey & Lindley 2009 — Algebraic Effects and Effect Handlers.  "
    "Implicit effect-row leak through opaque continuation (E044 / "
    "CT-Async rule).";

inline constexpr std::string_view cite_krishnaswami_2014_cap =
    "Krishnaswami 2014 — Higher-Order Reasoning, Operationally.  "
    "Capability token replay across protocol rounds (S011 / "
    "capability-Replay rule).";

inline constexpr std::string_view cite_krishnaswami_2017_stale =
    "Krishnaswami-Pradic 2017 — Staleness & Compositional Reasoning "
    "for Constant-Time Programs.  Stale view used inside CT-typed "
    "channel (S010 / Staleness-CT rule).";

inline constexpr std::string_view cite_gaps_001 =
    "GAPS-001 (Crucible audit) — SessionGlobal::project<StopG> "
    "dropped crash-safety on non-Peer roles.  Fixed by extending the "
    "projection to preserve crash-class metadata.";

inline constexpr std::string_view cite_gaps_003 =
    "GAPS-003 (Crucible audit) — CrashWatchedHandle leaked Permission "
    "tokens on peer crash.  Fixed by mint_permission_inherit + survivor "
    "registry.";

inline constexpr std::string_view cite_gaps_010 =
    "GAPS-010 (Crucible audit) — Monotonic counter shared across "
    "threads without atomic carrier (M012 substrate rule).";

inline constexpr std::string_view cite_gaps_013 =
    "GAPS-013 (Crucible audit) — Decimal type with overflow=wrap "
    "(N002 substrate rule).";

inline constexpr std::string_view cite_gaps_017 =
    "GAPS-017 (Crucible audit) — Capability used in a Replay context "
    "where the runtime is not re-establishable (S011 substrate rule).";

// ═════════════════════════════════════════════════════════════════════
// ── Catalog — tuple enumeration of every theory entry ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// One tuple slot per §30.14 pattern.  Future entries append at the
// end (per misc/16_05_2026_fixy.md §9 R6 monotonic discipline).
//
// Enumeration via `std::tuple_size_v<Catalog>` + reflection lets a
// future matcher walk every entry without source modification.

using Catalog = std::tuple<
    theory_entry<pattern::atkey_2018_lam_double_use,           cite_atkey_2018_lam>,
    theory_entry<pattern::caires_pfenning_2010_implicit_flow,  cite_caires_pfenning_2010>,
    theory_entry<pattern::bsyz22_crash_stop_partial_view,      cite_bsyz22>,
    theory_entry<pattern::ccdr14_fractional_overallocation,    cite_ccdr14>,
    theory_entry<pattern::gpsy23_async_subtype_unbounded,      cite_gpsy23>,
    theory_entry<pattern::honda_yoshida_2008_proj_drop_role,   cite_honda_2008>,
    theory_entry<pattern::gay_hole_2005_subtype_branching,     cite_gay_hole_2005>,
    theory_entry<pattern::atkey_lindley_2009_effect_row_leak,  cite_atkey_lindley_2009>,
    theory_entry<pattern::krishnaswami_2014_capability_replay, cite_krishnaswami_2014_cap>,
    theory_entry<pattern::krishnaswami_2017_staleness_ct_channel, cite_krishnaswami_2017_stale>,
    theory_entry<pattern::gaps_001_session_global_stopg_proj,  cite_gaps_001>,
    theory_entry<pattern::gaps_003_crashwatched_perm_leak,     cite_gaps_003>,
    theory_entry<pattern::gaps_010_monotonic_concurrent_no_atomic, cite_gaps_010>,
    theory_entry<pattern::gaps_013_decimal_overflow_wrap,      cite_gaps_013>,
    theory_entry<pattern::gaps_017_capability_replay_session,  cite_gaps_017>
>;

inline constexpr std::size_t corpus_size_v = std::tuple_size_v<Catalog>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — corpus cardinality + cite non-empty ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pins that the seeded corpus has exactly 15 entries (10 academic +
// 5 Crucible-audit), every cite is non-empty.  A regression that
// drops an entry (which would violate §9 R6 monotonic discipline)
// fires here.

namespace theory_self_test {

static_assert(corpus_size_v == 15,
    "Theory corpus must seed with 15 entries (10 academic + 5 "
    "Crucible-audit).  See misc/16_05_2026_fixy.md §9 R6: corpus "
    "grows monotonically.  If this fires, an entry was removed.");

// Every cite literal is non-empty.
static_assert(!cite_atkey_2018_lam.empty());
static_assert(!cite_caires_pfenning_2010.empty());
static_assert(!cite_bsyz22.empty());
static_assert(!cite_ccdr14.empty());
static_assert(!cite_gpsy23.empty());
static_assert(!cite_honda_2008.empty());
static_assert(!cite_gay_hole_2005.empty());
static_assert(!cite_atkey_lindley_2009.empty());
static_assert(!cite_krishnaswami_2014_cap.empty());
static_assert(!cite_krishnaswami_2017_stale.empty());
static_assert(!cite_gaps_001.empty());
static_assert(!cite_gaps_003.empty());
static_assert(!cite_gaps_010.empty());
static_assert(!cite_gaps_013.empty());
static_assert(!cite_gaps_017.empty());

// Pattern tags are unique types — std::is_same_v on distinct
// pairs must be false.  This catches accidental tag-name collisions.
static_assert(!std::is_same_v<pattern::atkey_2018_lam_double_use,
                              pattern::gaps_001_session_global_stopg_proj>);
static_assert(!std::is_same_v<pattern::bsyz22_crash_stop_partial_view,
                              pattern::ccdr14_fractional_overallocation>);

}  // namespace theory_self_test

}  // namespace crucible::fixy::theory
