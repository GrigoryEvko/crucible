#pragma once

// ── crucible::safety::diag — per-Tag insight infrastructure ─────────
//
// "Super-insightful" diagnostic upgrade per the user mandate.  The
// V1 RowMismatch builder ships an 8-line structured block that
// answers WHAT failed.  This satellite extends the surface to also
// answer:
//
//   * WHY does this matter? (architectural rationale + paper / docs
//     reference; turns "you violated HotPath" into the load-bearing
//     story behind why HotPath exists)
//   * WHAT does this typically look like at the call site? (symptom
//     pattern; helps the engineer recognize "oh, this is the printf-
//     for-debugging foot-gun, not a deeper architectural error")
//   * HOW would the CORRECT version look? (working code example
//     side-by-side with the violating one; eliminates the "but what
//     does the right version actually look like?" round-trip)
//   * Severity (Hint / Warning / Error / Fatal) — IDE consumers
//     (clangd) can downgrade Hint to a soft suggestion, escalate
//     Fatal to a build-stop with no override.
//
// ── Architecture ────────────────────────────────────────────────────
//
// `insight_provider<Tag>` primary template provides EMPTY DEFAULTS
// for every field — Severity::Error, all string_view fields empty.
// Per-Tag explicit specializations carry the substantive content.
//
// Foundation tags get rich specializations citing CLAUDE.md sections,
// papers (Atkey QTT, Tang-Lindley Met(X), Brookes RG, etc.), and
// concrete one-line code examples.  User-defined tags inherit the
// empty defaults (no breaking change) and can specialize to opt in.
//
// `build_deep_diagnostic_message<...>()` (in RowMismatch.h, alongside
// the existing 8-line builder) consumes the insight_provider for the
// given Tag and emits a 13-line block with all four insight sections
// when populated.  Empty fields collapse — graceful degradation.
//
// ── Format spec (deep diagnostic, v1) ───────────────────────────────
//
//   Line  1: [<Severity>: <Category>]\n
//   Line  2:   at <function_display_name>\n
//   Line  3:   caller row contains: <type_name<CallerRow>>\n
//   Line  4:   callee requires:     Subrow<_, <type_name<CalleeRow>>>\n
//   Line  5:   offending atoms:     <type_name<OffendingDiff>>\n
//   Line  6:   remediation: <Tag::remediation>\n
//   ┌─ if !insight_provider<Tag>::why_this_matters.empty():
//   │ Line  N:   ── why this matters ──\n
//   │ Line N+1:     <insight_provider<Tag>::why_this_matters>\n
//   ├─ if !insight_provider<Tag>::symptom_pattern.empty():
//   │ Line  N:   ── symptom pattern ──\n
//   │ Line N+1:     <insight_provider<Tag>::symptom_pattern>\n
//   ├─ if !insight_provider<Tag>::correct_example.empty():
//   │ Line  N:   ── correct usage ──\n
//   │ Line N+1:     <insight_provider<Tag>::correct_example>\n
//   ├─ if !insight_provider<Tag>::violating_example.empty():
//   │ Line  N:   ── violating usage ──\n
//   │ Line N+1:     <insight_provider<Tag>::violating_example>\n
//   └─
//   Line  Z:   docs: see safety/Diagnostic.h, 28_04_2026_effects.md §7
//
// Format version locked via CRUCIBLE_DIAG_DEEP_FORMAT_VERSION = 1.
//
// ── Severity classification rationale ───────────────────────────────
//
//   Hint     — informational suggestion; the code is correct but
//              could be better-shaped (e.g., "consider tightening
//              this Refined predicate").  Default: never; opt-in.
//   Warning  — the code compiles BUT will likely fail at runtime in
//              specific scenarios (e.g., "this Tag has no production
//              call site; verify it's intentional").  Default: never.
//   Error    — DEFAULT for all foundation tags.  The compile breaks;
//              the program does not link.  Either the assertion or
//              the implementation must change.
//   Fatal    — same as Error in compile behavior, but tooling treats
//              the violation as security-critical (e.g., DetSafe
//              leak that would corrupt replay logs at runtime).
//              Default: opt-in for the few axioms where silent
//              breakage is dangerous.
//
// ── Authoring discipline for insights ───────────────────────────────
//
// * `why_this_matters`: state the ARCHITECTURAL constraint, cite the
//   CLAUDE.md section / paper / RFC.  Answer "if I just ignored this
//   would anything actually break, and what?"
// * `symptom_pattern`: describe how this violation TYPICALLY surfaces
//   at the call site.  Help the engineer pattern-match their
//   debugging session against past instances.
// * `correct_example`: ONE LINE of valid C++ that does what the user
//   was trying to do, in compliance with the discipline.  No
//   pseudo-code; the example must compile (modulo the surrounding
//   context).
// * `violating_example`: ONE LINE that's the canonical anti-pattern
//   the diagnostic catches.  Side-by-side with the correct example,
//   the user can see exactly what to change.
//
// ── References ──────────────────────────────────────────────────────
//
//   28_04_2026_effects.md §7         — diagnostic infrastructure spec
//   safety/Diagnostic.h               — Tag catalog
//   safety/diag/RowMismatch.h         — V1 8-line builder + helpers
//
// FOUND-E (insight extension, NEW; complements E18 F*-style aliases).

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── Severity classification ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class Severity : std::uint8_t {
    Hint    = 0,  // informational suggestion; never breaks the build
    Warning = 1,  // compiles but runtime risk; tooling-promoted to error
    Error   = 2,  // DEFAULT — compile breaks; assertion or impl must change
    Fatal   = 3,  // security-critical; build-stop with no override path
};

[[nodiscard]] constexpr std::string_view severity_name(Severity s) noexcept {
    switch (s) {
        case Severity::Hint:    return "Hint";
        case Severity::Warning: return "Warning";
        case Severity::Error:   return "Error";
        case Severity::Fatal:   return "Fatal";
        default:                return "<unknown Severity>";
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── insight_provider<Tag> — primary template (empty defaults) ──────
// ═════════════════════════════════════════════════════════════════════
//
// User tags inherit these defaults seamlessly.  Foundation tags
// specialize below with substantive content.

template <typename Tag>
struct insight_provider {
    static constexpr Severity         severity           = Severity::Error;
    static constexpr std::string_view why_this_matters   = {};
    static constexpr std::string_view symptom_pattern    = {};
    static constexpr std::string_view correct_example    = {};
    static constexpr std::string_view violating_example  = {};
};

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_DEFINE_INSIGHTS — one-liner specialization macro ──────
// ═════════════════════════════════════════════════════════════════════
//
// Future-proofing the populate-later workflow.  Adding insights for a
// new Tag (foundation OR user-defined) reduces to ONE macro invocation
// at namespace scope:
//
//   CRUCIBLE_DEFINE_INSIGHTS(
//       my_namespace::MyTag,
//       ::crucible::safety::diag::Severity::Error,
//       "Why this matters: <one paragraph citing arch / paper / docs>",
//       "Symptom pattern: <one paragraph: how this surfaces>",
//       "Correct: <one-line C++ showing intended use>",
//       "Violating: <one-line C++ showing the anti-pattern>");
//
// Expands to a `template<>` specialization.  The `Tag` argument MUST
// be a fully-qualified type name (so the macro works from any
// namespace).  The macro ends with a `;` so use sites can place it
// at any namespace-scope.
//
// Discipline:
//   * The four prose strings should be substantive (one paragraph
//     each); sentinel TU's coverage assertions can pin minimum lengths
//     per tag if a project wants to enforce.
//   * Severity::Error is the default for foundation tags; only
//     Hint / Warning / Fatal need explicit choice.
//   * Use CRUCIBLE_DEFINE_INSIGHTS_DEFAULTS for tags that only need
//     the empty placeholder (rare; usually you skip the macro entirely
//     and rely on the primary template).
//
// If a project wants to ENFORCE that every Tag has insights, add a
// site-local concept gate:
//
//   template <typename T>
//   concept InsightedTag =
//       is_diagnostic_class_v<T> && has_insights_v<T>;
//
// and constrain consumers on `InsightedTag` instead of
// `is_diagnostic_class_v`.  The foundation does NOT impose this gate
// (some tags legitimately ship without insights and add them later
// when production callers reveal the load-bearing patterns).

// IMPORTANT: TagType MUST be a fully-qualified name (e.g.,
// `::my_proj::tags::HotPathViolation`).  C++ requires template
// specializations to be declared in the namespace where the primary
// template is defined; this macro opens that namespace and writes the
// specialization there.  Unqualified names inside the macro's body
// would resolve against `crucible::safety::diag` (where the primary
// lives), not against the user's namespace.
//
// The macro can be invoked from any namespace scope (including
// user namespaces, anonymous namespaces, the global namespace).
// The `namespace ... {}` block re-opens the foundation namespace,
// installs the specialization, and closes — so the surrounding code's
// namespace state is unaffected.
#define CRUCIBLE_DEFINE_INSIGHTS(TagType, Sev, Why, Symptom, Correct, Violating) \
    namespace crucible::safety::diag {                                            \
        template <>                                                                \
        struct insight_provider<TagType> {                                         \
            static constexpr Severity severity = (Sev);                            \
            static constexpr std::string_view why_this_matters   = (Why);          \
            static constexpr std::string_view symptom_pattern    = (Symptom);      \
            static constexpr std::string_view correct_example    = (Correct);      \
            static constexpr std::string_view violating_example  = (Violating);    \
        };                                                                         \
    }                                                                              \
    static_assert(true, "force trailing semicolon at call site")

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_DEFINE_INSIGHTS_SEVERITY — severity-only escalation ───
// ═════════════════════════════════════════════════════════════════════
//
// Use when you want to escalate (or pin) a Tag's severity but the
// prose fields aren't ready yet.  The four prose strings stay empty;
// `has_insights_v<Tag>` becomes true (because severity is now
// non-default OR the specialization exists at all).
//
// Note: the primary template's default severity is Error.  Calling
// CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(Tag, Severity::Error) with the
// SAME default IS still useful — it marks the Tag as "explicitly
// reviewed; no escalation needed" via has_insights_v becoming true.
//
// Pattern:
//
//   CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(
//       ::my_proj::tags::PaymentRefundLeak,
//       ::crucible::safety::diag::Severity::Fatal);
//
// Tradeoff: this leaves the user with a "registered but no prose"
// state.  For consumers that demand substantive insights, see the
// HasSubstantiveInsights concept below.
#define CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(TagType, Sev) \
    namespace crucible::safety::diag {                  \
        template <>                                      \
        struct insight_provider<TagType> {              \
            static constexpr Severity severity = (Sev); \
            static constexpr std::string_view why_this_matters   = {}; \
            static constexpr std::string_view symptom_pattern    = {}; \
            static constexpr std::string_view correct_example    = {}; \
            static constexpr std::string_view violating_example  = {}; \
        };                                              \
    }                                                   \
    static_assert(true, "force trailing semicolon at call site")

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_DEFINE_INSIGHTS_QV — quality-validated registration ───
// ═════════════════════════════════════════════════════════════════════
//
// Same surface as CRUCIBLE_DEFINE_INSIGHTS but with embedded
// `static_assert`s pinning each prose field to a minimum length.
// Catches the "shipped 'TODO' as the why field" failure mode at
// compile time instead of in code review.
//
// Default thresholds: why ≥ 30 chars, symptom ≥ 20, correct ≥ 10,
// violating ≥ 10.  Override via `insights_quality_thresholds`
// specialization (rare; defaults work for most projects).
//
// Pattern (preferred for production / load-bearing tags):
//
//   CRUCIBLE_DEFINE_INSIGHTS_QV(
//       ::my_proj::tags::DetSafeBreach,
//       ::crucible::safety::diag::Severity::Fatal,
//       "Why: a detailed paragraph explaining architectural intent...",
//       "Symptom: how this surfaces in production logs / CI...",
//       "fn(DetSafe<Pure, T>);",
//       "fn(WallClockRead<T>);  // VIOLATES");
//
// Failure mode (a too-short prose):
//
//   error: static assertion failed: Insight 'why_this_matters' is too
//   short (≥30 chars required) — be substantive
#define CRUCIBLE_DEFINE_INSIGHTS_QV(TagType, Sev, Why, Symptom, Correct, Violating) \
    namespace crucible::safety::diag {                                              \
        template <>                                                                  \
        struct insight_provider<TagType> {                                           \
            static constexpr Severity severity = (Sev);                              \
            static constexpr std::string_view why_this_matters   = (Why);           \
            static constexpr std::string_view symptom_pattern    = (Symptom);       \
            static constexpr std::string_view correct_example    = (Correct);       \
            static constexpr std::string_view violating_example  = (Violating);     \
            using thresholds = ::crucible::safety::diag::insights_quality_thresholds<TagType>; \
            static_assert(why_this_matters.size() >= thresholds::min_why_chars,     \
                "Insight 'why_this_matters' is too short — be substantive. "        \
                "Override via insights_quality_thresholds<Tag>::min_why_chars.");    \
            static_assert(symptom_pattern.size() >= thresholds::min_symptom_chars,  \
                "Insight 'symptom_pattern' is too short — be substantive. "         \
                "Override via insights_quality_thresholds<Tag>::min_symptom_chars."); \
            static_assert(correct_example.size() >= thresholds::min_correct_chars,  \
                "Insight 'correct_example' is too short — show real C++. "          \
                "Override via insights_quality_thresholds<Tag>::min_correct_chars."); \
            static_assert(violating_example.size() >= thresholds::min_violating_chars, \
                "Insight 'violating_example' is too short — show the anti-pattern. " \
                "Override via insights_quality_thresholds<Tag>::min_violating_chars."); \
        };                                                                           \
    }                                                                                 \
    static_assert(true, "force trailing semicolon at call site")

// ═════════════════════════════════════════════════════════════════════
// ── insights_quality_thresholds — per-tag minimum-length overrides ─
// ═════════════════════════════════════════════════════════════════════
//
// Default thresholds for CRUCIBLE_DEFINE_INSIGHTS_QV.  Specialize per
// Tag if a project wants stricter or looser bars.
template <typename Tag>
struct insights_quality_thresholds {
    static constexpr std::size_t min_why_chars       = 30;
    static constexpr std::size_t min_symptom_chars   = 20;
    static constexpr std::size_t min_correct_chars   = 10;
    static constexpr std::size_t min_violating_chars = 10;
};

// ═════════════════════════════════════════════════════════════════════
// ── Concept gates for downstream enforcement ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `has_insights_v<Tag>` (defined later) is the boolean predicate.
// Two concept aliases ergonomic for `requires` clauses:
//
//   * WellInsightedTag<T>  — Tag IS a tag AND has been explicitly
//                            registered (has_insights_v true).
//                            Use to GUARANTEE diagnostic output is
//                            non-default.
//
//   * HasSubstantiveInsights<T> — Tag is well-insighted AND every
//                                  prose field meets the QV
//                                  thresholds.  Use for production-
//                                  critical surfaces that must not
//                                  ship with placeholder insights.
//
// Forward declarations; concepts proper appear after has_insights_v
// + is_diagnostic_class_v are visible.

// ═════════════════════════════════════════════════════════════════════
// ── Workflow: populating insights for an existing tag ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// 1. Foundation tags (the 22 in safety/Diagnostic.h):
//    * Add a CRUCIBLE_DEFINE_INSIGHTS(...) line in this header's
//      "Foundation-tag insight specializations" section below.
//    * If the tag was previously unspecialized, the corresponding
//      `static_assert(!has_insights_v<TagX>)` in the self-test block
//      flips — change it to `static_assert(has_insights_v<TagX>)`.
//    * No other code change required; the deep builder picks up the
//      insights automatically via insight_provider<Tag>.
//
// 2. User-defined tags (project-local extensions):
//    * Place the CRUCIBLE_DEFINE_INSIGHTS(...) call in the user's
//      header right after the tag struct definition.
//    * The specialization is at namespace scope — it does not "leak"
//      into the foundation Catalog (which is closed).
//    * The deep builder consumes the user-tag's insights when the
//      user invokes CRUCIBLE_INSIGHTFUL_ROW_MISMATCH_ASSERT with that
//      tag.
//
// 3. Severity escalation (e.g., a Warning-class issue surfaces as
//    breaking in production):
//    * Re-invoke CRUCIBLE_DEFINE_INSIGHTS with the new Severity.
//    * The macro is idempotent over its template-specialization
//      target — a second invocation is a redefinition error, so the
//      author MUST remove the original specialization first.  This
//      forces a code-review touch on every severity change.
//
// 4. CI guard for "every load-bearing tag has insights":
//    * In a project test, enumerate the tags that MUST have insights
//      and `static_assert(has_insights_v<Tag>)` for each.  See the
//      sentinel TU in test/test_insights_compile.cpp for the
//      foundation-tag enumeration pattern.

// ═════════════════════════════════════════════════════════════════════
// ── Foundation-tag insight specializations ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Ten of the 22 foundation tags get heavyweight specializations.  The
// other twelve inherit the empty defaults and can be filled in as
// production callers reveal which insights are most load-bearing.

// ── EffectRowMismatch ──────────────────────────────────────────────
template <>
struct insight_provider<EffectRowMismatch> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Met(X) effect rows (Tang-Lindley POPL 2026) encode capability "
        "propagation in the type system.  A function declared with row "
        "R promises to use AT MOST those effects; a caller with row R' "
        "MUST contain R' to invoke it (Subrow<R, R'>).  Without this "
        "discipline, hot-path code accidentally inherits Bg-thread "
        "capabilities (alloc / IO / block) from a transitively-called "
        "subroutine, blowing latency budgets and breaking the recording "
        "trace.  The row IS the function's contract.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces after a refactor that lifts a previously-bg-thread "
        "helper into a Computation<Row<>, T> (pure) signature without "
        "removing the helper's printf / arena alloc.  The transitive "
        "Subrow check fires at the lowest call site that actually "
        "needs the bg-row, NOT at the helper itself.";
    static constexpr std::string_view correct_example =
        "Computation<Row<Effect::Bg>, T> bg_helper(); // honest about Bg";
    static constexpr std::string_view violating_example =
        "Computation<Row<>, T> bg_helper(); // hides Bg → fires here";
};

// ── HotPathViolation ───────────────────────────────────────────────
template <>
struct insight_provider<HotPathViolation> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "The hot-path latency floor is the MESI cache-line transfer "
        "cost — ~10-40 ns intra-socket, ~30-100 ns cross-socket "
        "(CLAUDE.md §IX latency hierarchy).  Admitting a Warm callee "
        "(allocation, ~50-200 ns) or Cold callee (block / IO, "
        "~10-100 us) on a 5 ns recording path is a 10x to 10000x "
        "slowdown.  HotPath<Hot, T> is the type system's promise that "
        "the recorded code path stays within the latency floor; the "
        "rejection here proves a regression would have shipped.";
    static constexpr std::string_view symptom_pattern =
        "Almost always surfaces after adding logging, asserting, or "
        "instrumentation to a previously-clean hot-path TU.  The new "
        "fprintf / std::cout / std::format call is Cold-tier; the "
        "containing function's HotPath<Hot> declaration rejects the "
        "transitive call at compile time, before the regression can "
        "ship.  Less commonly: an inline-friendly helper that grew a "
        "vector<T>::push_back over time.";
    static constexpr std::string_view correct_example =
        "static std::atomic<uint64_t> debug_counter; // Hot-path-safe";
    static constexpr std::string_view violating_example =
        "fprintf(stderr, \"debug %d\\n\", x); // Cold; rejected by Hot caller";
};

// ── DetSafeLeak ────────────────────────────────────────────────────
template <>
struct insight_provider<DetSafeLeak> {
    static constexpr Severity         severity = Severity::Fatal;
    static constexpr std::string_view why_this_matters =
        "DetSafe is the 8TH AXIOM (CLAUDE.md §II.8): same inputs → "
        "same outputs, bit-identical under BITEXACT replay.  It is "
        "the ONE axiom historically un-fenced — caught only by the "
        "bit_exact_replay_invariant CI test ~12 hours after the "
        "commit lands.  This rejection at compile time is what fences "
        "the axiom.  A wall-clock read or /dev/urandom call recorded "
        "into the replay log makes EVERY subsequent replay produce "
        "different outputs; cross-vendor numerics CI then rejects "
        "Mimic backends that 'fail' to match the corrupted oracle.  "
        "Severity::Fatal because silent breakage cascades into hours "
        "of debugging lost replay determinism.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when adding 'just one little timestamp' to a Cipher "
        "event-recording site for debugging.  Or when a refactor "
        "replaces seeded Philox with std::random_device for 'better "
        "randomness'.  Or when the Augur metric collector's wall-clock "
        "sample accidentally crosses into a record_event path.  All "
        "three are real production patterns the framework now catches.";
    static constexpr std::string_view correct_example =
        "DetSafe<Pure, uint64_t> seed = philox(counter, key); // deterministic";
    static constexpr std::string_view violating_example =
        "auto seed = std::chrono::steady_clock::now(); // wall clock; rejected";
};

// ── NumericalTierMismatch ──────────────────────────────────────────
template <>
struct insight_provider<NumericalTierMismatch> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Recipe tiers (FORGE.md §20) discipline the cross-vendor "
        "numerics CI: BITEXACT_STRICT recipes produce byte-identical "
        "output across all backends; BITEXACT_TC tolerate ≤1 ULP "
        "tensor-core deviation; ORDERED enforce a per-recipe tolerance; "
        "UNORDERED admit reduction-order changes.  A caller pinned at "
        "BITEXACT_STRICT calling an UNORDERED kernel silently propagates "
        "non-determinism through the model — checkpoint loads from a "
        "BITEXACT chain produce divergent outputs after the UNORDERED "
        "kernel runs.  The recipe-tier discipline is what makes "
        "federation work.";
    static constexpr std::string_view symptom_pattern =
        "Often surfaces after a Mimic backend ships a fast-path "
        "ALLREDUCE (UNORDERED tier) and a downstream training step "
        "expecting BITEXACT_TC tries to consume its output.  Or after "
        "a Forge Phase E recipe-select picker switches to a tighter "
        "BITEXACT_STRICT recipe and a previously-OK ORDERED kernel is "
        "now rejected.";
    static constexpr std::string_view correct_example =
        "select_recipe<Tolerance::BITEXACT_TC>(KernelKind::GEMM_MM, fleet);";
    static constexpr std::string_view violating_example =
        "auto k = unordered_allreduce(x); // UNORDERED; rejected by BITEXACT";
};

// ── MemOrderViolation ──────────────────────────────────────────────
template <>
struct insight_provider<MemOrderViolation> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "memory_order_seq_cst emits MFENCE on x86 (~30 ns serializing "
        "the store buffer) and DMB ISH on ARM (full system barrier).  "
        "Acquire/release semantics suffice for every SPSC ring, every "
        "snapshot, every lock-free pattern Crucible needs (CLAUDE.md "
        "§IX).  seq_cst on the hot path is a 30-100x latency hit and "
        "is almost always wrong — it usually indicates the engineer "
        "didn't think about the actual ordering requirement and "
        "reached for the strongest available primitive 'just to be "
        "safe'.  The discipline is to think about acquire/release.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces in code that was written outside the project's "
        "discipline (e.g., copied from a stack-overflow lock-free "
        "snippet) or refactored from a mutex-protected region without "
        "the author auditing the actual ordering need.  Less commonly: "
        "true total-order requirement (rare; if you genuinely need "
        "this, the ownership boundary is wrong — escalate to design "
        "review).";
    static constexpr std::string_view correct_example =
        "x.store(v, std::memory_order_release); // publish";
    static constexpr std::string_view violating_example =
        "x.store(v, std::memory_order_seq_cst); // banned; emit MFENCE";
};

// ── AllocClassViolation ────────────────────────────────────────────
template <>
struct insight_provider<AllocClassViolation> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Hot-path code MUST NOT allocate from the heap (CLAUDE.md HS10): "
        "malloc round-trip is ~50-200 ns, unpredictable under "
        "contention, and breaks the per-iteration latency budget.  "
        "Arena bump allocation is ~2 ns and lock-free; PoolAllocator "
        "is ~5 ns and bounded.  The AllocClass wrapper makes the "
        "discipline visible in function signatures so a refactor that "
        "introduces std::vector::push_back into the hot path is "
        "rejected at the call site before it ships.";
    static constexpr std::string_view symptom_pattern =
        "Almost always surfaces after a refactor that 'just adds a "
        "vector<T>' to collect intermediate results, OR after using "
        "std::string concatenation in a logging path, OR after "
        "refactoring an arena-backed buffer to std::array<T, N> via a "
        "helper that returns std::vector instead.";
    static constexpr std::string_view correct_example =
        "auto* buf = arena.alloc_array<float>(n); // Arena, ~2 ns";
    static constexpr std::string_view violating_example =
        "auto buf = std::vector<float>(n); // Heap; rejected on hot path";
};

// ── GradedWrapperViolation ─────────────────────────────────────────
template <>
struct insight_provider<GradedWrapperViolation> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "GradedWrapper is the structural concept (algebra/GradedTrait.h) "
        "every safety wrapper must satisfy: graded_type points at a "
        "real Graded<M, L, T>, the wrapper's modality matches the "
        "substrate's, value_type and lattice_type are consistent, "
        "diagnostic forwarders return the substrate's strings.  These "
        "five 'cheats' are the audit cluster from Round-4; the "
        "concept's job is to lock them in.  Violating the concept "
        "means downstream FOUND-D dispatcher reflection produces "
        "wrong dispatch decisions (e.g., a 'wrapper' with mismatched "
        "modality routes to the wrong lowering target).";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when authoring a NEW wrapper (e.g., for FOUND-G "
        "wrappers G01-G80) without following the canonical Stale.h "
        "template.  Common cause: copying part of an existing wrapper "
        "and forgetting to update graded_type, OR adding a "
        "value_type_decoupled opt-in without justification, OR "
        "publishing a custom string-returning value_type_name() that "
        "doesn't forward to the substrate.";
    static constexpr std::string_view correct_example =
        "using graded_type = Graded<Absolute, MyLattice::At<X>, T>; // matches";
    static constexpr std::string_view violating_example =
        "using graded_type = void; // not a Graded<...> — concept rejects";
};

// ── LinearityViolation ─────────────────────────────────────────────
template <>
struct insight_provider<LinearityViolation> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Quantitative type theory (Atkey FLoC 2018) and Concurrent "
        "Separation Logic (O'Hearn 2007) both require that linear "
        "values are consumed EXACTLY ONCE.  Crucible's Linear<T> / "
        "Permission<Tag> / OwnedRegion<T, Tag> encode this in the "
        "type system: copy is deleted, move transfers ownership, "
        "double-consume is a compile error.  Without linearity, two "
        "threads can simultaneously hold the 'exclusive' permission "
        "for a region (data race), or a Linear<File> can be closed "
        "twice (heap corruption).  The discipline is what makes the "
        "BorrowSafe + ThreadSafe + LeakSafe axioms (CLAUDE.md §II "
        "5/6/7) actually hold at the type level.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when capturing a Linear<T> by value into a lambda "
        "that's then std::moved into TWO different jthread spawns, OR "
        "when a Permission<Tag> is stored in a struct field that's "
        "subsequently copied for parallel dispatch, OR when a refactor "
        "removes std::move and the compiler's implicit copy attempt "
        "fires the deleted-copy assertion.";
    static constexpr std::string_view correct_example =
        "consumer(std::move(linear_val)); // single consumption, OK";
    static constexpr std::string_view violating_example =
        "consumer1(linear_val); consumer2(linear_val); // double-use; rejected";
};

// ── RefinementViolation ────────────────────────────────────────────
template <>
struct insight_provider<RefinementViolation> {
    static constexpr Severity         severity = Severity::Error;
    static constexpr std::string_view why_this_matters =
        "Refined<P, T> attaches a compile-time-named predicate P to "
        "values of type T (safety/Refined.h).  The constructor's "
        "pre() clause checks P(v) at runtime under contract semantic="
        "enforce (debug builds, CI) and treats it as [[assume(P(v))]] "
        "under semantic=ignore (release builds).  The downstream code "
        "is OPTIMIZED on the assumption that P holds; violating the "
        "predicate at the construction site causes the optimizer to "
        "make incorrect downstream decisions (UB).  The discipline is "
        "to validate at the construction site.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when boundary-validating user input incorrectly: "
        "e.g., using Refined<positive>(value) on a value that could "
        "be zero or negative.  Or when a refactor changes the source "
        "of a Refined<bounded_above<8>>(ndim) where ndim came from a "
        "now-uncapped tensor metadata field.";
    static constexpr std::string_view correct_example =
        "if (n > 0) Refined<positive, int>(n); // validated first";
    static constexpr std::string_view violating_example =
        "Refined<positive, int>(maybe_zero); // pre() fails; UB on optimize";
};

// ── UnknownParameterShape ──────────────────────────────────────────
template <>
struct insight_provider<UnknownParameterShape> {
    static constexpr Severity         severity = Severity::Warning;
    static constexpr std::string_view why_this_matters =
        "The FOUND-D dispatcher (28_04 §6) reads function signatures "
        "and routes them to one of the seven canonical lowerings "
        "(UnaryTransform, BinaryTransform, Reduction, Producer/"
        "Consumer endpoint, SwmrWriter/Reader, PipelineStage).  "
        "Functions whose parameter types don't match any canonical "
        "shape can't be auto-dispatched — the user must either "
        "reshape the signature or manually call the underlying "
        "primitives.  Severity::Warning rather than Error because "
        "the manual orchestration path is documented and supported; "
        "auto-dispatch is the default but not the only option.";
    static constexpr std::string_view symptom_pattern =
        "Surfaces when a user writes a free function the natural way "
        "(e.g., taking raw pointers + sizes) and tries to dispatch it "
        "via crucible::dispatch().  The dispatcher rejects because "
        "raw pointers aren't OwnedRegion<T, Tag>; user reshapes to "
        "use the wrapper or calls parallel_for_views directly.";
    static constexpr std::string_view correct_example =
        "void f(OwnedRegion<float, Tag>&&); // canonical UnaryTransform";
    static constexpr std::string_view violating_example =
        "void f(float*, size_t); // raw ptr; not a canonical shape";
};

// ═════════════════════════════════════════════════════════════════════
// ── Trait: does this Tag have non-trivial insights? ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff at least ONE of the four insight string_views is non-empty.
// Used by the deep builder to decide whether to emit insight sections
// at all (a Tag with no specialization gets the bare format).

template <typename Tag>
inline constexpr bool has_insights_v =
       !insight_provider<Tag>::why_this_matters.empty()
    || !insight_provider<Tag>::symptom_pattern.empty()
    || !insight_provider<Tag>::correct_example.empty()
    || !insight_provider<Tag>::violating_example.empty();

// ═════════════════════════════════════════════════════════════════════
// ── Trait: insights meet QV thresholds ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff EVERY field meets its insights_quality_thresholds<Tag>
// minimum length.  Different from has_insights_v (which is true if
// ANY field is non-empty) — this is the strictly stronger predicate
// for production-critical surfaces.

template <typename Tag>
inline constexpr bool has_substantive_insights_v =
       insight_provider<Tag>::why_this_matters.size()
            >= insights_quality_thresholds<Tag>::min_why_chars
    && insight_provider<Tag>::symptom_pattern.size()
            >= insights_quality_thresholds<Tag>::min_symptom_chars
    && insight_provider<Tag>::correct_example.size()
            >= insights_quality_thresholds<Tag>::min_correct_chars
    && insight_provider<Tag>::violating_example.size()
            >= insights_quality_thresholds<Tag>::min_violating_chars;

// ═════════════════════════════════════════════════════════════════════
// ── Concepts: WellInsightedTag, HasSubstantiveInsights ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Use in `requires` clauses to enforce that a Tag has insights at all
// (WellInsightedTag) or that the insights meet QV thresholds
// (HasSubstantiveInsights).
//
// Pattern (consumer side):
//
//   template <typename Tag>
//       requires WellInsightedTag<Tag>
//   void emit_classified_diagnostic(...);
//
// Pattern (production-critical surface):
//
//   template <typename Tag>
//       requires HasSubstantiveInsights<Tag>
//   void emit_load_bearing_diagnostic(...);

template <typename Tag>
concept WellInsightedTag = is_diagnostic_class_v<Tag> && has_insights_v<Tag>;

template <typename Tag>
concept HasSubstantiveInsights =
    is_diagnostic_class_v<Tag> && has_substantive_insights_v<Tag>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The 10 specialized foundation tags MUST have non-empty insights.
// The 12 unspecialized foundation tags inherit empty defaults.
// User-defined tags use the empty defaults.

namespace detail::insights_self_test {

// Severity name coverage.
static_assert(severity_name(Severity::Hint)    == "Hint");
static_assert(severity_name(Severity::Warning) == "Warning");
static_assert(severity_name(Severity::Error)   == "Error");
static_assert(severity_name(Severity::Fatal)   == "Fatal");

// Default severity is Error.
static_assert(insight_provider<EpochMismatch>::severity == Severity::Error,
    "Unspecialized tag's default severity should be Error.");

// 10 specialized tags have insights.
static_assert(has_insights_v<EffectRowMismatch>);
static_assert(has_insights_v<HotPathViolation>);
static_assert(has_insights_v<DetSafeLeak>);
static_assert(has_insights_v<NumericalTierMismatch>);
static_assert(has_insights_v<MemOrderViolation>);
static_assert(has_insights_v<AllocClassViolation>);
static_assert(has_insights_v<GradedWrapperViolation>);
static_assert(has_insights_v<LinearityViolation>);
static_assert(has_insights_v<RefinementViolation>);
static_assert(has_insights_v<UnknownParameterShape>);

// 12 unspecialized foundation tags do NOT have insights (they
// inherit the empty primary-template defaults).  Promoting them to
// specialized requires authoring the four insight strings.
static_assert(!has_insights_v<VendorBackendMismatch>);
static_assert(!has_insights_v<CrashClassMismatch>);
static_assert(!has_insights_v<ConsistencyMismatch>);
static_assert(!has_insights_v<LifetimeViolation>);
static_assert(!has_insights_v<WaitStrategyViolation>);
static_assert(!has_insights_v<ProgressClassViolation>);
static_assert(!has_insights_v<CipherTierViolation>);
static_assert(!has_insights_v<ResidencyHeatViolation>);
static_assert(!has_insights_v<EpochMismatch>);
static_assert(!has_insights_v<BudgetExceeded>);
static_assert(!has_insights_v<NumaPlacementMismatch>);
static_assert(!has_insights_v<RecipeSpecMismatch>);

// DetSafeLeak is Fatal (the 8th axiom is the load-bearing one).
static_assert(insight_provider<DetSafeLeak>::severity == Severity::Fatal);

// UnknownParameterShape is Warning (manual orchestration is
// supported; not a hard error).
static_assert(insight_provider<UnknownParameterShape>::severity
              == Severity::Warning);

// All other specialized tags are Error.
static_assert(insight_provider<HotPathViolation>::severity == Severity::Error);
static_assert(insight_provider<EffectRowMismatch>::severity == Severity::Error);

// User-defined tag gets default insights (empty + Error).
struct user_tag : tag_base {
    static constexpr std::string_view name        = "UserDefinedX";
    static constexpr std::string_view description = "user";
    static constexpr std::string_view remediation = "see local docs";
};
static_assert(!has_insights_v<user_tag>);
static_assert(insight_provider<user_tag>::severity == Severity::Error);

// Concept gates exercised on foundation tags.
static_assert(WellInsightedTag<EffectRowMismatch>);
static_assert(WellInsightedTag<DetSafeLeak>);
static_assert(!WellInsightedTag<EpochMismatch>);  // unspecialized
static_assert(!WellInsightedTag<int>);            // not a tag at all

// HasSubstantiveInsights is the strictly stronger predicate — the 10
// hand-authored foundation specializations all clear the QV thresholds
// (since the prose was written deliberately).
static_assert(HasSubstantiveInsights<EffectRowMismatch>);
static_assert(HasSubstantiveInsights<HotPathViolation>);
static_assert(HasSubstantiveInsights<DetSafeLeak>);
static_assert(!HasSubstantiveInsights<EpochMismatch>);  // empty defaults fail QV

}  // namespace detail::insights_self_test

}  // namespace crucible::safety::diag

// ═════════════════════════════════════════════════════════════════════
// ── Self-test for CRUCIBLE_DEFINE_INSIGHTS macro ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify the macro produces a working specialization at namespace
// scope.  Defined in an anonymous namespace at the top level so it
// doesn't pollute the diag namespace; the macro itself qualifies
// fully.

namespace crucible::safety::diag::detail::insights_macro_test {

struct macro_target_tag : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "MacroTargetTag";
    static constexpr std::string_view description = "test fixture for "
        "CRUCIBLE_DEFINE_INSIGHTS expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

}  // namespace crucible::safety::diag::detail::insights_macro_test

// Macro invocation at namespace scope — the canonical user-extension
// pattern.  Verifies the workflow documented above.
CRUCIBLE_DEFINE_INSIGHTS(
    ::crucible::safety::diag::detail::insights_macro_test::macro_target_tag,
    ::crucible::safety::diag::Severity::Warning,
    "WHY-MACRO-TEST",
    "SYMPTOM-MACRO-TEST",
    "CORRECT-MACRO-TEST",
    "VIOLATING-MACRO-TEST");

namespace crucible::safety::diag::detail::insights_macro_test {

using P = ::crucible::safety::diag::insight_provider<macro_target_tag>;

static_assert(P::severity == ::crucible::safety::diag::Severity::Warning,
    "CRUCIBLE_DEFINE_INSIGHTS failed to set severity correctly.");
static_assert(P::why_this_matters   == std::string_view{"WHY-MACRO-TEST"});
static_assert(P::symptom_pattern    == std::string_view{"SYMPTOM-MACRO-TEST"});
static_assert(P::correct_example    == std::string_view{"CORRECT-MACRO-TEST"});
static_assert(P::violating_example  == std::string_view{"VIOLATING-MACRO-TEST"});
static_assert(::crucible::safety::diag::has_insights_v<macro_target_tag>,
    "Macro-populated insights must register as has_insights_v.");

// Severity-only target — exercises CRUCIBLE_DEFINE_INSIGHTS_SEVERITY.
struct severity_only_tag : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "SeverityOnlyTag";
    static constexpr std::string_view description =
        "test fixture for CRUCIBLE_DEFINE_INSIGHTS_SEVERITY expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

// QV-validated target — exercises CRUCIBLE_DEFINE_INSIGHTS_QV.
struct qv_target_tag : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "QvTargetTag";
    static constexpr std::string_view description =
        "test fixture for CRUCIBLE_DEFINE_INSIGHTS_QV expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

}  // namespace crucible::safety::diag::detail::insights_macro_test

// ── CRUCIBLE_DEFINE_INSIGHTS_SEVERITY exercise ────────────────────
CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(
    ::crucible::safety::diag::detail::insights_macro_test::severity_only_tag,
    ::crucible::safety::diag::Severity::Fatal);

// ── CRUCIBLE_DEFINE_INSIGHTS_QV exercise ──────────────────────────
//
// Each prose field clears its default minimum-length threshold:
//   * why ≥ 30 chars
//   * symptom ≥ 20 chars
//   * correct / violating ≥ 10 chars
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::safety::diag::detail::insights_macro_test::qv_target_tag,
    ::crucible::safety::diag::Severity::Error,
    "QV why field — substantive prose clearing the 30-char min.",
    "QV symptom — clears 20-char min.",
    "fn(GoodFoo);",   // 12 chars — clears 10-char min
    "fn(BadBar);   // VIOLATES the contract");

namespace crucible::safety::diag::detail::insights_macro_test {

using PSev = ::crucible::safety::diag::insight_provider<severity_only_tag>;
static_assert(PSev::severity == ::crucible::safety::diag::Severity::Fatal,
    "CRUCIBLE_DEFINE_INSIGHTS_SEVERITY must set severity.");
static_assert(PSev::why_this_matters.empty(),
    "Severity-only macro should leave why_this_matters empty.");
static_assert(PSev::symptom_pattern.empty(),
    "Severity-only macro should leave symptom_pattern empty.");
// has_insights_v stays FALSE because all 4 prose fields are empty —
// severity-only registration is a "tracked TODO" state, NOT a
// well-insighted state.  This is the documented semantic.
static_assert(!::crucible::safety::diag::has_insights_v<severity_only_tag>,
    "Severity-only macro leaves has_insights_v false (prose all empty).");

using PQv = ::crucible::safety::diag::insight_provider<qv_target_tag>;
static_assert(PQv::severity == ::crucible::safety::diag::Severity::Error);
static_assert(PQv::why_this_matters.size() >= 30);
static_assert(PQv::symptom_pattern.size()  >= 20);
static_assert(PQv::correct_example.size()  >= 10);
static_assert(PQv::violating_example.size() >= 10);
// QV-registered tag is well-insighted AND substantive.
static_assert(::crucible::safety::diag::has_insights_v<qv_target_tag>);
static_assert(::crucible::safety::diag::has_substantive_insights_v<qv_target_tag>);
static_assert(::crucible::safety::diag::WellInsightedTag<qv_target_tag>);
static_assert(::crucible::safety::diag::HasSubstantiveInsights<qv_target_tag>);

}  // namespace crucible::safety::diag::detail::insights_macro_test

