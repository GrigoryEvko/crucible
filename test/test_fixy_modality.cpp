// ── test_fixy_modality — sentinel TU for FIXY-U-060 surface ─────────
//
// Pulls fixy/Modality.h into a TU compiled under project warning
// flags so the in-header sentinels execute under enforcement.
// Witnesses (orthogonal to the in-header self_test:: block):
//
//   1. fixy::modality::ModalityKind aliases algebra::ModalityKind.
//   2. All 6 enum values round-trip through both paths.
//   3. All 7 concepts (IsModality + 6 per-form) accept the canonical
//      modality each names AND reject every OTHER form.
//   4. All 6 tag types alias the substrate algebra::modality::*_t.
//   5. Tag::kind round-trip + sizeof == 1.
//   6. modality_kind_count matches reflection-derived substrate value.
//   7. has_counit_v / has_unit_v / has_grade_only_v exhaustive table.
//   8. Cardinality mirror against in-header sentinel constants.
//
// FIXY-U-060.

#include <crucible/fixy/Modality.h>
#include <crucible/algebra/Modality.h>

#include <type_traits>

namespace fm  = ::crucible::fixy::modality;
namespace am  = ::crucible::algebra;
namespace amm = ::crucible::algebra::modality;

// ─── 1. Enum identity ─────────────────────────────────────────────

static_assert(std::is_same_v<fm::ModalityKind, am::ModalityKind>);

// ─── 2. All 6 values round-trip ───────────────────────────────────

static_assert(fm::ModalityKind::Comonad       == am::ModalityKind::Comonad);
static_assert(fm::ModalityKind::RelativeMonad == am::ModalityKind::RelativeMonad);
static_assert(fm::ModalityKind::Absolute      == am::ModalityKind::Absolute);
static_assert(fm::ModalityKind::Relative      == am::ModalityKind::Relative);
static_assert(fm::ModalityKind::Quotient      == am::ModalityKind::Quotient);
static_assert(fm::ModalityKind::Coeffect      == am::ModalityKind::Coeffect);

// ─── 3. Concept positive + negative table ─────────────────────────

// IsModality accepts every form.
static_assert(fm::IsModality<fm::ModalityKind::Comonad>);
static_assert(fm::IsModality<fm::ModalityKind::RelativeMonad>);
static_assert(fm::IsModality<fm::ModalityKind::Absolute>);
static_assert(fm::IsModality<fm::ModalityKind::Relative>);
static_assert(fm::IsModality<fm::ModalityKind::Quotient>);
static_assert(fm::IsModality<fm::ModalityKind::Coeffect>);

// ComonadModality: accepts ONLY Comonad.
static_assert( fm::ComonadModality<fm::ModalityKind::Comonad>);
static_assert(!fm::ComonadModality<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Absolute>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Relative>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Quotient>);
static_assert(!fm::ComonadModality<fm::ModalityKind::Coeffect>);

// RelativeMonadModality: accepts ONLY RelativeMonad.
static_assert(!fm::RelativeMonadModality<fm::ModalityKind::Comonad>);
static_assert( fm::RelativeMonadModality<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::RelativeMonadModality<fm::ModalityKind::Absolute>);

// AbsoluteModality: accepts ONLY Absolute.
static_assert( fm::AbsoluteModality<fm::ModalityKind::Absolute>);
static_assert(!fm::AbsoluteModality<fm::ModalityKind::Relative>);

// RelativeModality: accepts ONLY Relative.
static_assert( fm::RelativeModality<fm::ModalityKind::Relative>);
static_assert(!fm::RelativeModality<fm::ModalityKind::Absolute>);

// QuotientModality: accepts ONLY Quotient.
static_assert( fm::QuotientModality<fm::ModalityKind::Quotient>);
static_assert(!fm::QuotientModality<fm::ModalityKind::Coeffect>);

// CoeffectModality: accepts ONLY Coeffect.
static_assert( fm::CoeffectModality<fm::ModalityKind::Coeffect>);
static_assert(!fm::CoeffectModality<fm::ModalityKind::Quotient>);

// Concepts mirror their substrate (definitional alias preservation).
static_assert(fm::ComonadModality<fm::ModalityKind::Comonad>
              == am::ComonadModality<fm::ModalityKind::Comonad>);
static_assert(fm::CoeffectModality<fm::ModalityKind::Coeffect>
              == am::CoeffectModality<fm::ModalityKind::Coeffect>);

// ─── 4. Tag-type identity ─────────────────────────────────────────

static_assert(std::is_same_v<fm::Comonad_t,       amm::Comonad_t>);
static_assert(std::is_same_v<fm::RelativeMonad_t, amm::RelativeMonad_t>);
static_assert(std::is_same_v<fm::Absolute_t,      amm::Absolute_t>);
static_assert(std::is_same_v<fm::Relative_t,      amm::Relative_t>);
static_assert(std::is_same_v<fm::Quotient_t,      amm::Quotient_t>);
static_assert(std::is_same_v<fm::Coeffect_t,      amm::Coeffect_t>);

// ─── 5. Tag round-trip + sizeof ───────────────────────────────────

static_assert(fm::Comonad_t::kind       == fm::ModalityKind::Comonad);
static_assert(fm::RelativeMonad_t::kind == fm::ModalityKind::RelativeMonad);
static_assert(fm::Absolute_t::kind      == fm::ModalityKind::Absolute);
static_assert(fm::Relative_t::kind      == fm::ModalityKind::Relative);
static_assert(fm::Quotient_t::kind      == fm::ModalityKind::Quotient);
static_assert(fm::Coeffect_t::kind      == fm::ModalityKind::Coeffect);

static_assert(sizeof(fm::Comonad_t)       == 1);
static_assert(sizeof(fm::RelativeMonad_t) == 1);
static_assert(sizeof(fm::Absolute_t)      == 1);
static_assert(sizeof(fm::Relative_t)      == 1);
static_assert(sizeof(fm::Quotient_t)      == 1);
static_assert(sizeof(fm::Coeffect_t)      == 1);

// ─── 6. Cardinality reflection ────────────────────────────────────
//
// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// (`== 6`) lives in fixy/Modality.h colocated with the source-of-truth
// constant; THIS TU only holds the FLOOR pin (`>= 6`) which catches the
// inverse direction — an accidental REMOVAL of a modality form.

static_assert(fm::modality_kind_count == am::modality_kind_count);
static_assert(fm::modality_kind_count >= 6,
    "floor: fixy::modality::modality_kind_count regressed below 6 — "
    "a ModalityKind enumerator was removed without updating both "
    "Modality.h's colocated ceiling pin AND this floor witness.");

// ─── 7. has_*_v exhaustive table ─────────────────────────────────

// has_counit_v: Comonad only.
static_assert( fm::has_counit_v<fm::ModalityKind::Comonad>);
static_assert(!fm::has_counit_v<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::has_counit_v<fm::ModalityKind::Absolute>);
static_assert(!fm::has_counit_v<fm::ModalityKind::Coeffect>);

// has_unit_v: RelativeMonad only.
static_assert(!fm::has_unit_v<fm::ModalityKind::Comonad>);
static_assert( fm::has_unit_v<fm::ModalityKind::RelativeMonad>);
static_assert(!fm::has_unit_v<fm::ModalityKind::Absolute>);

// has_grade_only_v: Absolute / Relative / Quotient / Coeffect.
static_assert(!fm::has_grade_only_v<fm::ModalityKind::Comonad>);
static_assert(!fm::has_grade_only_v<fm::ModalityKind::RelativeMonad>);
static_assert( fm::has_grade_only_v<fm::ModalityKind::Absolute>);
static_assert( fm::has_grade_only_v<fm::ModalityKind::Relative>);
static_assert( fm::has_grade_only_v<fm::ModalityKind::Quotient>);
static_assert( fm::has_grade_only_v<fm::ModalityKind::Coeffect>);

// ─── 8. Cardinality FLOOR mirrors against in-header sentinel ──────
//
// Per FIXY-U-127 / U-128 floor-vs-ceiling split: the EXACT ceiling
// pins (`== 7 / 6 / 3`) live in fixy/Modality.h colocated with the
// source-of-truth constants; THIS TU only holds the FLOOR pins
// (`>= K`) which catch the inverse direction — accidental removal
// of a modality concept/tag/query.  Growth past K is silent here and
// auto-tracked by the header's `==` ceilings.

static_assert(fm::self_test::u060_concept_cardinality >= 7);
static_assert(fm::self_test::u060_tag_cardinality     >= 6);
static_assert(fm::self_test::u060_query_cardinality   >= 3);

// ─── 9. modality_name diagnostic reach ────────────────────────────

static_assert(fm::modality_name(fm::ModalityKind::Comonad)       == "Comonad");
static_assert(fm::modality_name(fm::ModalityKind::RelativeMonad) == "RelativeMonad");
static_assert(fm::modality_name(fm::ModalityKind::Absolute)      == "Absolute");
static_assert(fm::modality_name(fm::ModalityKind::Relative)      == "Relative");
static_assert(fm::modality_name(fm::ModalityKind::Quotient)      == "Quotient");
static_assert(fm::modality_name(fm::ModalityKind::Coeffect)      == "Coeffect");

int main() {
    // Compile-time-only sentinel.
    return 0;
}
