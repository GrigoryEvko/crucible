#pragma once

// ── crucible::fixy — Reject.h — the IsAccepted engagement gate ─────
//
// Phase A of the clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// THIS IS THE LOAD-BEARING FIXY HEADER.  Every fixy:: binding gates
// through `IsAccepted<Type, Grants...>` which combines:
//
//   (a) every grant in `Grants...` is an `IsGrantTag` (final-class +
//       inherits grant_base — defends against trait-spec injection)
//
//   (b) every DimensionAxis enumerator (the 20 axes from
//       safety::DimensionAxis) is engaged via at least one grant —
//       i.e., the author has read the discipline and made a choice
//       on every dim
//
//   (c) (Phase B hookup) the projected Fn<...> instantiation passes
//       safety::fn::ValidComposition — the 12 §6.8 collision rules
//       from safety/CollisionCatalog.h.  Phase A ships the concept
//       hookup but only fires it when Resolve.h is wired in Phase B;
//       Phase A consumers exercise the engagement-check path only.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety/CollisionCatalog.h::ValidComposition<F>   — 12-rule §6.8
//   safety/Diagnostic.h::tag_base                    — diag-tag base
//   safety/diag/Insights.h::insight_provider         — per-tag Why/
//                                                       Symptom/etc.
//
// ── Substrate added by this header ─────────────────────────────────
//
// Twenty `FixyNotEngaged_<Axis>` diagnostic tags, one per dimension.
// These inherit `safety::diag::tag_base` (so they participate in the
// substrate's structural diagnostic surface) but do NOT enter the
// closed `safety::diag::Category` enum (which is reserved for
// substrate-axis violations per FOUND-E01's Catalog discipline).
//
// User-defined diagnostic tags follow the same pattern (per
// `safety/Diagnostic.h:96-99`); fixy:: diagnostic tags participate in
// `diagnostic_name_v` / `Diagnostic<TagType, Ctx...>` accessors
// without occupying a foundation Category slot.
//
// ── Diagnostic surface ─────────────────────────────────────────────
//
// When IsAccepted rejects a grant pack, the FIRST missing-engagement
// dimension fires the structured diagnostic.  Compiler error message
// names the specific dim (e.g., "FixyNotEngaged_Effect") + carries a
// remediation message pointing at the relevant grants.  The other 19
// dims surface in a single summary line to keep the diagnostic tight
// (R4 of misc/16_05_2026_fixy.md §9).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — every concept gate is a structural concept; no
//              implicit conversion path exists.
//   InitSafe — no state; pure compile-time computation.
//   DetSafe  — same `Grants...` → same `IsAccepted` outcome → same
//              cache key (relevant for federation cache, GAPS-028).
//
// ── Runtime cost ───────────────────────────────────────────────────
//
// Zero.  Every concept evaluates at template instantiation; the
// fixy::fn<> wrapper (Phase B) carries no runtime state.

#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/Diagnostic.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── Per-dim diagnostic tags ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// One tag per DimensionAxis enumerator.  Each inherits
// safety::diag::tag_base + ships the 3 string_view fields the
// foundation expects.  Users querying via
// `safety::diag::diagnostic_name_v<FixyNotEngaged_Effect>` see the
// structured surface.
//
// Naming convention: `FixyNotEngaged_<AxisName>`.  Grep-discoverable;
// matches FOUND-E01's local-tag pattern.

namespace diag {

#define CRUCIBLE_FIXY_NOT_ENGAGED_TAG(AxisName, AxisDesc)                          \
    struct FixyNotEngaged_##AxisName final : ::crucible::safety::diag::tag_base {  \
        static constexpr ::std::string_view name =                                  \
            "FixyNotEngaged_" #AxisName;                                            \
        static constexpr ::std::string_view description =                           \
            "Dimension '" #AxisName "' (" AxisDesc ") has no engagement marker "    \
            "or relaxation tag in the binding's Grants pack.  Per misc/"            \
            "16_05_2026_fixy.md §3, every fixy:: binding MUST engage with every "   \
            "one of the 20 dimensions either via an explicit relaxation tag or "    \
            "via `grant::accept_default_strict_for<dim::DimensionAxis::"            \
            #AxisName ">`.";                                                        \
        static constexpr ::std::string_view remediation =                           \
            "Add `grant::accept_default_strict_for<dim::DimensionAxis::"            \
            #AxisName ">` to the Grants pack if the binding's behavior on this "    \
            "axis is the strict default, OR add the appropriate per-axis "          \
            "relaxation tag (see fixy/Grant.h's `crucible::fixy::grant` "           \
            "namespace for the catalog).";                                          \
    };                                                                              \
    static_assert(::crucible::safety::diag::is_diagnostic_class_v<                  \
                      FixyNotEngaged_##AxisName>,                                   \
        "FixyNotEngaged_" #AxisName " must inherit safety::diag::tag_base.")

CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Type,           "the function type itself");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Refinement,     "value-level predicate");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Usage,          "linear / affine / copy / ghost");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Effect,         "Bg / IO / Alloc / Block / etc.");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Security,       "Classified / Public / Secret");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Protocol,       "session-type / state machine");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Lifetime,       "Static / In<Region>");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Provenance,     "source provenance tag");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Trust,          "Verified / Tested / Assumed");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Representation, "memory layout discipline");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Observability,  "derived from Effect — accept only");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Complexity,     "O(1) / O(N) / O(N²) classification");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Precision,      "FP error bound");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Space,          "allocation footprint bound");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Overflow,       "Trap / Wrap / Saturate / Widen");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Mutation,       "Immutable / Append / Monotonic / Mutable");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Reentrancy,     "NonReentrant / Reentrant / Coroutine");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Size,           "codata observation depth");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Version,        "schema version number");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Staleness,      "freshness bound τ");

#undef CRUCIBLE_FIXY_NOT_ENGAGED_TAG

// ─── tag_for_axis<D> — DimensionAxis → diag tag ────────────────────
//
// Used by IsAccepted's failure reporter to surface the structured
// diagnostic for the FIRST unengaged dim.

template <dim::DimensionAxis D> struct tag_for_axis;

template <> struct tag_for_axis<dim::DimensionAxis::Type>           { using type = FixyNotEngaged_Type; };
template <> struct tag_for_axis<dim::DimensionAxis::Refinement>     { using type = FixyNotEngaged_Refinement; };
template <> struct tag_for_axis<dim::DimensionAxis::Usage>          { using type = FixyNotEngaged_Usage; };
template <> struct tag_for_axis<dim::DimensionAxis::Effect>         { using type = FixyNotEngaged_Effect; };
template <> struct tag_for_axis<dim::DimensionAxis::Security>       { using type = FixyNotEngaged_Security; };
template <> struct tag_for_axis<dim::DimensionAxis::Protocol>       { using type = FixyNotEngaged_Protocol; };
template <> struct tag_for_axis<dim::DimensionAxis::Lifetime>       { using type = FixyNotEngaged_Lifetime; };
template <> struct tag_for_axis<dim::DimensionAxis::Provenance>     { using type = FixyNotEngaged_Provenance; };
template <> struct tag_for_axis<dim::DimensionAxis::Trust>          { using type = FixyNotEngaged_Trust; };
template <> struct tag_for_axis<dim::DimensionAxis::Representation> { using type = FixyNotEngaged_Representation; };
template <> struct tag_for_axis<dim::DimensionAxis::Observability>  { using type = FixyNotEngaged_Observability; };
template <> struct tag_for_axis<dim::DimensionAxis::Complexity>     { using type = FixyNotEngaged_Complexity; };
template <> struct tag_for_axis<dim::DimensionAxis::Precision>      { using type = FixyNotEngaged_Precision; };
template <> struct tag_for_axis<dim::DimensionAxis::Space>          { using type = FixyNotEngaged_Space; };
template <> struct tag_for_axis<dim::DimensionAxis::Overflow>       { using type = FixyNotEngaged_Overflow; };
template <> struct tag_for_axis<dim::DimensionAxis::Mutation>       { using type = FixyNotEngaged_Mutation; };
template <> struct tag_for_axis<dim::DimensionAxis::Reentrancy>     { using type = FixyNotEngaged_Reentrancy; };
template <> struct tag_for_axis<dim::DimensionAxis::Size>           { using type = FixyNotEngaged_Size; };
template <> struct tag_for_axis<dim::DimensionAxis::Version>        { using type = FixyNotEngaged_Version; };
template <> struct tag_for_axis<dim::DimensionAxis::Staleness>      { using type = FixyNotEngaged_Staleness; };

template <dim::DimensionAxis D>
using tag_for_axis_t = typename tag_for_axis<D>::type;

}  // namespace diag

// ═════════════════════════════════════════════════════════════════════
// ── EngagedFor<D, Grants...> — per-axis engagement check ───────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff at least one grant in `Grants...` has
// `grant::which_dim_v<G> == D`.  Used by `AllDimsEngaged` to fold
// over every DimensionAxis enumerator.

namespace detail::engagement {

template <dim::DimensionAxis D, typename... Grants>
[[nodiscard]] consteval bool engaged_for() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return ((grant::IsGrantTag_v<Grants>
                 && grant::which_dim_v<Grants> == D) || ...);
    }
}

// ─── all_grants_well_formed — every Grant must satisfy IsGrantTag ──
//
// Defends against:
//   1. User-defined non-grant types in the pack
//   2. Inheriting `grant_base` without being final (subclass attempt)
// Both are caught at the structural concept level.

template <typename... Grants>
[[nodiscard]] consteval bool all_grants_well_formed() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return true;
    } else {
        return (grant::IsGrantTag_v<Grants> && ...);
    }
}

// ─── first_missing_axis — diagnostic helper ────────────────────────
//
// Returns the DimensionAxis whose engagement is missing, or a
// sentinel (0xFF) if every axis is engaged.  Used by IsAccepted's
// failure tag selector.

template <typename... Grants>
[[nodiscard]] consteval dim::DimensionAxis first_missing_axis() noexcept {
    constexpr std::size_t kSentinel = 0xFFu;
    std::size_t result = kSentinel;
    static constexpr auto fm_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : fm_axes) {
        constexpr auto axis_v = [:en:];
        constexpr bool ok = engaged_for<axis_v, Grants...>();
        if (!ok && result == kSentinel) {
            result = static_cast<std::size_t>(axis_v);
        }
    }
#pragma GCC diagnostic pop
    return static_cast<dim::DimensionAxis>(result);
}

template <typename... Grants>
[[nodiscard]] consteval bool every_axis_engaged() noexcept {
    bool ok = true;
    static constexpr auto ea_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : ea_axes) {
        constexpr auto axis_v = [:en:];
        if (!engaged_for<axis_v, Grants...>()) {
            ok = false;
        }
    }
#pragma GCC diagnostic pop
    return ok;
}

// ─── count_engagements_for — per-axis engagement multiplicity ──────
//
// FIXY-AUDIT-A3: silent redundant grants on the same axis hide
// authorial intent ("did I mean to engage twice?") and bypass any
// future tag-vs-tag disagreement check.  We need an explicit duplicate
// count so callers cannot accidentally over-engage an axis.

template <dim::DimensionAxis D, typename... Grants>
[[nodiscard]] consteval std::size_t count_engagements_for() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return 0;
    } else {
        return ((grant::IsGrantTag_v<Grants>
                 && grant::which_dim_v<Grants> == D ? 1u : 0u) + ...);
    }
}

template <typename... Grants>
[[nodiscard]] consteval bool every_axis_engaged_at_most_once() noexcept {
    bool ok = true;
    static constexpr auto uniq_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : uniq_axes) {
        constexpr auto axis_v = [:en:];
        if (count_engagements_for<axis_v, Grants...>() > 1u) {
            ok = false;
        }
    }
#pragma GCC diagnostic pop
    return ok;
}

}  // namespace detail::engagement

// ─── EngagedFor<D, Grants...> concept ──────────────────────────────

template <dim::DimensionAxis D, typename... Grants>
concept EngagedFor = detail::engagement::engaged_for<D, Grants...>();

// ─── AllDimsEngaged<Grants...> concept ─────────────────────────────

template <typename... Grants>
concept AllDimsEngaged = detail::engagement::every_axis_engaged<Grants...>();

// ─── AllGrantsWellFormed<Grants...> concept ────────────────────────

template <typename... Grants>
concept AllGrantsWellFormed =
    detail::engagement::all_grants_well_formed<Grants...>();

// ─── UniqueEngagementPerAxis<Grants...> concept ────────────────────
//
// FIXY-AUDIT-A3.  Per-axis engagement count must be ≤ 1.  Duplicates
// (same axis engaged by multiple grants — even by identical strict
// markers) signal author confusion and silently lose information
// under the resolver's "first matching grant wins" rule.  Reject
// at the gate.
//
// Side-benefit: covers FIXY-AUDIT-A7 (ban explicit user-spelling of
// `accept_default_strict_for<Type>`).  The wrapper injects the Type
// marker implicitly; a user that also writes it explicitly produces
// a duplicate on the Type axis, which fires UniqueEngagementPerAxis.

template <typename... Grants>
concept UniqueEngagementPerAxis =
    detail::engagement::every_axis_engaged_at_most_once<Grants...>();

// ═════════════════════════════════════════════════════════════════════
// ── IsAcceptedGrants<Grants...> — the engagement gate ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff:
//   (a) every Grant satisfies `IsGrantTag` (well-formed),
//   (b) every DimensionAxis is engaged by at least one Grant.
//
// This is the GRANT-LEVEL gate.  The full `IsAccepted<Type, Grants...>`
// concept (defined below) adds Type-axis validation and the
// downstream ValidComposition hookup (active in Phase B once Resolve.h
// is wired).

template <typename... Grants>
concept IsAcceptedGrants =
       AllGrantsWellFormed<Grants...>
    && AllDimsEngaged<Grants...>
    && UniqueEngagementPerAxis<Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── IsAccepted<Type, Grants...> — the full gate ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Adds Type-axis well-formedness on top of IsAcceptedGrants.  The
// Type axis is caller-supplied via fixy::fn<Type, ...>'s first
// template parameter; the strict-default machinery rejects it as a
// caller-supplied axis (no relaxation possible — Type IS the
// parameter).
//
// Phase B hooks in `safety::fn::ValidComposition` once Resolve.h
// projects the grants pack to an Fn<...> instantiation.  Phase A
// only ships the engagement-level gate; the composition gate stays
// dormant until the projection ships.

namespace detail::accept {

template <typename T>
[[nodiscard]] consteval bool type_is_object_or_function() noexcept {
    // Mirror of safety/Fn.h's Type constraints:
    //   - complete object type, non-cv, non-array, non-ref, non-bare-function.
    // Functions are wrapped as pointers or callables before reaching here.
    return  std::is_object_v<T>
        && !std::is_const_v<T>
        && !std::is_volatile_v<T>
        && !std::is_array_v<T>
        && !std::is_reference_v<T>;
}

}  // namespace detail::accept

template <typename Type, typename... Grants>
concept IsAccepted =
       detail::accept::type_is_object_or_function<Type>()
    && IsAcceptedGrants<Grants...>
    && theory::NotInTheoryCorpus<Type, Grants...>;

// ─── IsAccepted_v — variable-template form for static_assert sites ─

template <typename Type, typename... Grants>
inline constexpr bool IsAccepted_v = IsAccepted<Type, Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── Failure inspection — for downstream diagnostic emission ────────
// ═════════════════════════════════════════════════════════════════════
//
// `first_missing_axis_v<Grants...>` returns the DimensionAxis of the
// FIRST unengaged dim (or a sentinel meaning "every axis engaged").
// `first_missing_tag_t<Grants...>` aliases the corresponding
// safety::diag::tag for that axis — usable in a downstream
// static_assert that wants to surface the FOUND-E01 structured
// diagnostic.

template <typename... Grants>
inline constexpr dim::DimensionAxis first_missing_axis_v =
    detail::engagement::first_missing_axis<Grants...>();

template <typename... Grants>
inline constexpr bool every_axis_engaged_v =
    detail::engagement::every_axis_engaged<Grants...>();

// Helper to surface the per-axis tag at the offending site.  Callers
// can write:
//
//   if constexpr (!IsAccepted_v<T, Grants...>) {
//       static_assert(false,
//           "fixy: not accepted — see diagnostic tag below");
//       // tag is fixy::diag::tag_for_axis_t<first_missing_axis_v<Grants...>>
//   }

template <typename... Grants>
    requires (!AllDimsEngaged<Grants...>)
using first_missing_tag_t =
    diag::tag_for_axis_t<first_missing_axis_v<Grants...>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — compile-time witnesses ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// 1. Empty pack rejects (no dims engaged).
// 2. Accept-all-strict pack accepts.
// 3. Single-relaxation pack rejects (other 19 dims unengaged).
// 4. Replacing one accept-strict with a relaxation on the same axis
//    still accepts (relaxation IS engagement).
// 5. Replacing one accept-strict with a relaxation on a DIFFERENT
//    axis rejects (the original axis becomes unengaged again).

namespace detail::reject_self_test {

template <dim::DimensionAxis D>
using strict = grant::accept_default_strict_for<D>;

// All 20 axes accepted-strict.
using AllStrictPack = std::tuple<
    strict<dim::DimensionAxis::Type>,
    strict<dim::DimensionAxis::Refinement>,
    strict<dim::DimensionAxis::Usage>,
    strict<dim::DimensionAxis::Effect>,
    strict<dim::DimensionAxis::Security>,
    strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>,
    strict<dim::DimensionAxis::Provenance>,
    strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>,
    strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>,
    strict<dim::DimensionAxis::Precision>,
    strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>,
    strict<dim::DimensionAxis::Mutation>,
    strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>,
    strict<dim::DimensionAxis::Version>,
    strict<dim::DimensionAxis::Staleness>>;

// Apply Grants pack from a tuple to a template — helper.
template <template <typename...> class Tmpl, typename Tuple>
struct apply_tuple;
template <template <typename...> class Tmpl, typename... Ts>
struct apply_tuple<Tmpl, std::tuple<Ts...>> { using type = Tmpl<Ts...>; };

template <typename T, typename Tuple>
inline constexpr bool accepts_pack_v = []() {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return IsAccepted<T, Ts...>;
    }(static_cast<Tuple*>(nullptr));
}();

// 1. Empty pack rejects.
static_assert(!IsAccepted<int>,
    "Empty Grants pack must reject (no dims engaged).");
// Witnessed at the dim level too:
static_assert(!IsAcceptedGrants<>,
    "IsAcceptedGrants<> must reject the empty pack.");

// 2. All-strict pack accepts.
static_assert(accepts_pack_v<int, AllStrictPack>,
    "AllStrict pack must accept — every dim has an engagement marker.");

// 3. Single-relaxation pack rejects (19 dims still unengaged).
static_assert(!IsAccepted<int, grant::copy>,
    "Single Usage relaxation must reject — 19 other dims unengaged.");

// 4. Replacing Usage's accept-strict with `grant::copy` still accepts.
using CopyForUsagePack = std::tuple<
    strict<dim::DimensionAxis::Type>,
    strict<dim::DimensionAxis::Refinement>,
    grant::copy,  // <-- relaxation replaces accept-strict on Usage
    strict<dim::DimensionAxis::Effect>,
    strict<dim::DimensionAxis::Security>,
    strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>,
    strict<dim::DimensionAxis::Provenance>,
    strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>,
    strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>,
    strict<dim::DimensionAxis::Precision>,
    strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>,
    strict<dim::DimensionAxis::Mutation>,
    strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>,
    strict<dim::DimensionAxis::Version>,
    strict<dim::DimensionAxis::Staleness>>;

static_assert(accepts_pack_v<int, CopyForUsagePack>,
    "Replacing accept-strict<Usage> with `grant::copy` must still "
    "accept — `copy` engages the Usage axis.");

// 5. Removing one accept-strict (without replacement) rejects.
//    AllStrictPack minus Effect's accept-strict.
using MinusEffectPack = std::tuple<
    strict<dim::DimensionAxis::Type>,
    strict<dim::DimensionAxis::Refinement>,
    strict<dim::DimensionAxis::Usage>,
    // Effect removed
    strict<dim::DimensionAxis::Security>,
    strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>,
    strict<dim::DimensionAxis::Provenance>,
    strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>,
    strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>,
    strict<dim::DimensionAxis::Precision>,
    strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>,
    strict<dim::DimensionAxis::Mutation>,
    strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>,
    strict<dim::DimensionAxis::Version>,
    strict<dim::DimensionAxis::Staleness>>;

static_assert(!accepts_pack_v<int, MinusEffectPack>,
    "Removing accept-strict<Effect> without replacement must reject.");

// Type-axis well-formedness:
static_assert(!IsAccepted<void, grant::accept_default_strict_for<dim::DimensionAxis::Usage>>,
    "Type=void must reject (Fn requires complete object type).");
static_assert(!IsAccepted<int&>,
    "Type=int& must reject (no reference types).");
static_assert(!IsAccepted<int[4]>,
    "Type=int[4] must reject (array decay would corrupt copy ctor).");
static_assert(!IsAccepted<const int>,
    "Type=const int must reject (silent deletion of assignment).");

// Failure inspection — first_missing_axis_v returns the FIRST
// unengaged dim, which for an empty pack is Type (index 0).
static_assert(first_missing_axis_v<> == dim::DimensionAxis::Type,
    "An empty Grants pack reports Type as the first missing axis.");

// Skipping just Refinement reports Refinement.
using MinusRefinementPack = std::tuple<
    strict<dim::DimensionAxis::Type>,
    // Refinement removed
    strict<dim::DimensionAxis::Usage>,
    strict<dim::DimensionAxis::Effect>,
    strict<dim::DimensionAxis::Security>,
    strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>,
    strict<dim::DimensionAxis::Provenance>,
    strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>,
    strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>,
    strict<dim::DimensionAxis::Precision>,
    strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>,
    strict<dim::DimensionAxis::Mutation>,
    strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>,
    strict<dim::DimensionAxis::Version>,
    strict<dim::DimensionAxis::Staleness>>;

inline constexpr dim::DimensionAxis first_missing_for_minus_refinement = []() consteval {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return first_missing_axis_v<Ts...>;
    }(static_cast<MinusRefinementPack*>(nullptr));
}();

static_assert(first_missing_for_minus_refinement == dim::DimensionAxis::Refinement,
    "first_missing_axis_v points at Refinement when only that axis "
    "is omitted from an otherwise full strict pack.");

}  // namespace detail::reject_self_test

}  // namespace crucible::fixy
