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
//       static constexpr dim::DimAxis value = ...;   // dim::Type if all engaged
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
//                instantiation time; no runtime state involved.
//   TypeSafe   — fold-expression dispatch on `Grants::relaxes`
//                (strong enum); cross-dim confusion is a compile
//                error.  20 diagnostic tags are non-convertible.
//   DetSafe    — concept evaluation is constexpr / bit-identical
//                across compiles.
//   LeakSafe   — zero-state types.
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

template <dim::DimAxis D, typename... Grants>
concept EngagedFor = (false || ... || (Grants::relaxes == D));

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
// `WhichDimUnengaged<Grants...>::value`.  If `all_engaged` is true,
// `value` is `dim::Type` (a meaningless sentinel — guard with
// `all_engaged` first).  Implementation iterates the 20 dims in
// enumerator order and returns the first failure.

namespace detail {

[[nodiscard]] consteval bool engaged_for_runtime(
    dim::DimAxis D,
    auto... grants_relaxes
) noexcept {
    return ((grants_relaxes == D) || ...);
}

}  // namespace detail

template <typename... Grants>
struct WhichDimUnengaged {
private:
    [[nodiscard]] static consteval dim::DimAxis compute_first_failing() noexcept {
        // Per-dim engagement, in enumerator order.  Branch on the
        // first failure; if every dim engages, return dim::Type as
        // a sentinel.  The accompanying `all_engaged` flag tells
        // callers whether `value` is meaningful.
        if constexpr (!EngagedFor<dim::Type, Grants...>)           return dim::Type;
        else if constexpr (!EngagedFor<dim::Refinement, Grants...>)     return dim::Refinement;
        else if constexpr (!EngagedFor<dim::Usage, Grants...>)          return dim::Usage;
        else if constexpr (!EngagedFor<dim::Effect, Grants...>)         return dim::Effect;
        else if constexpr (!EngagedFor<dim::Security, Grants...>)       return dim::Security;
        else if constexpr (!EngagedFor<dim::Protocol, Grants...>)       return dim::Protocol;
        else if constexpr (!EngagedFor<dim::Lifetime, Grants...>)       return dim::Lifetime;
        else if constexpr (!EngagedFor<dim::Provenance, Grants...>)     return dim::Provenance;
        else if constexpr (!EngagedFor<dim::Trust, Grants...>)          return dim::Trust;
        else if constexpr (!EngagedFor<dim::Representation, Grants...>) return dim::Representation;
        else if constexpr (!EngagedFor<dim::Observability, Grants...>)  return dim::Observability;
        else if constexpr (!EngagedFor<dim::Complexity, Grants...>)     return dim::Complexity;
        else if constexpr (!EngagedFor<dim::Precision, Grants...>)      return dim::Precision;
        else if constexpr (!EngagedFor<dim::Space, Grants...>)          return dim::Space;
        else if constexpr (!EngagedFor<dim::Overflow, Grants...>)       return dim::Overflow;
        else if constexpr (!EngagedFor<dim::Mutation, Grants...>)       return dim::Mutation;
        else if constexpr (!EngagedFor<dim::Reentrancy, Grants...>)     return dim::Reentrancy;
        else if constexpr (!EngagedFor<dim::Size, Grants...>)           return dim::Size;
        else if constexpr (!EngagedFor<dim::Version, Grants...>)        return dim::Version;
        else if constexpr (!EngagedFor<dim::Staleness, Grants...>)      return dim::Staleness;
        else return dim::Type;  // sentinel — `all_engaged` will be true
    }

public:
    static constexpr dim::DimAxis value = compute_first_failing();
    static constexpr bool all_engaged = IsAccepted<Grants...>;
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

struct FixyNotEngaged_Type           : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Type"; };
struct FixyNotEngaged_Refinement     : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Refinement"; };
struct FixyNotEngaged_Usage          : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Usage"; };
struct FixyNotEngaged_Effect         : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Effect"; };
struct FixyNotEngaged_Security       : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Security"; };
struct FixyNotEngaged_Protocol       : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Protocol"; };
struct FixyNotEngaged_Lifetime       : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Lifetime"; };
struct FixyNotEngaged_Provenance     : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Provenance"; };
struct FixyNotEngaged_Trust          : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Trust"; };
struct FixyNotEngaged_Representation : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Representation"; };
struct FixyNotEngaged_Observability  : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Observability"; };
struct FixyNotEngaged_Complexity     : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Complexity"; };
struct FixyNotEngaged_Precision      : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Precision"; };
struct FixyNotEngaged_Space          : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Space"; };
struct FixyNotEngaged_Overflow       : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Overflow"; };
struct FixyNotEngaged_Mutation       : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Mutation"; };
struct FixyNotEngaged_Reentrancy     : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Reentrancy"; };
struct FixyNotEngaged_Size           : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Size"; };
struct FixyNotEngaged_Version        : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Version"; };
struct FixyNotEngaged_Staleness      : ::crucible::safety::diag::tag_base { static constexpr std::string_view name = "FixyNotEngaged_Staleness"; };

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

}  // namespace detail

static_assert(detail::every_dim_has_diag_tag(),
    "Every dim::DimAxis enumerator must have a fixy::diag::diag_tag_"
    "for<D> specialization carrying a non-empty tag name.  A new "
    "substrate dim was added without coordinated fixy/Reject.h update.");

}  // namespace diag

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

}  // namespace self_test

}  // namespace crucible::fixy
