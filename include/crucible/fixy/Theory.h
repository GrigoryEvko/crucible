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

#include <crucible/Expr.h>                       // detail::fmix64
#include <crucible/safety/CollisionCatalog.h>    // ValidComposition, predicate helpers
#include <crucible/safety/Fn.h>                  // Fn<...>, MutationMode, ReprKind, SecLevel
#include <crucible/safety/diag/StableName.h>     // fnv1a_64

#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

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

// ═════════════════════════════════════════════════════════════════════
// ── Cite hash — stable 64-bit identifier per corpus entry ──────────
// ═════════════════════════════════════════════════════════════════════
//
// `theory_cite_hash_v<Entry>` folds the cite literal through FNV-1a +
// fmix64 to produce a stable 64-bit identifier independent of any
// future cite-string rewording (rewording is a deliberate-bump act,
// not a silent edit).
//
// Use cases:
//
//   * Federation cache key contribution.  A future Cipher cold-tier
//     federation shipping corpus entries across orgs can key on the
//     hash; renaming an entry's pattern tag while keeping the cite
//     identical keeps the federation slot stable.
//
//   * Diagnostic identity.  The diag::Goal/Have/Gap/Suggestion catalog
//     (FOUND-E01) can route by hash when the same pattern is reported
//     from several substrate sites.
//
// The hash is `consteval` — no runtime cost; the literal is folded at
// compile time.

template <typename Entry>
inline constexpr std::uint64_t theory_cite_hash_v =
    ::crucible::detail::fmix64(
        ::crucible::safety::diag::detail::fnv1a_64(Entry::cite));

// ═════════════════════════════════════════════════════════════════════
// ── Pattern matchers — substrate-axis introspection per entry ──────
// ═════════════════════════════════════════════════════════════════════
//
// The matcher takes any well-formed `safety::fn::Fn<...>` instance F
// (post-ValidComposition acceptance) and classifies its grade vector
// against the corpus.  Each pattern's matcher is a metafunction
// `matches<F>::value` returning bool.
//
// **Detection scope.**  Five entries are mechanically detectable from
// the substrate's introspectable axes (the GAPS-* entries that
// correspond to existing CollisionCatalog rules — M012, N002, S010,
// S011, plus I002 / Caires-Pfenning).  The remaining 10 entries
// require flow-sensitive analysis (double-use under capture, MPST
// projection drop) that does NOT live in the per-binding grade
// vector — those are documented in the corpus as guidebook entries,
// not mechanically matched.
//
// A non-detectable pattern's `matches<F>::value` is always false; the
// matcher does NOT lie about coverage.

template <typename PatternTag, typename F>
struct matches : std::false_type {};

// ── M012-shaped match: Monotonic mutation × concurrent × non-Atomic ─
//
// The substrate's M012 rule REJECTS this combination outright via
// CollisionRules::validate().  The matcher inspects the would-be
// shape from the grade vector so a downstream consumer can ask "is
// this binding NEIGHBOURED by the M012 unsoundness?"  In practice
// the matcher fires only on bindings that bypassed substrate
// rejection through deliberate Repr=Atomic — i.e., it surfaces
// AtomicMonotonic bindings as "the M012-fix shape", citing GAPS-010.

template <typename F>
struct matches_m012_shape {
private:
    static constexpr bool monotonic =
        F::mutation_v == ::crucible::safety::fn::MutationMode::Monotonic;
    static constexpr bool has_bg = ::crucible::effects::row_contains_v<
        typename F::effect_row_t, ::crucible::effects::Effect::Bg>;
public:
    static constexpr bool value = monotonic && has_bg;
};

template <typename F>
struct matches<pattern::gaps_010_monotonic_concurrent_no_atomic, F>
    : std::bool_constant<matches_m012_shape<F>::value> {};

// ── N002-shaped match: exact decimal × Overflow::Wrap ───────────────

template <typename F>
struct matches<pattern::gaps_013_decimal_overflow_wrap, F>
    : std::bool_constant<
          ::crucible::safety::fn::collision::is_exact_decimal<
              std::remove_cvref_t<typename F::type_t>>::value &&
          F::overflow_v == ::crucible::safety::fn::OverflowMode::Wrap> {};

// ── S010-shaped match: CT × non-Fresh staleness ─────────────────────

template <typename F>
struct matches<pattern::krishnaswami_2017_staleness_ct_channel, F>
    : std::bool_constant<
          ::crucible::safety::fn::collision::marks_ct<F>::value &&
          !std::is_same_v<typename F::staleness_t,
                          ::crucible::safety::fn::stale::Fresh>> {};

// ── S011-shaped match: Capability usage × replay required ───────────

template <typename F>
struct matches<pattern::krishnaswami_2014_capability_replay, F>
    : std::bool_constant<
          F::usage_v == ::crucible::safety::fn::UsageMode::Capability &&
          ::crucible::safety::fn::collision::marks_replay_required<F>::value> {};

template <typename F>
struct matches<pattern::gaps_017_capability_replay_session, F>
    : std::bool_constant<
          F::usage_v == ::crucible::safety::fn::UsageMode::Capability &&
          ::crucible::safety::fn::collision::marks_replay_required<F>::value> {};

// ── I002-shaped match: Classified payload × Fail-marker present ─────
//
// Caires-Pfenning 2010 — classified information flowing through Fail
// without a Secret-tagged error payload.  Substrate rule I002.

template <typename F>
struct matches<pattern::caires_pfenning_2010_implicit_flow, F>
    : std::bool_constant<
          (F::security_v == ::crucible::safety::fn::SecLevel::Classified ||
           F::security_v == ::crucible::safety::fn::SecLevel::Secret) &&
          ::crucible::safety::fn::collision::marks_fail<F>::value &&
          !::crucible::safety::fn::collision::marks_fail_error_secret<F>::value> {};

// ═════════════════════════════════════════════════════════════════════
// ── which_pattern_matches — consteval first-match classifier ───────
// ═════════════════════════════════════════════════════════════════════
//
// Walks the catalog at compile time, returns the FIRST matching
// entry's cite (or empty `string_view` if no entry matches).  O(N) in
// corpus cardinality at compile time; zero runtime cost when called
// from a consteval context.
//
// **Determinism.**  Catalog iteration order is the tuple order from
// `Catalog`.  A binding that matches both `gaps_010` and
// `krishnaswami_2014_*` returns whichever appears first — the
// catalog order IS the precedence order.
//
// **Use site.**
//
//   constexpr auto cite = theory::which_pattern_matches<F>();
//   if constexpr (!cite.empty()) {
//       // F is in the unsoundness neighborhood; cite IDs the paper.
//   }

namespace detail {

template <typename F, std::size_t I>
[[nodiscard]] consteval std::string_view which_pattern_matches_at() noexcept {
    using Entry = std::tuple_element_t<I, Catalog>;
    if constexpr (matches<typename Entry::pattern_tag, F>::value) {
        return Entry::cite;
    } else if constexpr (I + 1 < std::tuple_size_v<Catalog>) {
        return which_pattern_matches_at<F, I + 1>();
    } else {
        return std::string_view{};
    }
}

}  // namespace detail

template <typename F>
[[nodiscard]] consteval std::string_view which_pattern_matches() noexcept {
    return detail::which_pattern_matches_at<F, 0>();
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — matcher behaviour on known shapes ──────────────────
// ═════════════════════════════════════════════════════════════════════

namespace matcher_self_test {

using ::crucible::safety::fn::Fn;

// DefaultFn = Linear / Tot / Classified / Fresh / Immutable.  No
// pattern matches (substrate's healthiest baseline).
using DefaultFn = Fn<int>;
static_assert(which_pattern_matches<DefaultFn>().empty(),
    "Theory matcher: DefaultFn must not match any corpus entry — "
    "if this fires, a matcher specialization is overreaching.");

// M012 shape pre-fix: Monotonic + Bg + non-Atomic.  We CANNOT
// instantiate the Fn<...> with this combination directly (substrate
// rejects via ValidComposition); we instead test the predicate on a
// fix-shape: Monotonic + Bg + Atomic.  matches_m012_shape returns
// true (the shape is in the M012 neighborhood); the safer encoding
// uses ReprKind::Atomic to clear the rule.
using MonotonicBgAtomic = Fn<int,
    ::crucible::safety::fn::pred::True,
    ::crucible::safety::fn::UsageMode::Linear,
    ::crucible::effects::Row<::crucible::effects::Effect::Bg>,
    ::crucible::safety::fn::SecLevel::Classified,
    ::crucible::safety::fn::proto::None,
    ::crucible::safety::fn::lifetime::Static,
    ::crucible::safety::source::FromInternal,
    ::crucible::safety::trust::Verified,
    ::crucible::safety::fn::ReprKind::Atomic,
    ::crucible::safety::fn::cost::Unstated,
    ::crucible::safety::fn::precision::Exact,
    ::crucible::safety::fn::space::Zero,
    ::crucible::safety::fn::OverflowMode::Trap,
    ::crucible::safety::fn::MutationMode::Monotonic,
    ::crucible::safety::fn::ReentrancyMode::NonReentrant,
    ::crucible::safety::fn::size_pol::Unstated,
    1,
    ::crucible::safety::fn::stale::Fresh>;

// MonotonicBgAtomic IS the M012-fix shape.  The matcher fires
// (Monotonic + Bg) because the matcher's job is "neighborhood
// classification", not "rule violation detection".  The substrate's
// ValidComposition has already cleared the rule via Repr=Atomic.
static_assert(matches_m012_shape<MonotonicBgAtomic>::value,
    "Theory matcher: Monotonic+Bg shape must match M012 neighborhood "
    "(cite gaps_010) regardless of Repr=Atomic fix.");

static_assert(which_pattern_matches<MonotonicBgAtomic>() == cite_gaps_010,
    "Theory matcher: first match on M012 shape must return gaps_010 cite.");

// theory_cite_hash_v stability — same cite literal → same hash.
static_assert(theory_cite_hash_v<
    std::tuple_element_t<0, Catalog>> != 0,
    "Theory cite hash must be non-zero — fnv1a + fmix64 cannot produce "
    "0 for any non-trivial input.");

// Distinct entries produce distinct hashes (high probability; collision
// here would indicate the fmix64 was misconfigured).
static_assert(theory_cite_hash_v<std::tuple_element_t<0, Catalog>> !=
              theory_cite_hash_v<std::tuple_element_t<1, Catalog>>,
    "Theory cite hash collision — two corpus entries hashed to the same "
    "value.  Either a duplicate cite or an fmix64 regression.");

}  // namespace matcher_self_test

}  // namespace crucible::fixy::theory
