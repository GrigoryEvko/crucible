#pragma once

// A modality says how a grade relates to the value it decorates.  The
// enum below is the choice a wrapper author makes once, at the point
// the wrapper is declared, and it decides which operations the wrapper
// is allowed to expose.
//
// A new enumerator has to reach the enum, the concept gates, the
// exclusivity predicates and the tag types together.  The cardinality
// guard in the self-test fires if any of them is missed.  The name is
// no longer one of them: it is read from the enumerator.
//
// Prefer the enum in a template parameter.  The tag types exist for the
// cases where an overload set reads better than a non-type argument.

#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string_view>

namespace foundation::algebra {

enum class ModalityKind : std::uint8_t {
    // The grade admits a counit: the value can come back out.
    Comonad = 0,
    // The grade admits a unit: a bare value can go in at a chosen grade.
    RelativeMonad = 1,
    // The grade is fixed at construction and says nothing about the
    // value's content.
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

// Two kinds were removed when the substrate was extracted: Quotient
// (agreement of representatives, which coincided with Absolute for
// every lattice in the tree) and Coeffect (a semiring grade, which the
// tree expresses as a Semiring-shaped lattice under Absolute).  Neither
// had an instantiation outside its own self-test.

inline constexpr std::size_t modality_kind_count = std::meta::enumerators_of(^^ModalityKind).size();

template <ModalityKind K>
concept IsModality = K == ModalityKind::Comonad || K == ModalityKind::RelativeMonad || K == ModalityKind::Absolute
                  || K == ModalityKind::Relative || K == ModalityKind::Stepping;

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
// declared.  A value outside the enum yields "<unknown ModalityKind>",
// the same sentinel the hand-written switch returned.
//
// This stays consteval, although the helper it calls is constexpr.  The
// three Graded forwarders declare themselves consteval and call it, and
// widening the signature here would say something this header does not
// mean to say.
[[nodiscard]] consteval std::string_view modality_name(ModalityKind K) noexcept {
    return ::foundation::reflect::enum_name(K);
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

namespace detail::modality_self_test {

static_assert(modality_kind_count == 5, "Modality count diverged from the five-member set "
                                        "(Comonad/RelativeMonad/Absolute/Relative/Stepping).  Confirm the "
                                        "addition is intentional and that the name pins below still cover "
                                        "the new enumerator.");

template <ModalityKind K>
inline constexpr bool is_exactly_one_predicate =
    int(has_counit_v<K>) + int(has_unit_v<K>) + int(has_grade_only_v<K>) == 1;

static_assert(is_exactly_one_predicate<ModalityKind::Comonad>);
static_assert(is_exactly_one_predicate<ModalityKind::RelativeMonad>);
static_assert(is_exactly_one_predicate<ModalityKind::Absolute>);
static_assert(is_exactly_one_predicate<ModalityKind::Relative>);
static_assert(is_exactly_one_predicate<ModalityKind::Stepping>);

static_assert(modality::Comonad_t::kind == ModalityKind::Comonad);
static_assert(modality::RelativeMonad_t::kind == ModalityKind::RelativeMonad);
static_assert(modality::Absolute_t::kind == ModalityKind::Absolute);
static_assert(modality::Relative_t::kind == ModalityKind::Relative);
static_assert(modality::Stepping_t::kind == ModalityKind::Stepping);

static_assert(IsModality<ModalityKind::Comonad>);
static_assert(IsModality<ModalityKind::RelativeMonad>);
static_assert(IsModality<ModalityKind::Absolute>);
static_assert(IsModality<ModalityKind::Relative>);
static_assert(IsModality<ModalityKind::Stepping>);

static_assert(ComonadModality<ModalityKind::Comonad>);
static_assert(!ComonadModality<ModalityKind::Absolute>);
static_assert(RelativeMonadModality<ModalityKind::RelativeMonad>);
static_assert(!RelativeMonadModality<ModalityKind::Comonad>);
static_assert(AbsoluteModality<ModalityKind::Absolute>);
static_assert(!AbsoluteModality<ModalityKind::Relative>);
static_assert(RelativeModality<ModalityKind::Relative>);
static_assert(!RelativeModality<ModalityKind::Absolute>);
static_assert(SteppingModality<ModalityKind::Stepping>);
static_assert(!SteppingModality<ModalityKind::Absolute>);
static_assert(!SteppingModality<ModalityKind::Relative>);

// Each enumerator renders as exactly the identifier it declares, and a
// value outside the enum still reaches the sentinel.  These pins
// replace the coverage walk that used to police the switch: with the
// name read from the enumerator, that walk answered true by
// construction.
static_assert(modality_name(ModalityKind::Comonad) == "Comonad");
static_assert(modality_name(ModalityKind::RelativeMonad) == "RelativeMonad");
static_assert(modality_name(ModalityKind::Absolute) == "Absolute");
static_assert(modality_name(ModalityKind::Relative) == "Relative");
static_assert(modality_name(ModalityKind::Stepping) == "Stepping");
static_assert(modality_name(static_cast<ModalityKind>(200)) == "<unknown ModalityKind>",
              "A value outside the enum must reach the unknown-kind sentinel, so a corrupt byte prints "
              "as one rather than as an empty name.");

// A tag type must stay empty so that it costs nothing as a
// [[no_unique_address]] member.
static_assert(std::is_empty_v<modality::Comonad_t>);
static_assert(std::is_empty_v<modality::RelativeMonad_t>);
static_assert(std::is_empty_v<modality::Absolute_t>);
static_assert(std::is_empty_v<modality::Relative_t>);
static_assert(std::is_empty_v<modality::Stepping_t>);

// modality_name is consteval and so cannot appear here at all.  What
// this checks is the rest of the header against runtime semantics.
inline void runtime_smoke_test() {
    [[maybe_unused]] modality::Comonad_t co_tag{};
    [[maybe_unused]] modality::RelativeMonad_t rm_tag{};
    [[maybe_unused]] modality::Absolute_t ab_tag{};
    [[maybe_unused]] modality::Relative_t rl_tag{};
    [[maybe_unused]] modality::Stepping_t st_tag{};

    // Reading ::kind into a non-constexpr local pins that it stays
    // usable in a runtime context as well as a constant one.
    ModalityKind k = modality::Comonad_t::kind;
    [[maybe_unused]] bool ok1 = (k == ModalityKind::Comonad);
    k = modality::Stepping_t::kind;
    [[maybe_unused]] bool ok2 = (k == ModalityKind::Stepping);

    // The predicates take a non-type template argument, so they cannot
    // be driven with a runtime value.  Only their results reach here.
    [[maybe_unused]] bool unit_co = has_unit_v<ModalityKind::RelativeMonad>;
    [[maybe_unused]] bool grade_ab = has_grade_only_v<ModalityKind::Absolute>;
    [[maybe_unused]] bool grade_st = has_grade_only_v<ModalityKind::Stepping>;
}

}  // namespace detail::modality_self_test

}  // namespace foundation::algebra
