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

// ─── FixyDuplicate_<Axis> tag family — fixy-H-02 ───────────────────
//
// Companion to FixyNotEngaged_<Axis>.  Fires when an axis is engaged
// by MORE THAN ONE grant in the pack (UniqueEngagementPerAxis
// rejection path).  Distinct from FixyNotEngaged_<Axis> so the
// diagnostic surface tells the author which failure mode tripped:
// missing-axis ⇒ "add a grant"; duplicate-axis ⇒ "remove a grant".
// Per fixy-H-02: the single static_assert in fixy::fn<>'s class body
// was misleading — it always said "axis not engaged" even when the
// real failure was UniqueEngagementPerAxis (or AllGrantsWellFormed).

#define CRUCIBLE_FIXY_DUPLICATE_TAG(AxisName, AxisDesc)                            \
    struct FixyDuplicate_##AxisName final : ::crucible::safety::diag::tag_base {  \
        static constexpr ::std::string_view name =                                  \
            "FixyDuplicate_" #AxisName;                                             \
        static constexpr ::std::string_view description =                           \
            "Dimension '" #AxisName "' (" AxisDesc ") is engaged by MORE THAN "     \
            "ONE grant in the binding's Grants pack.  Per misc/16_05_2026_"         \
            "fixy.md §3, every fixy:: binding MUST engage with every dimension "    \
            "EXACTLY ONCE — silent redundant grants hide authorial intent and "     \
            "bypass any future tag-vs-tag disagreement check (FIXY-AUDIT-A3).";     \
        static constexpr ::std::string_view remediation =                           \
            "Remove all but one of the grants engaging '" #AxisName "' from the "   \
            "Grants pack.  Note: explicitly writing `grant::accept_default_"        \
            "strict_for<dim::DimensionAxis::Type>` is FORBIDDEN because "           \
            "fixy::fn implicitly engages Type (FIXY-AUDIT-A7) — that explicit "     \
            "Type marker would trigger a duplicate on the Type axis.  Drop the "    \
            "explicit Type marker.";                                                \
    };                                                                              \
    static_assert(::crucible::safety::diag::is_diagnostic_class_v<                  \
                      FixyDuplicate_##AxisName>,                                    \
        "FixyDuplicate_" #AxisName " must inherit safety::diag::tag_base.")

CRUCIBLE_FIXY_DUPLICATE_TAG(Type,           "the function type itself");
CRUCIBLE_FIXY_DUPLICATE_TAG(Refinement,     "value-level predicate");
CRUCIBLE_FIXY_DUPLICATE_TAG(Usage,          "linear / affine / copy / ghost");
CRUCIBLE_FIXY_DUPLICATE_TAG(Effect,         "Bg / IO / Alloc / Block / etc.");
CRUCIBLE_FIXY_DUPLICATE_TAG(Security,       "Classified / Public / Secret");
CRUCIBLE_FIXY_DUPLICATE_TAG(Protocol,       "session-type / state machine");
CRUCIBLE_FIXY_DUPLICATE_TAG(Lifetime,       "Static / In<Region>");
CRUCIBLE_FIXY_DUPLICATE_TAG(Provenance,     "source provenance tag");
CRUCIBLE_FIXY_DUPLICATE_TAG(Trust,          "Verified / Tested / Assumed");
CRUCIBLE_FIXY_DUPLICATE_TAG(Representation, "memory layout discipline");
CRUCIBLE_FIXY_DUPLICATE_TAG(Observability,  "derived from Effect — accept only");
CRUCIBLE_FIXY_DUPLICATE_TAG(Complexity,     "O(1) / O(N) / O(N²) classification");
CRUCIBLE_FIXY_DUPLICATE_TAG(Precision,      "FP error bound");
CRUCIBLE_FIXY_DUPLICATE_TAG(Space,          "allocation footprint bound");
CRUCIBLE_FIXY_DUPLICATE_TAG(Overflow,       "Trap / Wrap / Saturate / Widen");
CRUCIBLE_FIXY_DUPLICATE_TAG(Mutation,       "Immutable / Append / Monotonic / Mutable");
CRUCIBLE_FIXY_DUPLICATE_TAG(Reentrancy,     "NonReentrant / Reentrant / Coroutine");
CRUCIBLE_FIXY_DUPLICATE_TAG(Size,           "codata observation depth");
CRUCIBLE_FIXY_DUPLICATE_TAG(Version,        "schema version number");
CRUCIBLE_FIXY_DUPLICATE_TAG(Staleness,      "freshness bound τ");

#undef CRUCIBLE_FIXY_DUPLICATE_TAG

// ─── FixyMalformedGrant — AllGrantsWellFormed rejection tag ────────
//
// fixy-H-02: single tag covering the "Grants pack contains a non-grant
// type" failure mode (a Grant that fails fixy::grant::IsGrantTag —
// either non-final, doesn't inherit grant_base, or is a foreign type
// reaching the gate by accident).  No per-axis projection because the
// failure is at the PACK level (the malformed entry is not a grant at
// all, so it has no axis to project onto).
struct FixyMalformedGrant final : ::crucible::safety::diag::tag_base {
    static constexpr ::std::string_view name = "FixyMalformedGrant";
    static constexpr ::std::string_view description =
        "The Grants pack contains a type that does NOT satisfy "
        "fixy::grant::IsGrantTag (the entry is not final-class, does not "
        "inherit fixy::grant::grant_base, or is a non-grant type entirely "
        "— e.g. a raw `int`, a user-defined struct, or a substrate type). "
        "This defends against trait-spec injection: only grants from the "
        "fixy::grant::* namespace OR `fixy::grant::accept_default_strict_"
        "for<...>` instantiations may engage the discipline axes.";
    static constexpr ::std::string_view remediation =
        "Audit the Grants pack — every entry must be either (a) a tag "
        "from the fixy::grant::* catalog (e.g. `grant::copy`, `grant::"
        "ghost`, `grant::with<effects::Effect::IO>`, "
        "`grant::declassify<Policy>`), or (b) an explicit acceptance "
        "marker `grant::accept_default_strict_for<dim::DimensionAxis::"
        "<Axis>>` (except the Type axis — see FIXY-AUDIT-A7).  Substrate "
        "types reaching this gate are typically the result of a "
        "copy-paste error, a misspelled grant name, or a typo in the "
        "`fixy::fn<Type, ...>` parameter list.";
};
static_assert(::crucible::safety::diag::is_diagnostic_class_v<FixyMalformedGrant>,
    "FixyMalformedGrant must inherit safety::diag::tag_base.");

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

// ─── dup_tag_for_axis<D> — DimensionAxis → duplicate-engagement tag ─
//
// fixy-H-02: companion lookup to tag_for_axis<D>.  Surfaces the
// FixyDuplicate_<Axis> diagnostic tag when UniqueEngagementPerAxis
// rejects.  Same 20-entry partial specialization map, separate type
// family because the failure semantics (missing-vs-duplicate) drives
// different remediation messages.

template <dim::DimensionAxis D> struct dup_tag_for_axis;

template <> struct dup_tag_for_axis<dim::DimensionAxis::Type>           { using type = FixyDuplicate_Type; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Refinement>     { using type = FixyDuplicate_Refinement; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Usage>          { using type = FixyDuplicate_Usage; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Effect>         { using type = FixyDuplicate_Effect; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Security>       { using type = FixyDuplicate_Security; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Protocol>       { using type = FixyDuplicate_Protocol; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Lifetime>       { using type = FixyDuplicate_Lifetime; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Provenance>     { using type = FixyDuplicate_Provenance; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Trust>          { using type = FixyDuplicate_Trust; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Representation> { using type = FixyDuplicate_Representation; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Observability>  { using type = FixyDuplicate_Observability; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Complexity>     { using type = FixyDuplicate_Complexity; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Precision>      { using type = FixyDuplicate_Precision; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Space>          { using type = FixyDuplicate_Space; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Overflow>       { using type = FixyDuplicate_Overflow; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Mutation>       { using type = FixyDuplicate_Mutation; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Reentrancy>     { using type = FixyDuplicate_Reentrancy; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Size>           { using type = FixyDuplicate_Size; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Version>        { using type = FixyDuplicate_Version; };
template <> struct dup_tag_for_axis<dim::DimensionAxis::Staleness>      { using type = FixyDuplicate_Staleness; };

template <dim::DimensionAxis D>
using dup_tag_for_axis_t = typename dup_tag_for_axis<D>::type;

// ═══════════════════════════════════════════════════════════════════
// ── FixyCatalog — closed enumeration of fixy diagnostic tags ───────
// ═══════════════════════════════════════════════════════════════════
//
// FIXY-AUDIT-C8 reconciliation surface.  Mirrors the substrate's
// `safety::diag::Catalog` pattern (one tuple, APPEND-ONLY, indexed in
// stable order) but lives entirely on the fixy side because per
// `safety/Diagnostic.h:96-99` user-defined tags MUST NOT enter the
// substrate's closed Category/Catalog — those are reserved for the
// foundation's 28 axis violations.  Fixy's twenty `FixyNotEngaged_*`
// tags still need to be enumerable as a closed set so callers can:
//
//   (a) discriminate fixy tags from substrate tags via
//       `is_fixy_diag_v<T>`;
//   (b) iterate the fixy tag set at compile time for diagnostic
//       formatters, federation cache hashing, and reflection-driven
//       UX layers;
//   (c) close the bijection `tag_for_axis_t<D>` already opens by
//       providing the reverse `axis_for_tag_v<Tag>`.
//
// The runtime category for fixy diagnostics is `dim::DimensionAxis`
// itself — every fixy diagnostic IS a "this axis went wrong" claim.
// We deliberately do NOT mint a separate Category-style enum: that
// would duplicate the canonical axis universe and force a second
// APPEND-ONLY discipline that could drift.
//
// Discipline (mirrors safety::diag::Catalog):
//   1. APPEND-ONLY in DimensionAxis enumerator order.
//   2. Adding a new DimensionAxis enumerator requires (a) the matching
//      `FixyNotEngaged_<Axis>` tag via `CRUCIBLE_FIXY_NOT_ENGAGED_TAG`
//      above, (b) the matching `tag_for_axis<>` specialization, (c)
//      appending here, and (d) the matching `axis_for_tag<>`
//      specialization below.  The bijection self-test fires if any
//      step is skipped.

using FixyCatalog = ::std::tuple<
    FixyNotEngaged_Type,            //  0
    FixyNotEngaged_Refinement,      //  1
    FixyNotEngaged_Usage,           //  2
    FixyNotEngaged_Effect,          //  3
    FixyNotEngaged_Security,        //  4
    FixyNotEngaged_Protocol,        //  5
    FixyNotEngaged_Lifetime,        //  6
    FixyNotEngaged_Provenance,      //  7
    FixyNotEngaged_Trust,           //  8
    FixyNotEngaged_Representation,  //  9
    FixyNotEngaged_Observability,   // 10
    FixyNotEngaged_Complexity,      // 11
    FixyNotEngaged_Precision,       // 12
    FixyNotEngaged_Space,           // 13
    FixyNotEngaged_Overflow,        // 14
    FixyNotEngaged_Mutation,        // 15
    FixyNotEngaged_Reentrancy,      // 16
    FixyNotEngaged_Size,            // 17
    FixyNotEngaged_Version,         // 18
    FixyNotEngaged_Staleness        // 19
>;

inline constexpr ::std::size_t fixy_catalog_size =
    ::std::tuple_size_v<FixyCatalog>;

// ─── is_fixy_diag_v — fixy-tag discriminator ───────────────────────
//
// True iff T appears in FixyCatalog.  Lets generic diagnostic
// machinery route fixy tags through the fixy-side surface and leave
// substrate Catalog tags to the substrate (the two enumerations are
// disjoint by design per FOUND-E01).
//
// Substrate `safety::diag::HotPathViolation` and friends MUST return
// false here.

namespace detail::fixy_catalog {

template <typename T, typename Tuple>
struct in_tuple_impl;

template <typename T, typename... Us>
struct in_tuple_impl<T, ::std::tuple<Us...>>
    : ::std::bool_constant<(::std::is_same_v<T, Us> || ...)> {};

}  // namespace detail::fixy_catalog

template <typename T>
inline constexpr bool is_fixy_diag_v =
    detail::fixy_catalog::in_tuple_impl<T, FixyCatalog>::value;

// ─── axis_for_tag — reverse lookup (Tag → DimensionAxis) ──────────
//
// Closes the bijection with `tag_for_axis<D>` above.  Specialized for
// every entry in FixyCatalog.  The bijection self-test (below) walks
// every catalog index and asserts the round-trip holds.

template <typename Tag> struct axis_for_tag;  // primary undefined

template <> struct axis_for_tag<FixyNotEngaged_Type>           { static constexpr auto value = dim::DimensionAxis::Type; };
template <> struct axis_for_tag<FixyNotEngaged_Refinement>     { static constexpr auto value = dim::DimensionAxis::Refinement; };
template <> struct axis_for_tag<FixyNotEngaged_Usage>          { static constexpr auto value = dim::DimensionAxis::Usage; };
template <> struct axis_for_tag<FixyNotEngaged_Effect>         { static constexpr auto value = dim::DimensionAxis::Effect; };
template <> struct axis_for_tag<FixyNotEngaged_Security>       { static constexpr auto value = dim::DimensionAxis::Security; };
template <> struct axis_for_tag<FixyNotEngaged_Protocol>       { static constexpr auto value = dim::DimensionAxis::Protocol; };
template <> struct axis_for_tag<FixyNotEngaged_Lifetime>       { static constexpr auto value = dim::DimensionAxis::Lifetime; };
template <> struct axis_for_tag<FixyNotEngaged_Provenance>     { static constexpr auto value = dim::DimensionAxis::Provenance; };
template <> struct axis_for_tag<FixyNotEngaged_Trust>          { static constexpr auto value = dim::DimensionAxis::Trust; };
template <> struct axis_for_tag<FixyNotEngaged_Representation> { static constexpr auto value = dim::DimensionAxis::Representation; };
template <> struct axis_for_tag<FixyNotEngaged_Observability>  { static constexpr auto value = dim::DimensionAxis::Observability; };
template <> struct axis_for_tag<FixyNotEngaged_Complexity>     { static constexpr auto value = dim::DimensionAxis::Complexity; };
template <> struct axis_for_tag<FixyNotEngaged_Precision>      { static constexpr auto value = dim::DimensionAxis::Precision; };
template <> struct axis_for_tag<FixyNotEngaged_Space>          { static constexpr auto value = dim::DimensionAxis::Space; };
template <> struct axis_for_tag<FixyNotEngaged_Overflow>       { static constexpr auto value = dim::DimensionAxis::Overflow; };
template <> struct axis_for_tag<FixyNotEngaged_Mutation>       { static constexpr auto value = dim::DimensionAxis::Mutation; };
template <> struct axis_for_tag<FixyNotEngaged_Reentrancy>     { static constexpr auto value = dim::DimensionAxis::Reentrancy; };
template <> struct axis_for_tag<FixyNotEngaged_Size>           { static constexpr auto value = dim::DimensionAxis::Size; };
template <> struct axis_for_tag<FixyNotEngaged_Version>        { static constexpr auto value = dim::DimensionAxis::Version; };
template <> struct axis_for_tag<FixyNotEngaged_Staleness>      { static constexpr auto value = dim::DimensionAxis::Staleness; };

template <typename Tag>
inline constexpr dim::DimensionAxis axis_for_tag_v = axis_for_tag<Tag>::value;

// ═══════════════════════════════════════════════════════════════════
// ── Bijection self-test — FixyCatalog ↔ DimensionAxis ──────────────
// ═══════════════════════════════════════════════════════════════════

namespace detail::fixy_catalog {

// FixyCatalog cardinality must match DimensionAxis cardinality.  If a
// new DimensionAxis enumerator is added without the matching tag /
// catalog entry / reverse lookup, this fires first.
inline constexpr ::std::size_t kDimAxisCount = []() consteval {
    return ::std::meta::enumerators_of(
        ^^::crucible::safety::DimensionAxis).size();
}();

static_assert(fixy_catalog_size == kDimAxisCount,
    "FixyCatalog cardinality drifted from DimensionAxis cardinality. "
    "Adding a new DimensionAxis enumerator requires (a) adding the "
    "matching FixyNotEngaged_<Axis> tag via the macro above, (b) "
    "appending to FixyCatalog in DimensionAxis order, (c) the matching "
    "tag_for_axis specialization, and (d) the matching axis_for_tag "
    "specialization.");

// Round-trip: for every catalog index I, both lookups agree and the
// catalog ordering follows DimensionAxis value ordering.
template <::std::size_t I>
[[nodiscard]] consteval bool catalog_bijection_at() noexcept {
    using TagAtI = ::std::tuple_element_t<I, FixyCatalog>;
    constexpr auto axis_v = axis_for_tag_v<TagAtI>;
    using TagViaForward = tag_for_axis_t<axis_v>;
    return ::std::is_same_v<TagAtI, TagViaForward>
        && static_cast<::std::size_t>(axis_v) == I;
}

template <::std::size_t... Is>
[[nodiscard]] consteval bool catalog_bijection_holds(
    ::std::index_sequence<Is...>) noexcept
{
    return (catalog_bijection_at<Is>() && ...);
}

static_assert(
    catalog_bijection_holds(
        ::std::make_index_sequence<fixy_catalog_size>{}),
    "FixyCatalog ordering drifted from DimensionAxis value ordering. "
    "Each FixyCatalog entry at index I must satisfy "
    "tag_for_axis_t<static_cast<DimensionAxis>(I)> == entry AND "
    "axis_for_tag_v<entry> == static_cast<DimensionAxis>(I).");

}  // namespace detail::fixy_catalog

// ─── is_fixy_diag_v / axis_for_tag_v compile-time witnesses ───────
//
// These ride next to the catalog definition so a regression in the
// substrate-vs-fixy discrimination fires at the definition site
// instead of at a distant call site.

static_assert(is_fixy_diag_v<FixyNotEngaged_Type>,
    "FixyNotEngaged_Type must be recognized as a fixy diagnostic.");
static_assert(is_fixy_diag_v<FixyNotEngaged_Staleness>,
    "FixyNotEngaged_Staleness must be recognized as a fixy diagnostic.");
static_assert(!is_fixy_diag_v<int>,
    "Plain primitive types must not register as fixy diagnostics.");
static_assert(!is_fixy_diag_v<::crucible::safety::diag::tag_base>,
    "tag_base itself is not a catalog entry — only concrete subclasses "
    "appear in FixyCatalog.");
static_assert(!is_fixy_diag_v<::crucible::safety::diag::HotPathViolation>,
    "Substrate diagnostic tags MUST NOT register as fixy diagnostics. "
    "The substrate Catalog and FixyCatalog are disjoint by design "
    "(FOUND-E01 + FIXY-AUDIT-C8 reconciliation).");
static_assert(!is_fixy_diag_v<::crucible::safety::diag::EffectRowMismatch>,
    "Substrate diagnostic tags MUST NOT register as fixy diagnostics.");

static_assert(axis_for_tag_v<FixyNotEngaged_Type>
              == dim::DimensionAxis::Type,
    "axis_for_tag must invert tag_for_axis at the Type axis.");
static_assert(axis_for_tag_v<FixyNotEngaged_Staleness>
              == dim::DimensionAxis::Staleness,
    "axis_for_tag must invert tag_for_axis at the Staleness axis.");

}  // namespace diag

// ═════════════════════════════════════════════════════════════════════
// ── engaged_for<D, Grants...>() — per-axis engagement helpers ──────
// ═════════════════════════════════════════════════════════════════════
//
// `detail::engagement::engaged_for<D, Grants...>()` is true iff at
// least one grant in `Grants...` has `grant::which_dim_v<G> == D`.
// Folded over every DimensionAxis enumerator by `every_axis_engaged`,
// `first_missing_axis`, and `first_duplicate_axis`.
//
// Concept form: AllDimsEngaged is derived from the reflection fold
// directly — no per-axis public concept ships (fixy-H-04).

namespace detail::engagement {

// FIXY-AUDIT-CR-08: per-grant engagement probe.
//
// The fold form
//
//     ((grant::IsGrantTag_v<G> && grant::which_dim_v<G> == D) || ...)
//
// substitutes `grant::which_dim_v<G>` for every G in the pack BEFORE
// the consteval `&&` short-circuit runs.  `which_dim`'s primary
// template is intentionally left undefined (per-tag specialization
// only), so a non-grant G in the pack — e.g. a user-defined struct,
// `int`, or anything reaching `engaged_for` before `IsAccepted` has
// gated it — produces a hard substitution error inside the fold
// rather than a clean rejection at the IsAccepted boundary.
//
// This was exactly the failure mode FIXY-AUDIT-A1 fixed for
// `find_grant_impl` (Fn.h:338-347): there, the fix was constraint
// partial-ordering across two partial specializations.  Folds cannot
// use that mechanism, so we extract a per-grant helper and use
// `if constexpr` to gate the `which_dim_v` lookup.
template <dim::DimensionAxis D, typename G>
[[nodiscard]] consteval bool engages_dim_one() noexcept {
    if constexpr (grant::IsGrantTag_v<G>) {
        return grant::which_dim_v<G> == D;
    } else {
        return false;
    }
}

template <dim::DimensionAxis D, typename... Grants>
[[nodiscard]] consteval bool engaged_for() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return (engages_dim_one<D, Grants>() || ...);
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

// FIXY-AUDIT-CR-08: same eager-substitution hazard as `engaged_for`
// above — `which_dim_v<G>` would be instantiated for every G in the
// pack before the consteval `?:` runs.  Route through the gated
// `engages_dim_one<D, G>()` helper instead.
template <dim::DimensionAxis D, typename... Grants>
[[nodiscard]] consteval std::size_t count_engagements_for() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return 0;
    } else {
        return ((engages_dim_one<D, Grants>() ? 1u : 0u) + ...);
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

// ─── first_duplicate_axis — diagnostic helper (fixy-H-02) ──────────
//
// Returns the FIRST DimensionAxis whose engagement count exceeds 1,
// or the sentinel (0xFF) if every axis is engaged at most once.
// Mirror of first_missing_axis: same DimensionAxis ordering, same
// sentinel convention, same template-for-over-reflected-enumerators
// scan.  Used by fixy::fn<>'s branched static_assert to surface a
// duplicate-engagement diagnostic distinct from the missing-axis
// case.

template <typename... Grants>
[[nodiscard]] consteval dim::DimensionAxis first_duplicate_axis() noexcept {
    constexpr std::size_t kSentinel = 0xFFu;
    std::size_t result = kSentinel;
    static constexpr auto fd_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : fd_axes) {
        constexpr auto axis_v = [:en:];
        constexpr std::size_t cnt =
            count_engagements_for<axis_v, Grants...>();
        if (cnt > 1u && result == kSentinel) {
            result = static_cast<std::size_t>(axis_v);
        }
    }
#pragma GCC diagnostic pop
    return static_cast<dim::DimensionAxis>(result);
}

}  // namespace detail::engagement

// fixy-H-04: the public `EngagedFor<D, Grants...>` concept was removed
// (defined here through 2026-05-18, never consumed by any caller).  The
// per-axis engagement check lives in `detail::engagement::engaged_for<>()`
// and is folded over reflection by `every_axis_engaged<>` /
// `first_missing_axis<>` / `first_duplicate_axis<>` — `AllDimsEngaged`
// derives from the reflection fold directly, not from a 20-fold
// conjunction of `EngagedFor` instantiations.  Reintroduce if (and only
// if) a real consumer needs single-axis constraint expression.

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

// fixy-H-05: Canonical home for the implicit Type-axis engagement
// marker.  Defined here (not in Fn.h's detail::resolve) so the
// wrapper-discipline `IsAccepted` concept below can reference it
// without taking a dependency on Fn.h.  Fn.h's
// `detail::resolve::ImplicitTypeMarker` is an alias of this canonical
// definition.
using ImplicitTypeMarker =
    grant::accept_default_strict_for<dim::DimensionAxis::Type>;

}  // namespace detail::accept

// ═════════════════════════════════════════════════════════════════════
// ── IsAcceptedDirect<Type, Grants...> — low-level acceptance gate ──
// ═════════════════════════════════════════════════════════════════════
//
// THE LOW-LEVEL FORM.  Callers MUST include the Type-axis engagement
// marker (`grant::accept_default_strict_for<dim::DimensionAxis::Type>`)
// in the `Grants...` pack.  This is the concept the wrapper's class
// body and the H-02/H-03 tier static_assert chain consume internally;
// production fixy::fn user code does NOT call this directly because
// FIXY-AUDIT-A7 forbids user-spelling of the Type marker.
//
// For production-style "user passes grants for the 19 non-Type axes
// and the wrapper supplies the Type marker for you" discipline, use
// `IsAccepted<Type, Grants...>` below (renamed from `IsAcceptedFn`
// per fixy-H-05; the simpler name denotes the safer, marker-injecting
// form).
//
// The fixy-H-05 rename eliminates the public-name footgun: previously
// a user grep-ing for "IsAccepted" found the LOW-LEVEL form (named
// `IsAccepted`) and was tempted to call it directly with raw grants,
// then hit a confusing "Type axis not engaged" failure.  After H-05,
// the simpler name `IsAccepted` is the user-facing form that injects
// the marker; the qualified `IsAcceptedDirect` name signals "you must
// know what you're doing — this expects a complete pack."

template <typename Type, typename... Grants>
concept IsAcceptedDirect =
       detail::accept::type_is_object_or_function<Type>()
    && IsAcceptedGrants<Grants...>
    && theory::NotInTheoryCorpus<Type, Grants...>;

// ─── IsAcceptedDirect_v — variable-template form ───────────────────

template <typename Type, typename... Grants>
inline constexpr bool IsAcceptedDirect_v = IsAcceptedDirect<Type, Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── IsAccepted<Type, Grants...> — the wrapper-discipline gate ──────
// ═════════════════════════════════════════════════════════════════════
//
// THE USER-FACING FORM.  Auto-injects the implicit Type-axis marker
// so callers pass grants for the 19 non-Type axes only.  This is what
// `fn<>`'s `requires`-clause and `mint_fn`'s gate consume; it is also
// the recommended name for any out-of-tree code writing acceptance
// constraints (per fixy-H-05).
//
// Renamed from `IsAcceptedFn` (which used to live in Fn.h) so the
// simpler public name denotes the safer behavior; the previous public
// `IsAccepted` (the low-level form) is now `IsAcceptedDirect` above.

template <typename Type, typename... Grants>
concept IsAccepted =
    IsAcceptedDirect<Type, detail::accept::ImplicitTypeMarker, Grants...>;

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

// ─── first_duplicate_axis_v / first_duplicate_tag_t (fixy-H-02) ────
//
// Public-surface companions to first_missing_axis_v / first_missing_
// tag_t.  Surface the FIRST axis that is engaged MORE THAN ONCE and
// its matching FixyDuplicate_<Axis> diagnostic tag.  The alias is
// guarded by `!UniqueEngagementPerAxis<Grants...>` so a well-formed
// pack cannot accidentally instantiate it (the sentinel 0xFF would
// cast to an out-of-range DimensionAxis enumerator).

template <typename... Grants>
inline constexpr dim::DimensionAxis first_duplicate_axis_v =
    detail::engagement::first_duplicate_axis<Grants...>();

template <typename... Grants>
    requires (!UniqueEngagementPerAxis<Grants...>)
using first_duplicate_tag_t =
    diag::dup_tag_for_axis_t<first_duplicate_axis_v<Grants...>>;

// ═════════════════════════════════════════════════════════════════════
// ── fixy-H-03 — Surface diagnostic tags in compiler error trail ────
// ═════════════════════════════════════════════════════════════════════
//
// H-02 cited first_missing_axis_v / first_duplicate_axis_v / Fixy*
// tags in static_assert message strings.  But static_assert messages
// are STRING LITERALS — the compiler does not substitute template
// parameters into them.  A user reading the error sees the cite text
// but the SPECIFIC tag class name (e.g. FixyNotEngaged_Effect) never
// appears in the diagnostic context.
//
// H-03 fixes this by instantiating the tag in a templated diagnostic
// helper whose name surfaces in the compiler instantiation chain.
// Each Diagnose<Tag> helper has a static_assert in its primary
// template (fires on any non-void Tag) plus an empty <void>
// specialization (silent OK sentinel).  fixy::fn<> instantiates
// Diagnose<lazy_tag_or_void_t<...>> as a class-body member.  When
// the tier passes, the resolved type is `void` — matches the empty
// spec, no diagnostic.  When the tier fails, the type resolves to
// the real Fixy* tag — fires the inner static_assert AND surfaces
// the tag class name in the compiler's "required from" trail.
//
// Tier chaining: each Diagnose only fires if prior tiers passed
// (avoids cascade diagnostics — a malformed-grant doesn't ALSO
// trigger missing-axis since AllDimsEngaged is meaningless then).

namespace detail::diagnose {

template <typename T>
inline constexpr bool always_false_v = false;

// Lazy partial-spec dispatcher — selects void when Failed=false
// without substituting the (possibly-ill-formed) failure branch.

template <bool Failed, typename... Grants>
struct select_missing_tag { using type = void; };

template <typename... Grants>
struct select_missing_tag<true, Grants...> {
    using type = diag::tag_for_axis_t<
        detail::engagement::first_missing_axis<Grants...>()>;
};

template <bool Failed, typename... Grants>
struct select_duplicate_tag { using type = void; };

template <typename... Grants>
struct select_duplicate_tag<true, Grants...> {
    using type = diag::dup_tag_for_axis_t<
        detail::engagement::first_duplicate_axis<Grants...>()>;
};

}  // namespace detail::diagnose

// Public tag-or-void evaluators (per-tier guarded).
//
// `malformed_grant_or_void_t<Grants...>` resolves to FixyMalformedGrant
// when AllGrantsWellFormed fails; void otherwise.
//
// `missing_tag_or_void_t<Grants...>` resolves to FixyNotEngaged_<Axis>
// when AllGrantsWellFormed PASSED but AllDimsEngaged failed; void
// otherwise (tier-2 catches the malformed-grant case first).
//
// `duplicate_tag_or_void_t<Grants...>` resolves to FixyDuplicate_<Axis>
// when tiers 2+3 PASSED but UniqueEngagementPerAxis failed; void
// otherwise.

template <typename... Grants>
using malformed_grant_or_void_t = std::conditional_t<
    AllGrantsWellFormed<Grants...>,
    void,
    diag::FixyMalformedGrant>;

template <typename... Grants>
using missing_tag_or_void_t = typename detail::diagnose::select_missing_tag<
    AllGrantsWellFormed<Grants...> && !AllDimsEngaged<Grants...>,
    Grants...>::type;

template <typename... Grants>
using duplicate_tag_or_void_t = typename detail::diagnose::select_duplicate_tag<
       AllGrantsWellFormed<Grants...>
    && AllDimsEngaged<Grants...>
    && !UniqueEngagementPerAxis<Grants...>,
    Grants...>::type;

// Diagnose<Tag> helpers — primary template fires static_assert with
// Tag name in the compiler's instantiation context; <void>
// specialization is silent OK.

template <typename Tag>
struct DiagnoseAxisNotEngaged {
    static_assert(detail::diagnose::always_false_v<Tag>,
        "fixy::fn<Type, Grants...>: AllDimsEngaged FAILED — the Tag "
        "template parameter on this DiagnoseAxisNotEngaged<...> "
        "instantiation names the specific FixyNotEngaged_<Axis> "
        "diagnostic tag for the offending axis.  Add the matching "
        "`grant::accept_default_strict_for<dim::DimensionAxis::<Axis>>` "
        "or a per-axis relaxation tag from fixy::grant::*.");
};

template <>
struct DiagnoseAxisNotEngaged<void> {};

template <typename Tag>
struct DiagnoseAxisDuplicate {
    static_assert(detail::diagnose::always_false_v<Tag>,
        "fixy::fn<Type, Grants...>: UniqueEngagementPerAxis FAILED — "
        "the Tag template parameter on this DiagnoseAxisDuplicate<...> "
        "instantiation names the specific FixyDuplicate_<Axis> "
        "diagnostic tag for the duplicated axis.  Remove the redundant "
        "grant(s) for that axis.");
};

template <>
struct DiagnoseAxisDuplicate<void> {};

template <typename Tag>
struct DiagnoseMalformedGrant {
    static_assert(detail::diagnose::always_false_v<Tag>,
        "fixy::fn<Type, Grants...>: AllGrantsWellFormed FAILED — the "
        "Tag template parameter on this DiagnoseMalformedGrant<...> "
        "instantiation names FixyMalformedGrant.  The Grants pack "
        "contains an entry that does NOT satisfy fixy::grant::"
        "IsGrantTag (not final-class, doesn't inherit grant_base, or "
        "is a non-grant type entirely such as `int`).");
};

template <>
struct DiagnoseMalformedGrant<void> {};

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

// `accepts_pack_v` exercises the LOW-LEVEL `IsAcceptedDirect` form because
// the test packs (AllStrictPack, CopyForUsagePack, MinusEffectPack)
// explicitly include `strict<dim::DimensionAxis::Type>`.  Routing them
// through the wrapper-discipline `IsAccepted` would double-engage the
// Type axis (via ImplicitTypeMarker injection) and trip the
// UniqueEngagementPerAxis gate.
template <typename T, typename Tuple>
inline constexpr bool accepts_pack_v = []() {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return IsAcceptedDirect<T, Ts...>;
    }(static_cast<Tuple*>(nullptr));
}();

// 1. Empty pack rejects.
//    `IsAccepted<int>` (wrapper-discipline) auto-injects the Type marker
//    so the pack has 1 axis engaged but 19 missing → rejects.
static_assert(!IsAccepted<int>,
    "Empty Grants pack must reject (only Type engaged via injection).");
// Witnessed at the dim level too:
static_assert(!IsAcceptedGrants<>,
    "IsAcceptedGrants<> must reject the empty pack.");

// 2. All-strict pack accepts (low-level form — pack includes strict<Type>).
static_assert(accepts_pack_v<int, AllStrictPack>,
    "AllStrict pack must accept — every dim has an engagement marker.");

// 3. Single-relaxation pack rejects (under wrapper-discipline IsAccepted,
//    18 dims still unengaged after auto-injection of Type marker).
static_assert(!IsAccepted<int, grant::copy>,
    "Single Usage relaxation must reject — 18 other dims unengaged.");

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
