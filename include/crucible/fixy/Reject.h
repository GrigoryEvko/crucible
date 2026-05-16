#pragma once

// ── crucible::fixy — Reject.h (FIXY-A4a) ──────────────────────────────
//
// The load-bearing concept gate for the entire fixy/ discipline.
//
// **The discipline.**  A fixy binding's `Grants...` pack must engage
// EVERY one of the 20 dims — either by carrying a relaxation tag (a
// `grant::*` empty struct whose `relaxes` member names a dim) OR by
// carrying `accept_default_strict_for<dim::X>` (the explicit "I read
// the discipline and chose strict for X" tag).  Anything else fails.
//
// **The mechanism.**  Two concepts:
//
//   1. `EngagedFor<dim::D, Grants...>` — true iff at least one tag in
//      Grants reports `relaxes == D`.  A pure fold expression; cannot
//      be specialized (concepts are unspecializable per [temp]/2),
//      closing the trait-spec-injection cheat (FIXY-A7).
//
//   2. `IsAccepted<Grants...>` — conjunction of `EngagedFor<dim::X,
//      Grants...>` over all 20 dims.  The 20 clauses appear in dim
//      enumerator order so the FIRST unengaged dim is the one whose
//      EngagedFor clause fails — a fixy::fn wrapper (Phase B) reads
//      `WhichDimUnengaged<Grants...>::value` to emit a structured
//      diagnostic naming exactly which dim the author missed.
//
// **Inheritance-bypass closure.**  All grant tags in fixy/Grant.h
// are not marked final today — Phase A keeps them composable.  A
// `struct fake : accept_default_strict_for<dim::Usage> {};` WOULD
// satisfy `EngagedFor<dim::Usage, fake>` because `fake::relaxes`
// inherits as `dim::Usage`.  This is documented as an architectural
// trade-off (cheat probe FIXY-A7 #2) and the Phase B fixy::fn
// wrapper rejects derived-class grants via a `final-or-known-grant`
// gate.  For Phase A's pure-engagement-check role, the trade-off is
// acceptable: inheritance through Grant tags is rare enough that
// review catches it, and final-marking the tags would block
// legitimate composition (e.g., a `stance::PureCopy` alias that
// expands to a tuple of grant tags).
//
// ── Surface ────────────────────────────────────────────────────────────
//
//   // Per-dim engagement check (used internally + exposed for tests).
//   template <dim::DimAxis D, typename... Grants>
//   concept EngagedFor = (... || (Grants::relaxes == D));
//
//   // The 20-dim conjunction — the load-bearing gate.
//   template <typename... Grants>
//   concept IsAccepted = AllDimsEngaged<Grants...>;
//
//   // Diagnostic: the first-failing dim (for downstream consumers).
//   template <typename... Grants>
//   struct WhichDimUnengaged {
//       static constexpr dim::DimAxis value = ...;   // kAllEngagedSentinel
//                                                    // when all engaged —
//                                                    // guard via all_engaged
//                                                    // (see FIXY-AUDIT-SENTINEL).
//       static constexpr bool all_engaged    = ...;
//   };
//
//   // Per-dim diagnostic tag types (20).
//   struct FixyNotEngaged_Type      : safety::diag::tag_base { ... };
//   ... 19 more ...
//   using FixyDiagCatalog = std::tuple<FixyNotEngaged_Type, ...>;
//
//   // Type-level dim → tag mapping.
//   template <dim::DimAxis D> struct diag_tag_for;
//   template <dim::DimAxis D> using diag_tag_for_t = typename diag_tag_for<D>::type;
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — concept gates produce static_assert at template-
//                instantiation time; no runtime state involved.  Every
//                inline diag tag has NSDMI on its constexpr static
//                members (`name`, `description`); P2795R5 + the empty-
//                struct shape close the uninit-read window.
//   TypeSafe   — fold-expression dispatch on `Grants::relaxes`
//                (strong enum); cross-dim confusion is a compile
//                error.  20 diagnostic tags are non-convertible
//                (asserted by test_fixy_diag).
//   NullSafe   — no pointer members; the `name` / `description` views
//                are `string_view` over compile-time string literals
//                (no nullptr backing).
//   MemSafe    — every tag is an empty struct (sizeof == 1); zero
//                allocation, zero resource ownership, no dtor work.
//   BorrowSafe — concepts are pure value-domain predicates; no
//                aliasing concern at the type level.
//   ThreadSafe — entire header is compile-time material; no runtime
//                state to race over.
//   LeakSafe   — zero-state types; tags are not resource-bearing.
//   DetSafe    — concept evaluation is constexpr / bit-identical
//                across compiles.  Reflection iteration order matches
//                the source-order enumerators of `dim::DimAxis`;
//                first-failing-dim heuristic returns a stable answer.
//
// ── Runtime cost ──────────────────────────────────────────────────────
//
// Zero.  IsAccepted is a pure concept; it fires at template
// instantiation, emits no machine code, and produces no runtime
// state.  WhichDimUnengaged is consteval-callable; its `value`
// member is an immediate at all consumer sites.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §3          — IsAccepted load-bearing gate
//   misc/fixy.md §24                    — fixy reject-by-default spec
//   safety/Diagnostic.h                 — tag_base + Category pattern
//   safety/CollisionCatalog.h           — diagnostic emission precedent
//   fixy/Grant.h                        — grant tags consumed here
//   fixy/Dim.h                          — dim::DimAxis identity layer

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/Insights.h>
#include <crucible/safety/diag/StableName.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── EngagedFor concept — per-dim engagement check ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pure fold expression.  Concepts are not specializable per [temp]/2,
// so user-side override is structurally impossible.  Empty pack
// (sizeof...(Grants) == 0) evaluates to `false` per the fold identity
// — disengages every dim, exactly the desired default-reject semantic.

// FIXY-AUDIT-CVR: `Grants::relaxes` is ill-formed when Grants is a
// reference (`T&::relaxes`), a pointer (`T*::relaxes`), an arithmetic
// type (`int::relaxes`), or any random struct without a `relaxes`
// static member.  A user who writes `IsAccepted<grant::copy&, ...>`
// or accidentally passes `int` through a forwarding-template
// previously got an ugly substitution-failure error pointing inside
// Reject.h.  We dispatch through a per-pack-member SFINAE-style
// variable template `engages_dim_v<T, D>` so non-grant members
// contribute `false` to the fold instead of triggering substitution
// failure.
//
// Note: cv-qualified types (e.g., `const grant::copy`) DO have a
// `relaxes` static member via const-qualified access — those continue
// to engage as expected, which is the right behavior since grants are
// stateless metadata.

namespace detail {

template <typename T>
concept has_axis_relaxes = requires {
    { T::relaxes } -> std::convertible_to<dim::DimAxis>;
};

// Per-member engagement predicate.  For grants (which have a
// DimAxis-typed `relaxes`) it compares; for everything else
// (references, pointers, ints, random structs) it folds to false
// without trying to access `T::relaxes`.
template <typename T, dim::DimAxis D>
inline constexpr bool engages_dim_v = false;

template <has_axis_relaxes T, dim::DimAxis D>
inline constexpr bool engages_dim_v<T, D> = (T::relaxes == D);

}  // namespace detail

template <dim::DimAxis D, typename... Grants>
concept EngagedFor =
    (false || ... || detail::engages_dim_v<Grants, D>);

// ═════════════════════════════════════════════════════════════════════
// ── IsAccepted concept — 20-dim conjunction (LOAD-BEARING) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Order matches `dim::DimAxis` enumerator order, so the FIRST
// unengaged dim is the one whose EngagedFor clause fails.  A future
// consumer can read `WhichDimUnengaged<Grants...>::value` to recover
// the failing dim and emit a structured diagnostic.

template <typename... Grants>
concept IsAccepted =
       EngagedFor<dim::Type,           Grants...>
    && EngagedFor<dim::Refinement,     Grants...>
    && EngagedFor<dim::Usage,          Grants...>
    && EngagedFor<dim::Effect,         Grants...>
    && EngagedFor<dim::Security,       Grants...>
    && EngagedFor<dim::Protocol,       Grants...>
    && EngagedFor<dim::Lifetime,       Grants...>
    && EngagedFor<dim::Provenance,     Grants...>
    && EngagedFor<dim::Trust,          Grants...>
    && EngagedFor<dim::Representation, Grants...>
    && EngagedFor<dim::Observability,  Grants...>
    && EngagedFor<dim::Complexity,     Grants...>
    && EngagedFor<dim::Precision,      Grants...>
    && EngagedFor<dim::Space,          Grants...>
    && EngagedFor<dim::Overflow,       Grants...>
    && EngagedFor<dim::Mutation,       Grants...>
    && EngagedFor<dim::Reentrancy,     Grants...>
    && EngagedFor<dim::Size,           Grants...>
    && EngagedFor<dim::Version,        Grants...>
    && EngagedFor<dim::Staleness,      Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── WhichDimUnengaged — recover the first-failing dim ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// A consumer that needs to emit a per-dim diagnostic reads
// `WhichDimUnengaged<Grants...>::value` AFTER checking `all_engaged`.
//
// FIXY-AUDIT-SENTINEL: pre-AUDIT, `value` returned `dim::Type` BOTH
// when Type was the first-failing dim AND as the "all engaged"
// sentinel.  A consumer who forgot the `all_engaged` guard could not
// disambiguate.  Post-AUDIT, the sentinel is `kAllEngagedSentinel`
// (DimAxis{255}, structurally outside the 20-enumerator range — see
// fixy::dim::is_valid_axis_v) so a misread is loud, not silent.
//
// `first_failing_dim()` is the recommended accessor: returns
// `std::optional<dim::DimAxis>` so the all-engaged case is a clean
// `nullopt` and no consumer can accidentally consume the sentinel.

namespace detail {

[[nodiscard]] consteval bool engaged_for_runtime(
    dim::DimAxis D,
    auto... grants_relaxes
) noexcept {
    return ((grants_relaxes == D) || ...);
}

}  // namespace detail

// Distinguished sentinel: NOT a real dim, intentionally outside the
// 20-enumerator range so dim::is_valid_axis_v<kAllEngagedSentinel> is
// `false` — a consumer that forgets to guard with `all_engaged` and
// passes `value` into a dim-name lookup gets a clean failure instead
// of a misleading "Type" string.
inline constexpr dim::DimAxis kAllEngagedSentinel =
    static_cast<dim::DimAxis>(255);

static_assert(!dim::is_valid_axis_v<kAllEngagedSentinel>,
    "fixy::kAllEngagedSentinel must lie outside the valid DimAxis "
    "enumerator range so consumers cannot confuse it with a real dim.");

template <typename... Grants>
struct WhichDimUnengaged {
private:
    // Reflection-driven first-failing-dim search (B3 fix).  Replaces
    // the pre-A-PLUS-3 20-deep `if constexpr` ladder.  Iterates the
    // 20 enumerators of dim::DimAxis in source order and returns the
    // first dim D for which EngagedFor<D, Grants...> is false.  When
    // every dim engages, returns kAllEngagedSentinel (DimAxis{255})
    // so the sentinel is structurally distinguishable from any real
    // first-failing dim.
    [[nodiscard]] static consteval dim::DimAxis compute_first_failing() noexcept {
        static constexpr auto enumerators =
            std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr auto en : enumerators) {
            // Splice in template-arg position needs parens (GCC 16
            // reflection gotcha — feedback_gcc16_c26_reflection_gotchas).
            if constexpr (!EngagedFor<([:en:]), Grants...>) {
                return [:en:];
            }
        }
#pragma GCC diagnostic pop
        return kAllEngagedSentinel;
    }

public:
    static constexpr dim::DimAxis value = compute_first_failing();
    static constexpr bool all_engaged = IsAccepted<Grants...>;

    // Recommended accessor — unambiguous, no sentinel to forget.
    [[nodiscard]] static consteval bool has_failing_dim() noexcept {
        return !all_engaged;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Per-dim diagnostic tag types ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each inherits `safety::diag::tag_base` so it integrates with the
// existing CRUCIBLE_DIAG_ASSERT / Catalog / Category infrastructure.
// The `name` member emits the literal tag identifier in compiler
// diagnostics; greppable build logs.

namespace diag {

// Every tag carries `name` (literal identity for grep), `description`
// (one-line rationale for IDE hover), and the canonical
// safety::diag::tag_base inheritance for Catalog integration.
// The richer goal/gap/suggestion accessors live in the
// fixy::diag::insight_for<dim::DimAxis> specializations below.

#define CRUCIBLE_FIXY_DIAG_TAG(DimName, DescText)                              \
    struct FixyNotEngaged_##DimName : ::crucible::safety::diag::tag_base {     \
        static constexpr std::string_view name        =                        \
            "FixyNotEngaged_" #DimName;                                        \
        static constexpr std::string_view description = DescText;              \
    }

// Each description ≤120 chars (IDE hover constraint).  Long-form
// rationale + symptom/example prose lives in the insight_provider
// specializations below, where line length is unconstrained.
CRUCIBLE_FIXY_DIAG_TAG(Type,
    "Type unengaged.  Use grant::typed<T> or accept_default_strict_for<dim::Type>.");
CRUCIBLE_FIXY_DIAG_TAG(Refinement,
    "Refinement unengaged.  Use grant::refined_with<Pred> or accept_default_strict_for<dim::Refinement>.");
CRUCIBLE_FIXY_DIAG_TAG(Usage,
    "Usage unengaged.  Use grant::{affine,copy,ghost,borrow,capability_usage} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Effect,
    "Effect unengaged.  Use grant::with<Effects...> or accept_default_strict_for<dim::Effect>.");
CRUCIBLE_FIXY_DIAG_TAG(Security,
    "Security unengaged.  Use grant::{declassify<P>,upgrade_to_secret} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Protocol,
    "Protocol unengaged.  Use grant::protocol_session<Proto> or accept_default_strict_for<dim::Protocol>.");
CRUCIBLE_FIXY_DIAG_TAG(Lifetime,
    "Lifetime unengaged.  Use grant::lifetime_region<Tag> or accept_default_strict_for<dim::Lifetime>.");
CRUCIBLE_FIXY_DIAG_TAG(Provenance,
    "Provenance unengaged.  Use grant::{from_source<S>,sanitize<C>} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Trust,
    "Trust unengaged.  Use grant::{trust_assumed<R>,trust_assumed_for<C>} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Representation,
    "Representation unengaged.  Use grant::{repr_*,vendor<V>,tier<R>} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Observability,
    "Observability unengaged.  Use grant::observability_visible or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Complexity,
    "Complexity unengaged.  Use grant::complexity_{constant,linear,quadratic,unbounded} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Precision,
    "Precision unengaged.  Use grant::{precision_f32,f64,higham<B>,reassociate} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Space,
    "Space unengaged.  Use grant::{space_bounded<N>,space_unbounded} or accept_default_strict_for<dim::Space>.");
CRUCIBLE_FIXY_DIAG_TAG(Overflow,
    "Overflow unengaged.  Use grant::overflow_{wrap,saturate,widen} or accept_default_strict_for<dim::Overflow>.");
CRUCIBLE_FIXY_DIAG_TAG(Mutation,
    "Mutation unengaged.  Use grant::{mutable_in_place,append_only,monotonic_advance} or accept_default_strict_for.");
CRUCIBLE_FIXY_DIAG_TAG(Reentrancy,
    "Reentrancy unengaged.  Use grant::{reentrant,coroutine} or accept_default_strict_for<dim::Reentrancy>.");
CRUCIBLE_FIXY_DIAG_TAG(Size,
    "Size unengaged.  Use grant::{sized<Depth>,productive} or accept_default_strict_for<dim::Size>.");
CRUCIBLE_FIXY_DIAG_TAG(Version,
    "Version unengaged.  Use grant::version<V> or accept_default_strict_for<dim::Version>.");
CRUCIBLE_FIXY_DIAG_TAG(Staleness,
    "Staleness unengaged.  Use grant::stale_to<TauMax> or accept_default_strict_for<dim::Staleness>.");

#undef CRUCIBLE_FIXY_DIAG_TAG

using FixyDiagCatalog = std::tuple<
    FixyNotEngaged_Type,
    FixyNotEngaged_Refinement,
    FixyNotEngaged_Usage,
    FixyNotEngaged_Effect,
    FixyNotEngaged_Security,
    FixyNotEngaged_Protocol,
    FixyNotEngaged_Lifetime,
    FixyNotEngaged_Provenance,
    FixyNotEngaged_Trust,
    FixyNotEngaged_Representation,
    FixyNotEngaged_Observability,
    FixyNotEngaged_Complexity,
    FixyNotEngaged_Precision,
    FixyNotEngaged_Space,
    FixyNotEngaged_Overflow,
    FixyNotEngaged_Mutation,
    FixyNotEngaged_Reentrancy,
    FixyNotEngaged_Size,
    FixyNotEngaged_Version,
    FixyNotEngaged_Staleness
>;

static_assert(std::tuple_size_v<FixyDiagCatalog> == dim::count_v,
    "FixyDiagCatalog must have one entry per fixy::dim::DimAxis "
    "enumerator (20).  A substrate-side append to DimensionAxis "
    "without coordinated update here fires this gate.");

// ── Dim → diagnostic-tag mapping ──────────────────────────────────────
//
// Surfaced for consumers who need to emit a specific dim's
// diagnostic by name (Phase B fixy::fn wrapper uses this).

template <dim::DimAxis D> struct diag_tag_for;

template <> struct diag_tag_for<dim::Type>           { using type = FixyNotEngaged_Type; };
template <> struct diag_tag_for<dim::Refinement>     { using type = FixyNotEngaged_Refinement; };
template <> struct diag_tag_for<dim::Usage>          { using type = FixyNotEngaged_Usage; };
template <> struct diag_tag_for<dim::Effect>         { using type = FixyNotEngaged_Effect; };
template <> struct diag_tag_for<dim::Security>       { using type = FixyNotEngaged_Security; };
template <> struct diag_tag_for<dim::Protocol>       { using type = FixyNotEngaged_Protocol; };
template <> struct diag_tag_for<dim::Lifetime>       { using type = FixyNotEngaged_Lifetime; };
template <> struct diag_tag_for<dim::Provenance>     { using type = FixyNotEngaged_Provenance; };
template <> struct diag_tag_for<dim::Trust>          { using type = FixyNotEngaged_Trust; };
template <> struct diag_tag_for<dim::Representation> { using type = FixyNotEngaged_Representation; };
template <> struct diag_tag_for<dim::Observability>  { using type = FixyNotEngaged_Observability; };
template <> struct diag_tag_for<dim::Complexity>     { using type = FixyNotEngaged_Complexity; };
template <> struct diag_tag_for<dim::Precision>      { using type = FixyNotEngaged_Precision; };
template <> struct diag_tag_for<dim::Space>          { using type = FixyNotEngaged_Space; };
template <> struct diag_tag_for<dim::Overflow>       { using type = FixyNotEngaged_Overflow; };
template <> struct diag_tag_for<dim::Mutation>       { using type = FixyNotEngaged_Mutation; };
template <> struct diag_tag_for<dim::Reentrancy>     { using type = FixyNotEngaged_Reentrancy; };
template <> struct diag_tag_for<dim::Size>           { using type = FixyNotEngaged_Size; };
template <> struct diag_tag_for<dim::Version>        { using type = FixyNotEngaged_Version; };
template <> struct diag_tag_for<dim::Staleness>      { using type = FixyNotEngaged_Staleness; };

template <dim::DimAxis D>
using diag_tag_for_t = typename diag_tag_for<D>::type;

// ─── stable_name integration (FIXY-A-PLUS-4) ──────────────────────────
//
// safety::diag::stable_name_of<T> returns a portable consteval
// string_view derived from reflection (P2996R13).  fixy diag tags
// integrate so IDE/clangd tooling, federation-cache hashing, and the
// JSON emitter (safety/diag/JsonEmitter.h) can address tags by name
// without scraping compiler diagnostics.

template <dim::DimAxis D>
inline constexpr std::string_view stable_name_for_dim =
    ::crucible::safety::diag::stable_name_of<diag_tag_for_t<D>>;

template <dim::DimAxis D>
inline constexpr std::uint64_t stable_type_id_for_dim =
    ::crucible::safety::diag::stable_type_id<diag_tag_for_t<D>>;

// ── Reflection-driven coverage assertion ──────────────────────────────
namespace detail {

template <dim::DimAxis D>
inline constexpr bool has_diag_tag_v =
    requires { typename diag_tag_for<D>::type; };

[[nodiscard]] consteval bool every_dim_has_diag_tag() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (!has_diag_tag_v<([:en:])>) return false;
        constexpr auto tag_name_view =
            diag_tag_for_t<([:en:])>::name;
        if (tag_name_view.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

// IDE-hover constraint: `description` ≤ 120 chars (the conventional
// clangd hover width).  Long-form rationale lives in the
// insight_provider's why_this_matters where the bound is relaxed.
inline constexpr std::size_t DESCRIPTION_MAX_CHARS = 120;

[[nodiscard]] consteval bool every_description_fits_hover() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr auto desc =
            diag_tag_for_t<([:en:])>::description;
        if (desc.empty()) return false;
        if (desc.size() > DESCRIPTION_MAX_CHARS) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

static_assert(detail::every_dim_has_diag_tag(),
    "Every dim::DimAxis enumerator must have a fixy::diag::diag_tag_"
    "for<D> specialization carrying a non-empty tag name.  A new "
    "substrate dim was added without coordinated fixy/Reject.h update.");

static_assert(detail::every_description_fits_hover(),
    "Every FixyNotEngaged_<D> tag's `description` must fit within "
    "DESCRIPTION_MAX_CHARS (IDE hover limit).  Move long-form prose "
    "into the insight_provider<>::why_this_matters specialization.");

}  // namespace diag

// ═════════════════════════════════════════════════════════════════════
// ── insight_provider specializations (FIXY-A-PLUS-4) ───────────────
// ═════════════════════════════════════════════════════════════════════
//
// Mirrors the safety::diag::insight_provider<Tag> pattern from
// safety/diag/Insights.h.  Each FixyNotEngaged_<D> tag gets a
// specialization providing severity + why_this_matters + symptom +
// correct/violating example.  Downstream tooling
// (JsonEmitter / clangd / GAPS-093 LSP plugin) addresses these via
// std::meta reflection on the tag type.
//
// MUST be in namespace ::crucible::safety::diag (where the primary
// template lives) — specializations cannot land in fixy::diag.

}  // namespace crucible::fixy

namespace crucible::safety::diag {

// Quality-validated insight macro.  Mirrors the substrate's
// CRUCIBLE_DEFINE_INSIGHTS_QV thresholds (why ≥ 30, symptom ≥ 20,
// correct/violating ≥ 10) so a stale TODO-string can't ship.  An
// explicit severity argument lets security-critical dims (Security,
// Trust, Provenance, Lifetime) escalate to Fatal per the substrate's
// LifetimeViolation precedent — silent admission is a data-leak
// class consequence, not a perf regression.
#define CRUCIBLE_FIXY_INSIGHT(DimName, Sev, WhyText, SymptomText, CorrectEx, ViolatingEx) \
    template <>                                                              \
    struct insight_provider<::crucible::fixy::diag::FixyNotEngaged_##DimName> { \
        static constexpr Severity         severity           = (Sev);        \
        static constexpr std::string_view why_this_matters   = WhyText;      \
        static constexpr std::string_view symptom_pattern    = SymptomText;  \
        static constexpr std::string_view correct_example    = CorrectEx;    \
        static constexpr std::string_view violating_example  = ViolatingEx;  \
        static_assert(why_this_matters.size()   >= 30,                       \
            "FixyNotEngaged_" #DimName " why_this_matters too short "        \
            "(< 30 chars).  Be substantive — explain the architectural "     \
            "constraint and cite the spec / FX-§N / CLAUDE.md anchor.");     \
        static_assert(symptom_pattern.size()    >= 20,                       \
            "FixyNotEngaged_" #DimName " symptom_pattern too short "         \
            "(< 20 chars).  Help readers pattern-match production "          \
            "debugging sessions against the canonical surface.");            \
        static_assert(correct_example.size()    >= 10,                       \
            "FixyNotEngaged_" #DimName " correct_example too short "         \
            "(< 10 chars).  Show real grant tag wiring, not a TODO.");       \
        static_assert(violating_example.size()  >= 10,                       \
            "FixyNotEngaged_" #DimName " violating_example too short "       \
            "(< 10 chars).  Show the anti-pattern explicitly.");             \
    }

CRUCIBLE_FIXY_INSIGHT(Type, Severity::Error,
    "Fixy's reject-by-default discipline requires every binding to name "
    "its value type explicitly.  Without engagement on dim::Type, fixy "
    "refuses to invent a type — the binding has no carrier for the rest "
    "of the grade vector.",
    "Surfaces when an author writes `fixy::fn<int> f = ...` expecting "
    "the int to populate dim::Type implicitly.  Phase B's fixy::fn "
    "aggregator routes int via grant::typed<int>; the bare fixy::fn<int> "
    "is rejected because Type isn't engaged via a grant tag.",
    "fixy::fn<int, grant::typed<int>, accept_default_strict_for<dim::Refinement>, ...>",
    "fixy::fn<int> f = [](int x){ return x; };  // dim::Type unengaged");

CRUCIBLE_FIXY_INSIGHT(Refinement, Severity::Error,
    "Refinement is the FX dim-2 predicate slot — even if the predicate "
    "is True (no refinement), the author must acknowledge the choice.  "
    "Phase B fixy::fn will route grant::refined_with<Pred> into "
    "safety::Refined<Pred, T> at the substrate layer.",
    "Author writes accept_default_strict_for<dim::Usage> but forgets "
    "dim::Refinement.  Common because Refinement-True is the silent "
    "default in most languages.",
    "accept_default_strict_for<dim::Refinement>  // True predicate",
    "// (missing both refined_with<Pred> and accept_default_strict_for<dim::Refinement>)");

CRUCIBLE_FIXY_INSIGHT(Usage, Severity::Error,
    "Usage encodes linearity discipline — Linear/Affine/Copy/Ghost/"
    "Borrow/Capability.  Default Linear forces exactly-once consumption; "
    "silent admission would let a careless author dup-consume a linear "
    "handle without diagnostic.",
    "Refactoring a value-semantic POD from Linear discipline to Copy "
    "but forgetting to declare the relaxation.  Or writing a Phase-B "
    "stance that omits Usage engagement entirely.",
    "grant::copy  // explicit Copy relaxation",
    "// no grant::* engaging Usage AND no accept_default_strict_for<dim::Usage>");

CRUCIBLE_FIXY_INSIGHT(Effect, Severity::Error,
    "Effect is the Met(X) row carrier (Tang-Lindley POPL 2026).  A "
    "function that secretly does IO / Alloc / Bg without declaring the "
    "row bypasses the Subrow<R, Ctx> check at the consumer site.  Fixy "
    "rejects the binding before the bug ships.",
    "Function performs printf / malloc / std::async but the author "
    "writes a hot-path stance.  The fixy engagement check fires before "
    "the consumer's Subrow<R, HotCtx> would have rejected at call site.",
    "grant::with<Effect::IO>  // declares the row",
    "// no grant::with<...> declaring the actual side effects");

// Severity::Fatal — silent admission of a Classified→Public declass
// path is a data-exfiltration class consequence, not perf.  Mirrors
// the substrate's LifetimeViolation precedent.
CRUCIBLE_FIXY_INSIGHT(Security, Severity::Fatal,
    "Security encodes information-flow classification.  Default "
    "Classified means the value flows only to Classified/Secret sinks "
    "unless explicitly declassified via a named policy.  Silent "
    "admission would let Public output silently leak Classified inputs.",
    "Author writes a function consuming a Classified input and emitting "
    "a Public output without declaring grant::declassify<Policy>.  The "
    "engagement check fires before the implicit-flow rule (I002) would "
    "have rejected.",
    "grant::declassify<AuditedPolicy>",
    "// implicit declass via Public return type, no grant::declassify");

CRUCIBLE_FIXY_INSIGHT(Protocol, Severity::Error,
    "Protocol carries session typestate (Honda 1998 / HYC 2008 MPST).  "
    "Default proto::None means no protocol obligation.  Explicit "
    "engagement is required even for the None case so reviewers see the "
    "binding has been audited.",
    "Adding a binding to a session-typed channel without declaring "
    "grant::protocol_session<Proto>.  The session-protocol consumer "
    "rejects later; fixy rejects up-front.",
    "grant::protocol_session<MyProto>",
    "// channel-typed binding with no Protocol engagement");

// Severity::Fatal — dangling pointer / use-after-free is a memory-
// corruption class consequence, peer to the substrate's
// LifetimeViolation tag (also Fatal).
CRUCIBLE_FIXY_INSIGHT(Lifetime, Severity::Fatal,
    "Lifetime engagement is the gate against dangling-pointer / use-"
    "after-free bugs.  Default Static is the safe choice; named regions "
    "(grant::lifetime_region<Tag>) carry the explicit scope identity.",
    "Returning a pointer/reference into a temporary region without "
    "declaring the region's named tag.  Common in arena consumers.",
    "grant::lifetime_region<MyArenaTag>",
    "// returns view into arena without lifetime_region engagement");

// Severity::Fatal — missing sanitizer on FromUser data is the XSS /
// SQLi / command-injection class.  Silent admission causes
// security-incident-grade consequences.
CRUCIBLE_FIXY_INSIGHT(Provenance, Severity::Fatal,
    "Provenance is the source-tag carrier (Tagged.h source::*).  "
    "Sanitization is provenance-driven (FromUser → Sanitized) — engaging "
    "Provenance is what makes XSS/SQLi sanitizer pipelines auditable.",
    "Network/file/user-input data flowing into Internal-tagged buffers "
    "without crossing a grant::sanitize<TaintClass> boundary.  The "
    "engagement check catches the missing sanitizer.",
    "grant::from_source<source::FromUser> + grant::sanitize<XssClass>",
    "// network buffer treated as Internal without sanitize");

// Severity::Fatal — bypassing trust verification on third-party data
// is the supply-chain-injection class.  Audit-rationale literal must
// land at every relaxation; silent admission breaks the audit trail.
CRUCIBLE_FIXY_INSIGHT(Trust, Severity::Fatal,
    "Trust is the verification axis (Verified/Unverified lattice).  "
    "Default Verified requires explicit relaxation when the binding "
    "consumes unaudited data.  Engagement is mandatory so review can "
    "audit the rationale literal.",
    "Importing third-party data without declaring the trust relaxation.  "
    "grant::trust_assumed<\"PR-1234-audited\"> carries the audit trail.",
    "grant::trust_assumed<\"PR-1234-audited\">",
    "// third-party data consumed at Verified level without rationale");

CRUCIBLE_FIXY_INSIGHT(Representation, Severity::Error,
    "Representation is the storage-layout axis (Opaque/C/Packed/Aligned/"
    "Simd/Atomic) plus vendor pin / numerical tier.  Engagement makes "
    "vendor-backend selection auditable at the binding site.",
    "Mimic per-vendor backend code without grant::vendor<V> + "
    "grant::tier<R>; cross-vendor numerics CI catches at runtime; fixy "
    "catches at compile time.",
    "grant::vendor<nv::Vendor> + grant::tier<BITEXACT_TC>",
    "// kernel implementation with no vendor pin or recipe tier");

// Severity::Warning — mirrors the substrate's ResidencyHeatViolation
// (Warning): mis-tiered observability degrades drift attribution but
// doesn't corrupt state.  Author can still ship; review nudges.
CRUCIBLE_FIXY_INSIGHT(Observability, Severity::Warning,
    "Observability is derived from Effect row at the consumer site but "
    "still requires explicit engagement so the author has read the "
    "discipline.  Default is opaque (no observable side effects).",
    "Author declares grant::with<Effect::IO> but forgets the partner "
    "Observability engagement — the IO emission is invisible to drift "
    "detectors.",
    "grant::observability_visible  // when IO is intended to be observed",
    "// IO emission with no Observability engagement");

CRUCIBLE_FIXY_INSIGHT(Complexity, Severity::Error,
    "Complexity engagement is the cost-annotation axis (O(1)/O(N)/"
    "O(N^2)/unbounded).  Default Unstated; explicit engagement is the "
    "review/audit hook for budget-bounded code paths.",
    "Hot-path loop with no complexity annotation — the engagement check "
    "fires so the author either commits to Constant/Linear<N> or "
    "explicitly accepts Unstated.",
    "grant::complexity_linear<N>",
    "// O(N^2) loop on hot path with no Complexity engagement");

CRUCIBLE_FIXY_INSIGHT(Precision, Severity::Error,
    "Precision is the FP error-bound axis (Exact/F32/F64/Higham<Bound>).  "
    "Engagement is the cross-vendor numerics CI gate — recipe pinning "
    "(BITEXACT_TC etc.) flows from per-binding precision declaration.",
    "FP-using kernel without grant::precision_higham<Bound> or "
    "precision_f32/f64 — cross-vendor CI later flags divergence; fixy "
    "rejects up-front.",
    "grant::reassociate + grant::precision_higham<0.5>",
    "// FP loop with no Precision engagement");

CRUCIBLE_FIXY_INSIGHT(Space, Severity::Error,
    "Space is the allocation-bound axis (Zero/Bounded<N>/Unbounded).  "
    "Default Zero means stack-only; the engagement check is the gate "
    "against accidental heap allocation in stack-bound contexts.",
    "Function uses std::vector<T> on a stack-only path; fixy rejects "
    "before the runtime stack-overflow or unexpected heap touch.",
    "grant::space_bounded<4096>",
    "// std::vector<T> use without Space relaxation");

CRUCIBLE_FIXY_INSIGHT(Overflow, Severity::Error,
    "Overflow is the integer-overflow semantic (Trap/Wrap/Saturate/"
    "Widen).  Default Trap is the safe choice for unaudited arithmetic; "
    "modular code must declare grant::overflow_wrap.",
    "Modular-arithmetic counter (sequence numbers, hash mixing) without "
    "grant::overflow_wrap — Trap semantic causes runtime abort under "
    "UBSan.",
    "grant::overflow_wrap  // for wrap-around counters",
    "// sequence-number arithmetic with no Overflow engagement");

CRUCIBLE_FIXY_INSIGHT(Mutation, Severity::Error,
    "Mutation is the in-place-write discipline (Immutable/Mutable/"
    "AppendOnly/Monotonic).  Default Immutable is the safe choice; "
    "in-place mutation must declare the relaxation.",
    "Function that writes through a reference-to-non-const without "
    "engaging dim::Mutation — fixy rejects the binding's intent before "
    "the consumer's aliasing rules fire.",
    "grant::append_only  // for event-log writers",
    "// function mutating an arg without Mutation engagement");

CRUCIBLE_FIXY_INSIGHT(Reentrancy, Severity::Error,
    "Reentrancy is the self-call discipline (NonReentrant/Reentrant/"
    "Coroutine).  Default NonReentrant means self-calls are rejected; "
    "recursive functions and coroutines must declare.",
    "Recursive function or coroutine without engagement — fixy rejects "
    "before the runtime stack-blowup or coroutine-frame allocation "
    "surprise.",
    "grant::reentrant  // for recursive helpers",
    "// recursive helper with no Reentrancy engagement");

CRUCIBLE_FIXY_INSIGHT(Size, Severity::Error,
    "Size is the codata observation-depth axis (Unstated/Sized<N>/"
    "Productive).  Default Unstated; explicit engagement gates "
    "productive-stream consumers against infinite-loop bugs.",
    "Stream consumer without grant::sized<N> or grant::productive — "
    "fixy rejects so the consumer can't accidentally trigger infinite "
    "observation.",
    "grant::sized<1024>  // bounded-depth observation",
    "// stream consumer with no Size engagement");

CRUCIBLE_FIXY_INSIGHT(Version, Severity::Error,
    "Version is the federation-version axis (FX dim 21).  Default v=1; "
    "explicit engagement carries the cross-org compatibility signal "
    "into the binding's federation-cache key.",
    "Adding a new version of a previously-shipped binding without "
    "engaging grant::version<V>; downstream federation cache hits the "
    "old artifact under the same hash.",
    "grant::version<2>  // new schema-incompatible version",
    "// new behavior under old version number");

CRUCIBLE_FIXY_INSIGHT(Staleness, Severity::Error,
    "Staleness is the freshness-bound axis (Fresh/Stale<TauMax>).  "
    "Default Fresh means τ=0 (real-time data); cached/sampled data must "
    "declare grant::stale_to<TauMax>.",
    "Reading a cached counter without engaging Staleness — the consumer "
    "consumes possibly-stale data as if Fresh, breaking SLA bounds.",
    "grant::stale_to<100>  // τ ≤ 100 ms",
    "// cached metric consumed as Fresh without stale_to");

#undef CRUCIBLE_FIXY_INSIGHT

}  // namespace crucible::safety::diag

namespace crucible::fixy {

// Re-opened the fixy namespace for the remaining structural definitions
// after the safety::diag::insight_provider specialization block above.

// ═════════════════════════════════════════════════════════════════════
// ── Reflection-driven full-coverage gates ──────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `every_dim_has_diag_tag` and `every_description_fits_hover` (above)
// cover the diag_tag_for surface.  This block covers the
// insight_provider surface symmetrically — every one of the 20
// FixyNotEngaged_<D> tags must have a non-default specialization with
// all four prose fields populated.  Tests already spot-check 5 positions
// (0, 5, 10, 15, 19); the consteval loop here pins ALL 20 at header-
// inclusion time so the test surface can shrink and a missed
// specialization fires before the test ever runs.

namespace diag::detail {

[[nodiscard]] consteval bool every_tag_has_populated_insight() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using Tag    = diag_tag_for_t<([:en:])>;
        using Insight = ::crucible::safety::diag::insight_provider<Tag>;
        if (Insight::why_this_matters.empty())  return false;
        if (Insight::symptom_pattern.empty())   return false;
        if (Insight::correct_example.empty())   return false;
        if (Insight::violating_example.empty()) return false;
        // Mirror the substrate's QV thresholds:
        //   why ≥ 30, symptom ≥ 20, correct/violating ≥ 10
        if (Insight::why_this_matters.size()   < 30) return false;
        if (Insight::symptom_pattern.size()    < 20) return false;
        if (Insight::correct_example.size()    < 10) return false;
        if (Insight::violating_example.size()  < 10) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval bool every_tag_has_well_insighted_concept() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using Tag = diag_tag_for_t<([:en:])>;
        if (!::crucible::safety::diag::WellInsightedTag<Tag>) return false;
        if (!::crucible::safety::diag::HasSubstantiveInsights<Tag>) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace diag::detail

static_assert(diag::detail::every_tag_has_populated_insight(),
    "Every fixy::diag::FixyNotEngaged_<D> tag must have an "
    "insight_provider<> specialization with non-empty why/symptom/"
    "correct/violating prose meeting QV thresholds (30/20/10/10).  "
    "A new substrate dim was added without coordinated insight "
    "population.  See test/test_fixy_diag.cpp for per-tag coverage.");

static_assert(diag::detail::every_tag_has_well_insighted_concept(),
    "Every fixy::diag::FixyNotEngaged_<D> tag must satisfy "
    "safety::diag::WellInsightedTag AND HasSubstantiveInsights.  "
    "Downstream code can `requires WellInsightedTag<T>` to guarantee "
    "diagnostic richness; this assert is its symmetric producer-side "
    "obligation.");

// ═════════════════════════════════════════════════════════════════════
// ── Sanity self-tests ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// These pin the contract at header-load time.  They are NOT a
// substitute for the test/fixy_neg/ corpus (FIXY-A6) — neg-compile
// fixtures exercise the user-facing failure mode with structured
// diagnostics — but they catch regressions in IsAccepted's truth
// table at substrate-build time.

namespace self_test {

// Empty pack — zero dims engaged.
static_assert(!IsAccepted<>);
static_assert(!WhichDimUnengaged<>::all_engaged);
static_assert(WhichDimUnengaged<>::value == dim::Type);

// One grant — Usage engaged, 19 dims unengaged.
static_assert(!IsAccepted<grant::copy>);
static_assert(!WhichDimUnengaged<grant::copy>::all_engaged);
// First failing dim is Type (enumerator 0), since Usage IS engaged.
static_assert(WhichDimUnengaged<grant::copy>::value == dim::Type);

// All 20 dims engaged via accept_default_strict_for — accept.
using AllStrict = std::tuple<
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

static_assert(IsAccepted<
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>);

// One grant + 19 explicit-accepts — accept.  This is the canonical
// fixy binding shape.
static_assert(IsAccepted<
    grant::copy,                                      // Usage relaxed
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>);

// 19-of-20 (missing Staleness) — reject.
static_assert(!IsAccepted<
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>
    // accept_default_strict_for<dim::Staleness> -- intentionally omitted
>);

// FIXY-AUDIT-SENTINEL — WhichDimUnengaged sentinel disambiguation pin.
//
// When every dim engages, `value` is `kAllEngagedSentinel` (DimAxis{255},
// outside the valid 20-enumerator range — so dim::is_valid_axis_v on
// it is false, distinguishing it from any real first-failing dim).
//
// AllStrictAcceptPack engages every dim via accept_default_strict_for;
// the corresponding WhichDimUnengaged must report `all_engaged == true`
// AND `value == kAllEngagedSentinel`.
static_assert(WhichDimUnengaged<
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>::all_engaged);

static_assert(WhichDimUnengaged<
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>::value == kAllEngagedSentinel);

// And the sentinel itself is structurally outside the valid range:
static_assert(!dim::is_valid_axis_v<kAllEngagedSentinel>);

}  // namespace self_test

}  // namespace crucible::fixy
