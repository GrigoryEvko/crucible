#pragma once

// ── crucible::algebra::lattices::TrustLattice<Source> ───────────────
//
// Per-Source singleton lattice — the foundation for Tagged<T, Source>
// per 25_04_2026.md §2.3:
//
//     using Tagged<T, Source> = Graded<RelativeMonad, TrustLattice<Source>, T>;
//
// ── Why "singleton"? ────────────────────────────────────────────────
//
// A Tagged<T, Source> value carries Source at the type level — every
// Tagged<T, source::FromUser> instance has provenance "FromUser",
// every Tagged<T, trust::Verified> instance has trust "Verified", etc.
// There's no runtime-varying source within a single Tagged<T, Source>
// type; the source IS the type parameter.  So TrustLattice<Source>
// has a SINGLE element (encoded in an EMPTY tag struct at the type
// level), and all lattice operations are trivially identity.
//
// Empty element_type + [[no_unique_address]] in Graded gives
// Tagged<T, S> sizeof(T) — matching the existing safety::Tagged<T, V>
// zero-overhead guarantee that MIGRATE-4 (#464) preserves.
//
// ── Cross-source subsumption ────────────────────────────────────────
//
// Tagged<T, source::Sanitized> can flow to a position expecting
// Tagged<T, source::External> (sanitized values are safer than raw
// external).  This subsumption is NOT inside TrustLattice<Source>'s
// definition (Sanitized and External are different lattices) — it
// lives in SessionPayloadSubsort.h's `is_subsort` axioms (shipped
// per #395 SAFEINT-S6).  TrustLattice's job is the per-source
// singleton structure; cross-source subsumption is the job of the
// subsort family.
//
// The four tag namespaces from safety::Tagged convention work
// uniformly: source::* (provenance), trust::* (verification),
// access::* (mode), version::* (schema).  TrustLattice<Source>
// doesn't enforce or care about tag namespacing — that's
// a discipline of the safety::Tagged alias.
//
// ── Modality is RelativeMonad ───────────────────────────────────────
//
// Tagged<T, Source> uses RelativeMonad form per 25_04 §2.3.  The
// relative-monad unit `inject` is named `retag<NewSource>()` in
// Tagged<>'s public API.  At the Graded level, inject(value, grade)
// is a static factory available only for RelativeMonad modality —
// it's how new tagged values come into existence.
//
//   Axiom coverage: TypeSafe — Source is encoded at the type level;
//                   TrustLattice<A> and TrustLattice<B> are
//                   structurally distinct types so cross-source
//                   composition is structurally impossible (verified
//                   by static_assert(!is_same_v<...>)).
//   Runtime cost:   zero — empty element_type collapses via EBO.
//
// See ALGEBRA-3 (Graded.h), ALGEBRA-2 (Lattice.h);
// SessionPayloadSubsort.h for cross-source subsumption axioms;
// MIGRATE-4 (#464) for the Tagged<T, Source> alias instantiation.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── TrustLattice<Source>: per-Source singleton sub-lattice ──────────
template <typename Source>
struct TrustLattice {
    // Empty tag.  Source is captured at the type level; the element
    // carries no runtime state.  Comparison is trivially true (only
    // one possible value per Source type).
    struct element_type {
        using source_type = Source;
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
            return true;
        }
    };

    // Lattice-level introspection: external code can recover Source
    // either through the lattice (`TrustLattice<S>::source_type`) or
    // through the element (`element_type::source_type`) — symmetric
    // with QttSemiring's grade vs element_type::value pattern.
    using source_type = Source;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
    [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    // Diagnostic name — reflection-derived from Source's display
    // string.  For top-level sources (e.g. `source::FromUser`),
    // display_string_of returns the qualified name.  For nested
    // self-test sources, returns the simple name (verified by
    // static_assert below).  Used by SessionDiagnostic / Cipher
    // serialize / debug print to identify which Source the
    // TrustLattice carries.
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return std::meta::display_string_of(^^Source);
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::trust_lattice_self_test {

// Witness sources from each of the four tag namespaces that
// safety::Tagged uses (source::*, trust::*, access::*, version::*).
// Each namespace is independent — TrustLattice<S> works uniformly
// for any Source in any namespace.
namespace source {
    struct FromUser     {};
    struct FromDb       {};
    struct FromConfig   {};
    struct FromInternal {};
    struct External     {};
    struct Sanitized    {};
}
namespace trust {
    struct Verified   {};
    struct Tested     {};
    struct Unverified {};
}
namespace access {
    struct RW {};
    struct RO {};
    struct WO {};
}
namespace version {
    struct V1 {};
    struct V2 {};
    template <int N> struct V {};  // templated source witness
}

// Concept conformance — each Source instantiates a valid Lattice
// with bounded structure.  Covers all four tag namespaces + a
// templated source.
static_assert(Lattice<TrustLattice<source::FromUser>>);
static_assert(Lattice<TrustLattice<source::FromDb>>);
static_assert(Lattice<TrustLattice<source::Sanitized>>);
static_assert(Lattice<TrustLattice<trust::Verified>>);
static_assert(Lattice<TrustLattice<trust::Unverified>>);
static_assert(Lattice<TrustLattice<access::RW>>);
static_assert(Lattice<TrustLattice<access::WO>>);
static_assert(Lattice<TrustLattice<version::V1>>);
static_assert(Lattice<TrustLattice<version::V<2>>>);

static_assert(BoundedLattice<TrustLattice<source::FromUser>>);
static_assert(BoundedLattice<TrustLattice<trust::Verified>>);
static_assert(BoundedLattice<TrustLattice<access::RW>>);
static_assert(BoundedLattice<TrustLattice<version::V<7>>>);

// Empty element_type for EBO collapse — load-bearing for Tagged<T,S>'s
// zero-overhead guarantee.
static_assert(std::is_empty_v<TrustLattice<source::FromUser>::element_type>);
static_assert(std::is_empty_v<TrustLattice<trust::Verified>::element_type>);
static_assert(std::is_empty_v<TrustLattice<access::RW>::element_type>);
static_assert(std::is_empty_v<TrustLattice<version::V<2>>::element_type>);

// Lattice axioms hold (trivially — single-element lattice).
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<source::FromUser>>(
    {}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<trust::Verified>>(
    {}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<access::RW>>(
    {}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<version::V1>>(
    {}, {}, {}));
static_assert(verify_bounded_lattice_axioms_at<TrustLattice<version::V<3>>>(
    {}, {}, {}));

// source_type alias is correct at BOTH lattice level AND element
// level — symmetric introspection.
static_assert(std::is_same_v<TrustLattice<source::FromUser>::source_type,
                              source::FromUser>);
static_assert(std::is_same_v<TrustLattice<source::FromUser>::element_type::source_type,
                              source::FromUser>);
static_assert(std::is_same_v<TrustLattice<version::V<5>>::source_type,
                              version::V<5>>);

// CRITICAL: cross-source distinctness — TrustLattice<A> and
// TrustLattice<B> for A != B must be DIFFERENT types so Graded::
// compose() (which takes Graded const& other of THE SAME L) cannot
// accidentally combine values across sources.  This is the structural
// type safety that prevents Tagged<T, source::External> from
// composing with Tagged<T, source::Sanitized> at the Graded level —
// the only allowed transitions go through the SessionPayloadSubsort
// is_subsort axioms.
static_assert(!std::is_same_v<TrustLattice<source::FromUser>,
                               TrustLattice<source::FromDb>>);
static_assert(!std::is_same_v<TrustLattice<source::External>,
                               TrustLattice<source::Sanitized>>);
static_assert(!std::is_same_v<TrustLattice<trust::Verified>,
                               TrustLattice<trust::Unverified>>);
static_assert(!std::is_same_v<TrustLattice<source::FromUser>,
                               TrustLattice<trust::Verified>>);  // cross-namespace
static_assert(!std::is_same_v<TrustLattice<version::V<1>>,
                               TrustLattice<version::V<2>>>);   // templated source

// Diagnostic name comes from reflection.  For sources defined at
// nested-namespace scope inside the self-test, display_string_of
// returns either the SIMPLE name or the fully-qualified form
// depending on the depth of the including TU's scope chain (the
// TU-context-fragility documented in
// gcc16_c26_reflection_gotchas memory rule #5 +
// header_only_static_assert_blind_spot rule).  Use ends_with()
// rather than == so the assertions are robust across the
// algebra-only TU and the safety/* migration TUs.
static_assert(TrustLattice<source::FromUser>::name() .ends_with("FromUser"));
static_assert(TrustLattice<source::Sanitized>::name().ends_with("Sanitized"));
static_assert(TrustLattice<trust::Verified>::name()  .ends_with("Verified"));
static_assert(TrustLattice<access::RW>::name()       .ends_with("RW"));
static_assert(TrustLattice<version::V1>::name()      .ends_with("V1"));
// Templated source — display_string_of includes the template argument.
static_assert(TrustLattice<version::V<7>>::name()    .ends_with("V<7>"));

// ── Layout invariants on Graded<...,TrustLattice<S>,T> ─────────────
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using TaggedFromUser =
    Graded<ModalityKind::RelativeMonad, TrustLattice<source::FromUser>, T>;
template <typename T>
using TaggedSanitized =
    Graded<ModalityKind::RelativeMonad, TrustLattice<source::Sanitized>, T>;
template <typename T>
using TaggedVerified =
    Graded<ModalityKind::RelativeMonad, TrustLattice<trust::Verified>, T>;
template <typename T>
using TaggedV2 =
    Graded<ModalityKind::RelativeMonad, TrustLattice<version::V<2>>, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedFromUser,  OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedFromUser,  EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedSanitized, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedVerified,  EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedV2,        EightByteValue);
// Arithmetic T witnesses — pin macro correctness across the
// trivially-default-constructible-T axis (AUDIT-FOUNDATION dropped
// tdc parity).  Critical for MIGRATE-4 (Tagged<int, source::FromUser>
// for shape-bound integers from the Vessel boundary).
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedFromUser,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedSanitized, double);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded operations with non-constant args.  Includes
// the RelativeMonad-only `inject` factory (gated by requires-clause
// in Graded; available because this Graded uses RelativeMonad).
inline void runtime_smoke_test() {
    using L = TrustLattice<source::Sanitized>;
    L::element_type a{};
    L::element_type b{};
    [[maybe_unused]] bool             l = L::leq(a, b);
    [[maybe_unused]] L::element_type  j = L::join(a, b);
    [[maybe_unused]] L::element_type  m = L::meet(a, b);

    OneByteValue v{42};
    TaggedSanitized<OneByteValue> initial{v, L::bottom()};
    auto widened   = initial.weaken(L::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(L::top());

    // RelativeMonad inject — only available because modality is
    // RelativeMonad (gated by Graded's requires-clause).
    auto injected = TaggedSanitized<OneByteValue>::inject(
        OneByteValue{99}, L::bottom());

    [[maybe_unused]] auto g  = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(injected).consume().c;
}

}  // namespace detail::trust_lattice_self_test

}  // namespace crucible::algebra::lattices
