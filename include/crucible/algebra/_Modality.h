#pragma once

// A modality says how a grade relates to the value it decorates.  The
// enum below is the choice a wrapper author makes once, at the point
// the wrapper is declared, and it decides which operations the wrapper
// is allowed to expose.
//
// A new enumerator has to reach the enum, the name switch, the concept
// gates, the exclusivity predicates and the tag types together.  The
// cardinality guard in the self-test fires if any of them is missed.
//
// Prefer the enum in a template parameter.  The tag types exist for the
// cases where an overload set reads better than a non-type argument.

#include <cstdint>
#include <meta>
#include <string_view>

namespace crucible::algebra {

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
    // The grade names a representative of an equivalence class, and two
    // grades agree when their representatives do.  Reach for this only
    // when that agreement differs from structural equality.  While the
    // two coincide, Absolute already says everything Quotient would.
    Quotient = 4,
    // The grade is a resource the computation consumes, which makes it
    // a semiring rather than a lattice: sequence adds, parallel takes
    // the maximum, repetition multiplies.
    Coeffect = 5,
};

inline constexpr std::size_t modality_kind_count = std::meta::enumerators_of(^^ModalityKind).size();

template <ModalityKind K>
concept IsModality = K == ModalityKind::Comonad || K == ModalityKind::RelativeMonad || K == ModalityKind::Absolute
                  || K == ModalityKind::Relative || K == ModalityKind::Quotient || K == ModalityKind::Coeffect;

template <ModalityKind K>
concept ComonadModality = (K == ModalityKind::Comonad);

template <ModalityKind K>
concept RelativeMonadModality = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
concept AbsoluteModality = (K == ModalityKind::Absolute);

template <ModalityKind K>
concept RelativeModality = (K == ModalityKind::Relative);

template <ModalityKind K>
concept QuotientModality = (K == ModalityKind::Quotient);

template <ModalityKind K>
concept CoeffectModality = (K == ModalityKind::Coeffect);

template <ModalityKind K>
inline constexpr bool has_counit_v = (K == ModalityKind::Comonad);

template <ModalityKind K>
inline constexpr bool has_unit_v = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
inline constexpr bool has_grade_only_v = (K == ModalityKind::Absolute) || (K == ModalityKind::Relative)
                                      || (K == ModalityKind::Quotient) || (K == ModalityKind::Coeffect);

// The switch is exhaustive, but the default arm is required by the
// warning settings.  Its sentinel string is load-bearing: the
// name-coverage self-test below detects a missing arm by looking for
// exactly this text.
[[nodiscard]] consteval std::string_view modality_name(ModalityKind K) noexcept {
    switch (K) {
        case ModalityKind::Comonad:
            return "Comonad";
        case ModalityKind::RelativeMonad:
            return "RelativeMonad";
        case ModalityKind::Absolute:
            return "Absolute";
        case ModalityKind::Relative:
            return "Relative";
        case ModalityKind::Quotient:
            return "Quotient";
        case ModalityKind::Coeffect:
            return "Coeffect";
        default:
            return std::string_view{"<unknown ModalityKind>"};
    }
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
struct Quotient_t {
    static constexpr ModalityKind kind = ModalityKind::Quotient;
};
struct Coeffect_t {
    static constexpr ModalityKind kind = ModalityKind::Coeffect;
};

}  // namespace modality

namespace detail::modality_self_test {

static_assert(modality_kind_count == 6, "Modality count diverged from the six-member set "
                                        "(Comonad/RelativeMonad/Absolute/Relative/Quotient/Coeffect).  Confirm "
                                        "the addition is intentional and that the name-coverage assertion "
                                        "below still fires for the new enumerator.");

// The expansion statement puts each iteration's induction variable in
// its own scope, and the shadow warning fires on the second one.
[[nodiscard]] consteval bool every_modality_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ModalityKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (modality_name([:en:]) == std::string_view{"<unknown ModalityKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_modality_has_name(), "modality_name() switch is missing an arm for at least one "
                                         "ModalityKind enumerator — add the arm or the new enumerator "
                                         "leaks the '<unknown ModalityKind>' sentinel into diagnostics.");

template <ModalityKind K>
inline constexpr bool is_exactly_one_predicate =
    int(has_counit_v<K>) + int(has_unit_v<K>) + int(has_grade_only_v<K>) == 1;

static_assert(is_exactly_one_predicate<ModalityKind::Comonad>);
static_assert(is_exactly_one_predicate<ModalityKind::RelativeMonad>);
static_assert(is_exactly_one_predicate<ModalityKind::Absolute>);
static_assert(is_exactly_one_predicate<ModalityKind::Relative>);
static_assert(is_exactly_one_predicate<ModalityKind::Quotient>);
static_assert(is_exactly_one_predicate<ModalityKind::Coeffect>);

static_assert(modality::Comonad_t::kind == ModalityKind::Comonad);
static_assert(modality::RelativeMonad_t::kind == ModalityKind::RelativeMonad);
static_assert(modality::Absolute_t::kind == ModalityKind::Absolute);
static_assert(modality::Relative_t::kind == ModalityKind::Relative);
static_assert(modality::Quotient_t::kind == ModalityKind::Quotient);
static_assert(modality::Coeffect_t::kind == ModalityKind::Coeffect);

static_assert(IsModality<ModalityKind::Comonad>);
static_assert(IsModality<ModalityKind::RelativeMonad>);
static_assert(IsModality<ModalityKind::Absolute>);
static_assert(IsModality<ModalityKind::Relative>);
static_assert(IsModality<ModalityKind::Quotient>);
static_assert(IsModality<ModalityKind::Coeffect>);

static_assert(ComonadModality<ModalityKind::Comonad>);
static_assert(!ComonadModality<ModalityKind::Absolute>);
static_assert(RelativeMonadModality<ModalityKind::RelativeMonad>);
static_assert(!RelativeMonadModality<ModalityKind::Comonad>);
static_assert(AbsoluteModality<ModalityKind::Absolute>);
static_assert(!AbsoluteModality<ModalityKind::Relative>);
static_assert(RelativeModality<ModalityKind::Relative>);
static_assert(!RelativeModality<ModalityKind::Absolute>);
static_assert(QuotientModality<ModalityKind::Quotient>);
static_assert(!QuotientModality<ModalityKind::Absolute>);
static_assert(CoeffectModality<ModalityKind::Coeffect>);
static_assert(!CoeffectModality<ModalityKind::Absolute>);

static_assert(!modality_name(ModalityKind::Comonad).empty());
static_assert(!modality_name(ModalityKind::RelativeMonad).empty());
static_assert(!modality_name(ModalityKind::Absolute).empty());
static_assert(!modality_name(ModalityKind::Relative).empty());
static_assert(!modality_name(ModalityKind::Quotient).empty());
static_assert(!modality_name(ModalityKind::Coeffect).empty());
static_assert(modality_name(ModalityKind::Comonad) != "<unknown ModalityKind>");
static_assert(modality_name(ModalityKind::RelativeMonad) != "<unknown ModalityKind>");
static_assert(modality_name(ModalityKind::Absolute) != "<unknown ModalityKind>");
static_assert(modality_name(ModalityKind::Relative) != "<unknown ModalityKind>");
static_assert(modality_name(ModalityKind::Quotient) != "<unknown ModalityKind>");
static_assert(modality_name(ModalityKind::Coeffect) != "<unknown ModalityKind>");

// A tag type must stay empty so that it costs nothing as a
// [[no_unique_address]] member.
static_assert(std::is_empty_v<modality::Comonad_t>);
static_assert(std::is_empty_v<modality::RelativeMonad_t>);
static_assert(std::is_empty_v<modality::Absolute_t>);
static_assert(std::is_empty_v<modality::Relative_t>);
static_assert(std::is_empty_v<modality::Quotient_t>);
static_assert(std::is_empty_v<modality::Coeffect_t>);

// modality_name is consteval and so cannot appear here at all.  What
// this checks is the rest of the header against runtime semantics.
inline void runtime_smoke_test() {
    [[maybe_unused]] modality::Comonad_t co_tag{};
    [[maybe_unused]] modality::RelativeMonad_t rm_tag{};
    [[maybe_unused]] modality::Absolute_t ab_tag{};
    [[maybe_unused]] modality::Relative_t rl_tag{};
    [[maybe_unused]] modality::Quotient_t qt_tag{};
    [[maybe_unused]] modality::Coeffect_t cf_tag{};

    // Reading ::kind into a non-constexpr local pins that it stays
    // usable in a runtime context as well as a constant one.
    ModalityKind k = modality::Comonad_t::kind;
    [[maybe_unused]] bool ok1 = (k == ModalityKind::Comonad);
    k = modality::Coeffect_t::kind;
    [[maybe_unused]] bool ok2 = (k == ModalityKind::Coeffect);

    // The predicates take a non-type template argument, so they cannot
    // be driven with a runtime value.  Only their results reach here.
    [[maybe_unused]] bool unit_co = has_unit_v<ModalityKind::RelativeMonad>;
    [[maybe_unused]] bool grade_ab = has_grade_only_v<ModalityKind::Absolute>;
    [[maybe_unused]] bool grade_cf = has_grade_only_v<ModalityKind::Coeffect>;
}

}  // namespace detail::modality_self_test

}  // namespace crucible::algebra
