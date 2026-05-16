#pragma once

// ── crucible::fixy — Modality.h (FIXY-G10) ────────────────────────────
//
// Categorical modality classification for every grant tag.  Each
// shipped grant declares its modality via `using modality = ...;`;
// `grant_traits<G>` projects this to a value-level ModalityClass enum
// that the §6.8 collision catalog uses for modality-pair shape rules
// (R017, R018).
//
// ── The five modality classes ──────────────────────────────────────
//
//   Frame      — Absolute modality.  Invariant of the value; EBO-
//                collapses; does not transform or demand.  Examples:
//                reentrant, complexity_constant, sized<N>, copy.
//
//   Declares   — Comonad modality.  The binding PRODUCES a witness
//                for the property (counit-out).  Examples:
//                trust_assumed, from_source, declassify,
//                mutable_in_place, append_only, precision_*.
//
//   Requires   — RelativeMonad modality.  The binding DEMANDS an
//                input refinement from the caller (unit-in).
//                Examples: refined_with<Pred>, with<Effects...>,
//                overflow_wrap, overflow_saturate.
//
//   Linear     — Linear modality.  The binding consumes-and-produces
//                a resource (one-shot).  In Crucible's fixy grants
//                this maps specifically to `lifetime_region<Tag>`
//                paired with Mutable mutation — the binding holds an
//                exclusive borrow of the region for its execution.
//
//   Quotient   — Quotient modality.  Equivalence-class membership;
//                the grant names a representative of an equivalence
//                class (Version<N>, Vendor<V>, ForgePhase<P>, ...).
//                Two Quotient grants on the same axis with different
//                representatives are structurally incompatible.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   fixy::ModalityClass                          — enum class
//   fixy::classify_modality_v<Modality>          — algebra→fixy map
//   fixy::grant_traits<G>                        — projection trait
//   fixy::default_witness_for_class<MC>          — witness default per MC
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G10 modality classification
//   algebra/Modality.h                    — ModalityKind enum
//   fixy/Grant.h                          — grant-side `using modality`
//   fixy/Rules.h                          — R017/R018 collision rules

#include <crucible/algebra/Modality.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <string_view>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── ModalityClass — the 5-class fixy taxonomy ──────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class ModalityClass : std::uint8_t {
    Frame      = 0,  // Absolute       — invariant
    Declares   = 1,  // Comonad        — produces witness
    Requires   = 2,  // RelativeMonad  — demands refinement
    Linear     = 3,  // Linear         — consume-and-produce
    Quotient   = 4,  // Quotient       — equivalence class
};

[[nodiscard]] consteval std::string_view modality_class_name(
    ModalityClass mc) noexcept
{
    switch (mc) {
        case ModalityClass::Frame:    return "Frame";
        case ModalityClass::Declares: return "Declares";
        case ModalityClass::Requires: return "Requires";
        case ModalityClass::Linear:   return "Linear";
        case ModalityClass::Quotient: return "Quotient";
        default:                       return std::string_view{"<unknown ModalityClass>"};
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── classify_modality_v — algebra::Modality* → ModalityClass ───────
// ═════════════════════════════════════════════════════════════════════
//
// Maps the algebra-layer modality tag types (Absolute_t, Comonad_t,
// RelativeMonad_t, Relative_t, Quotient_t) to the fixy ModalityClass
// enum.  The Linear modality class is reserved for Permission-typed
// resource transfer (grant::lifetime_region paired with Mutable —
// enforced by R013).

namespace detail {

template <typename Mod>
struct classify_modality_impl;

template <>
struct classify_modality_impl<::crucible::algebra::modality::Absolute_t> {
    static constexpr ModalityClass value = ModalityClass::Frame;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Comonad_t> {
    static constexpr ModalityClass value = ModalityClass::Declares;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::RelativeMonad_t> {
    static constexpr ModalityClass value = ModalityClass::Requires;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Relative_t> {
    // Cross-region flow — treat as Linear at the fixy layer (CSL
    // Permission discipline applies; R013 is the production rule).
    static constexpr ModalityClass value = ModalityClass::Linear;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Quotient_t> {
    static constexpr ModalityClass value = ModalityClass::Quotient;
};

}  // namespace detail

template <typename Mod>
inline constexpr ModalityClass classify_modality_v =
    detail::classify_modality_impl<Mod>::value;

// ═════════════════════════════════════════════════════════════════════
// ── grant_traits<G> — per-grant projection ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reads G's `using modality = ...;` (FIXY-G10 sweep on Grant.h) and
// `using witness_t = ...;` (FIXY-G9 sweep), and exposes them under a
// uniform shape.

template <typename G>
struct grant_traits {
    using modality       = typename G::modality;
    using witness_t      = typename G::witness_t;
    static constexpr ModalityClass modality_class_v =
        classify_modality_v<modality>;
};

// ═════════════════════════════════════════════════════════════════════
// ── default_witness_for_class<MC> — suggested witness floor per MC ──
// ═════════════════════════════════════════════════════════════════════
//
// Each modality class has a per-class default witness expectation
// used by future migration tooling (FIXY-G16 nearest_canonical_t) to
// suggest evidence upgrades.  Not a hard constraint — bindings can
// default to Asserted regardless.

namespace detail {

template <ModalityClass MC>
struct default_witness_for_class_impl;

template <>
struct default_witness_for_class_impl<ModalityClass::Frame> {
    using type = ::crucible::safety::witness::Asserted<
        ::crucible::safety::witness::UnnamedRationale>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Declares> {
    using type = ::crucible::safety::witness::Tested<0>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Requires> {
    using type = ::crucible::safety::witness::Asserted<
        ::crucible::safety::witness::UnnamedRationale>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Linear> {
    using type = ::crucible::safety::witness::CrossValidated<0>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Quotient> {
    using type = ::crucible::safety::witness::Tested<0>;
};

}  // namespace detail

template <ModalityClass MC>
using default_witness_for_class =
    typename detail::default_witness_for_class_impl<MC>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace modality_self_test {

namespace alg = ::crucible::algebra;

static_assert(classify_modality_v<alg::modality::Absolute_t>      == ModalityClass::Frame);
static_assert(classify_modality_v<alg::modality::Comonad_t>       == ModalityClass::Declares);
static_assert(classify_modality_v<alg::modality::RelativeMonad_t> == ModalityClass::Requires);
static_assert(classify_modality_v<alg::modality::Relative_t>      == ModalityClass::Linear);
static_assert(classify_modality_v<alg::modality::Quotient_t>      == ModalityClass::Quotient);

static_assert(modality_class_name(ModalityClass::Frame)    == "Frame");
static_assert(modality_class_name(ModalityClass::Declares) == "Declares");
static_assert(modality_class_name(ModalityClass::Requires) == "Requires");
static_assert(modality_class_name(ModalityClass::Linear)   == "Linear");
static_assert(modality_class_name(ModalityClass::Quotient) == "Quotient");

}  // namespace modality_self_test

}  // namespace crucible::fixy
