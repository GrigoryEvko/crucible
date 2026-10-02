#pragma once

// A modality says how a grade relates to the value it decorates.  The
// enum below is the choice a wrapper author makes once, at the point
// the wrapper is declared, and it decides which operations the wrapper
// is allowed to expose.
//
// A new enumerator has to reach the enum, the concept gates, the
// exclusivity predicates and the tag types together.  The cardinality
// guard in the check file of this header fires if any of them is
// missed.  The name and
// IsModality come from the enumerators by reflection, so they need no
// edit.
//
// Prefer the enum in a template parameter.  The tag types exist for the
// cases where an overload set reads better than a non-type argument.

#include <foundation/reflect/EnumName.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>

namespace foundation::algebra {

enum class ModalityKind : std::uint8_t {
    // The grade admits a counit: the value can come back out.
    Comonad = 0,
    // The grade admits a unit: a bare value can go in at a chosen grade.
    RelativeMonad = 1,
    // The grade is fixed at construction.  Whether it claims anything
    // about the value's content is the lattice's to state
    // (ClaimSubject in ClaimOrientation.h).
    Absolute = 2,
    // The grade is a row that composes by union, both in sequence and
    // in parallel.
    Relative = 3,
    // The grade is a protocol remainder.  Every operation advances it,
    // so a handle at one grade produces a handle at the next, and leq
    // is protocol subtyping.  Reserved for session handles, which keep
    // their own class body and satisfy a SteppingGraded concept rather
    // than instantiating Graded.
    Stepping = 4,
};

// The enum has no quotient kind and no coeffect kind.  Agreement of
// representatives is Absolute for every lattice in the tree, and a
// semiring grade is a Semiring-shaped lattice under Absolute.

inline constexpr std::size_t modality_kind_count = 5;

// A kind is a modality when an enumerator holds it.  The test reads the
// enumerators by reflection, so a new enumerator is admitted the moment
// it is declared, and a value cast from a byte outside the enum is not.
// The answer is a function at namespace scope that is not a template, so
// no translation unit can specialize it to admit a kind that the enum
// does not name.  It compares values and builds no name.
[[nodiscard]] consteval bool is_modality_kind(ModalityKind kind) {
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^ModalityKind)) {
        if (std::meta::extract<ModalityKind>(std::meta::constant_of(enumerator)) == kind) return true;
    }
    return false;
}

template <ModalityKind K>
concept IsModality = is_modality_kind(K);

template <ModalityKind K>
concept ComonadModality = (K == ModalityKind::Comonad);

template <ModalityKind K>
concept RelativeMonadModality = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
concept AbsoluteModality = (K == ModalityKind::Absolute);

template <ModalityKind K>
concept RelativeModality = (K == ModalityKind::Relative);

template <ModalityKind K>
concept SteppingModality = (K == ModalityKind::Stepping);

template <ModalityKind K>
inline constexpr bool has_counit_v = (K == ModalityKind::Comonad);

template <ModalityKind K>
inline constexpr bool has_unit_v = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
inline constexpr bool has_grade_only_v =
    (K == ModalityKind::Absolute) || (K == ModalityKind::Relative) || (K == ModalityKind::Stepping);

// The name of a modality is the identifier its enumerator declares,
// read by reflection, so a new enumerator is named the moment it is
// declared.  A value outside the enum yields "<unknown ModalityKind>".
//
// This stays consteval, although the helper it calls is constexpr.  The
// three Graded forwarders declare themselves consteval and call it, and
// widening the signature here would say something this header does not
// mean to say.  It is a template on the one type ModalityKind, so that
// only a unit that asks for a name pays for the walk of enum_name.
template <std::same_as<ModalityKind> Kind>
[[nodiscard]] consteval std::string_view modality_name(Kind kind) noexcept {
    return ::foundation::reflect::enum_name(kind);
}

namespace modality {

struct Comonad_t {
    static constexpr ModalityKind kind = ModalityKind::Comonad;
};
struct RelativeMonad_t {
    static constexpr ModalityKind kind = ModalityKind::RelativeMonad;
};
struct Absolute_t {
    static constexpr ModalityKind kind = ModalityKind::Absolute;
};
struct Relative_t {
    static constexpr ModalityKind kind = ModalityKind::Relative;
};
struct Stepping_t {
    static constexpr ModalityKind kind = ModalityKind::Stepping;
};

}  // namespace modality

}  // namespace foundation::algebra
