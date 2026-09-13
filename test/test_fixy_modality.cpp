// Including the header from a translation unit is what makes its own
// sentinels run under the project warning flags.

#include <crucible/fixy/Modality.h>
#include <crucible/algebra/Modality.h>

#include <type_traits>

namespace fm = ::crucible::fixy::modality;
namespace am = ::crucible::algebra;
namespace amm = ::crucible::algebra::modality;

static_assert(std::is_same_v<fm::ModalityKind, am::ModalityKind>);

static_assert(fm::ModalityKind::Comonad == am::ModalityKind::Comonad);
static_assert(fm::ModalityKind::RelativeMonad == am::ModalityKind::RelativeMonad);
static_assert(fm::ModalityKind::Absolute == am::ModalityKind::Absolute);
static_assert(fm::ModalityKind::Relative == am::ModalityKind::Relative);
static_assert(fm::ModalityKind::Quotient == am::ModalityKind::Quotient);
static_assert(fm::ModalityKind::Coeffect == am::ModalityKind::Coeffect);

static_assert(fm::IsModality<fm::ModalityKind::Comonad>);
static_assert(fm::IsModality<fm::ModalityKind::RelativeMonad>);
static_assert(fm::IsModality<fm::ModalityKind::Absolute>);
static_assert(fm::IsModality<fm::ModalityKind::Relative>);
static_assert(fm::IsModality<fm::ModalityKind::Quotient>);
static_assert(fm::IsModality<fm::ModalityKind::Coeffect>);

static_assert(fm::ComonadModality<fm::ModalityKind::Comonad>);
static_assert(!fm::ComonadModality<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Absolute>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Relative>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Quotient>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Coeffect>);

static_assert(!fm::RelativeMonadModality<fm::ModalityKind::Comonad>);
static_assert(fm::RelativeMonadModality<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::RelativeMonadModality<fm::ModalityKind::Absolute>);

static_assert(fm::AbsoluteModality<fm::ModalityKind::Absolute>);
static_assert(!fm::AbsoluteModality<fm::ModalityKind::Relative>);

static_assert(fm::RelativeModality<fm::ModalityKind::Relative>);
static_assert(!fm::RelativeModality<fm::ModalityKind::Absolute>);

static_assert(fm::QuotientModality<fm::ModalityKind::Quotient>);
static_assert(!fm::QuotientModality<fm::ModalityKind::Coeffect>);

static_assert(fm::CoeffectModality<fm::ModalityKind::Coeffect>);
static_assert(!fm::CoeffectModality<fm::ModalityKind::Quotient>);

// A concept has no type, so identity across the alias is checked by
// comparing what each side evaluates to.
static_assert(fm::ComonadModality<fm::ModalityKind::Comonad> == am::ComonadModality<fm::ModalityKind::Comonad>);
static_assert(fm::CoeffectModality<fm::ModalityKind::Coeffect> == am::CoeffectModality<fm::ModalityKind::Coeffect>);

static_assert(std::is_same_v<fm::Comonad_t, amm::Comonad_t>);
static_assert(std::is_same_v<fm::RelativeMonad_t, amm::RelativeMonad_t>);
static_assert(std::is_same_v<fm::Absolute_t, amm::Absolute_t>);
static_assert(std::is_same_v<fm::Relative_t, amm::Relative_t>);
static_assert(std::is_same_v<fm::Quotient_t, amm::Quotient_t>);
static_assert(std::is_same_v<fm::Coeffect_t, amm::Coeffect_t>);

static_assert(fm::Comonad_t::kind == fm::ModalityKind::Comonad);
static_assert(fm::RelativeMonad_t::kind == fm::ModalityKind::RelativeMonad);
static_assert(fm::Absolute_t::kind == fm::ModalityKind::Absolute);
static_assert(fm::Relative_t::kind == fm::ModalityKind::Relative);
static_assert(fm::Quotient_t::kind == fm::ModalityKind::Quotient);
static_assert(fm::Coeffect_t::kind == fm::ModalityKind::Coeffect);

static_assert(sizeof(fm::Comonad_t) == 1);
static_assert(sizeof(fm::RelativeMonad_t) == 1);
static_assert(sizeof(fm::Absolute_t) == 1);
static_assert(sizeof(fm::Relative_t) == 1);
static_assert(sizeof(fm::Quotient_t) == 1);
static_assert(sizeof(fm::Coeffect_t) == 1);

// The exact pin lives beside the constant it counts.  What belongs here
// is the floor, which catches the other direction: a modality form
// removed without the pin being touched.

static_assert(fm::modality_kind_count == am::modality_kind_count);
static_assert(fm::modality_kind_count >= 6, "modality_kind_count has fallen below 6, which means a ModalityKind "
                                            "enumerator was removed without updating this floor and the exact "
                                            "pin that sits beside the constant.");

static_assert(fm::has_counit_v<fm::ModalityKind::Comonad>);
static_assert(!fm::has_counit_v<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::has_counit_v<fm::ModalityKind::Absolute>);
static_assert(!fm::has_counit_v<fm::ModalityKind::Coeffect>);

static_assert(!fm::has_unit_v<fm::ModalityKind::Comonad>);
static_assert(fm::has_unit_v<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::has_unit_v<fm::ModalityKind::Absolute>);

static_assert(!fm::has_grade_only_v<fm::ModalityKind::Comonad>);
static_assert(!fm::has_grade_only_v<fm::ModalityKind::RelativeMonad>);
static_assert(fm::has_grade_only_v<fm::ModalityKind::Absolute>);
static_assert(fm::has_grade_only_v<fm::ModalityKind::Relative>);
static_assert(fm::has_grade_only_v<fm::ModalityKind::Quotient>);
static_assert(fm::has_grade_only_v<fm::ModalityKind::Coeffect>);

// Floors again, for the concept, tag and query counts.  Growth past
// them is silent here and caught by the exact pins beside the constants.

static_assert(fm::self_test::u060_concept_cardinality >= 7);
static_assert(fm::self_test::u060_tag_cardinality >= 6);
static_assert(fm::self_test::u060_query_cardinality >= 3);

static_assert(fm::modality_name(fm::ModalityKind::Comonad) == "Comonad");
static_assert(fm::modality_name(fm::ModalityKind::RelativeMonad) == "RelativeMonad");
static_assert(fm::modality_name(fm::ModalityKind::Absolute) == "Absolute");
static_assert(fm::modality_name(fm::ModalityKind::Relative) == "Relative");
static_assert(fm::modality_name(fm::ModalityKind::Quotient) == "Quotient");
static_assert(fm::modality_name(fm::ModalityKind::Coeffect) == "Coeffect");

int main() {
    // Every claim in this file is a static_assert.
    return 0;
}
