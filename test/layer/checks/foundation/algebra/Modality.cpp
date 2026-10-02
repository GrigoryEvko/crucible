// The compile-time checks of foundation/algebra/Modality.h.

#include <foundation/algebra/Modality.h>

namespace foundation::algebra {

namespace detail::modality_self_test {

static_assert(modality_kind_count == std::meta::enumerators_of(^^ModalityKind).size(),
              "Modality count diverged from the five-member set (Comonad/RelativeMonad/Absolute/Relative/"
              "Stepping).  Confirm the addition is intentional, that the name pins below still cover the new "
              "enumerator, and write the new count in the initializer of modality_kind_count.");

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
static_assert(!IsModality<static_cast<ModalityKind>(5)> && !IsModality<static_cast<ModalityKind>(200)>,
              "a byte that no enumerator holds is not a modality");

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
// value outside the enum reaches the sentinel.
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

}  // namespace detail::modality_self_test

}  // namespace foundation::algebra
