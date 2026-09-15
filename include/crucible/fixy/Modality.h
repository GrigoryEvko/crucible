#pragma once

// These names are also reachable through the algebra namespace. Both
// paths name the same substrate symbols, so open one path per
// translation unit rather than both.

#include <crucible/algebra/Modality.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::modality {

using ::crucible::algebra::ModalityKind;
using ::crucible::algebra::modality_name;
inline constexpr std::size_t modality_kind_count = ::crucible::algebra::modality_kind_count;

template <ModalityKind K>
concept IsModality = ::crucible::algebra::IsModality<K>;

template <ModalityKind K>
concept ComonadModality = ::crucible::algebra::ComonadModality<K>;

template <ModalityKind K>
concept RelativeMonadModality = ::crucible::algebra::RelativeMonadModality<K>;

template <ModalityKind K>
concept AbsoluteModality = ::crucible::algebra::AbsoluteModality<K>;

template <ModalityKind K>
concept RelativeModality = ::crucible::algebra::RelativeModality<K>;

template <ModalityKind K>
concept QuotientModality = ::crucible::algebra::QuotientModality<K>;

template <ModalityKind K>
concept CoeffectModality = ::crucible::algebra::CoeffectModality<K>;

template <ModalityKind K>
inline constexpr bool has_counit_v = ::crucible::algebra::has_counit_v<K>;
template <ModalityKind K>
inline constexpr bool has_unit_v = ::crucible::algebra::has_unit_v<K>;
template <ModalityKind K>
inline constexpr bool has_grade_only_v = ::crucible::algebra::has_grade_only_v<K>;

using Comonad_t = ::crucible::algebra::modality::Comonad_t;
using RelativeMonad_t = ::crucible::algebra::modality::RelativeMonad_t;
using Absolute_t = ::crucible::algebra::modality::Absolute_t;
using Relative_t = ::crucible::algebra::modality::Relative_t;
using Quotient_t = ::crucible::algebra::modality::Quotient_t;
using Coeffect_t = ::crucible::algebra::modality::Coeffect_t;

}  // namespace crucible::fixy::modality

namespace crucible::fixy::modality::self_test {

static_assert(std::is_same_v<::crucible::fixy::modality::ModalityKind, ::crucible::algebra::ModalityKind>,
              "ModalityKind must alias the substrate enum.");

static_assert(::crucible::fixy::modality::ModalityKind::Comonad == ::crucible::algebra::ModalityKind::Comonad);
static_assert(::crucible::fixy::modality::ModalityKind::RelativeMonad
              == ::crucible::algebra::ModalityKind::RelativeMonad);
static_assert(::crucible::fixy::modality::ModalityKind::Absolute == ::crucible::algebra::ModalityKind::Absolute);
static_assert(::crucible::fixy::modality::ModalityKind::Relative == ::crucible::algebra::ModalityKind::Relative);
static_assert(::crucible::fixy::modality::ModalityKind::Quotient == ::crucible::algebra::ModalityKind::Quotient);
static_assert(::crucible::fixy::modality::ModalityKind::Coeffect == ::crucible::algebra::ModalityKind::Coeffect);

static_assert(::crucible::fixy::modality::IsModality<::crucible::fixy::modality::ModalityKind::Comonad>);
static_assert(::crucible::fixy::modality::IsModality<::crucible::fixy::modality::ModalityKind::Coeffect>);

static_assert(::crucible::fixy::modality::ComonadModality<::crucible::fixy::modality::ModalityKind::Comonad>);
static_assert(!::crucible::fixy::modality::ComonadModality<::crucible::fixy::modality::ModalityKind::RelativeMonad>);

static_assert(::crucible::fixy::modality::CoeffectModality<::crucible::fixy::modality::ModalityKind::Coeffect>);
static_assert(!::crucible::fixy::modality::CoeffectModality<::crucible::fixy::modality::ModalityKind::Quotient>);

static_assert(::crucible::fixy::modality::has_counit_v<::crucible::fixy::modality::ModalityKind::Comonad>);
static_assert(::crucible::fixy::modality::has_unit_v<::crucible::fixy::modality::ModalityKind::RelativeMonad>);
static_assert(::crucible::fixy::modality::has_grade_only_v<::crucible::fixy::modality::ModalityKind::Absolute>);
static_assert(!::crucible::fixy::modality::has_counit_v<::crucible::fixy::modality::ModalityKind::Absolute>);

static_assert(std::is_same_v<::crucible::fixy::modality::Comonad_t, ::crucible::algebra::modality::Comonad_t>);
static_assert(std::is_same_v<::crucible::fixy::modality::Coeffect_t, ::crucible::algebra::modality::Coeffect_t>);

static_assert(::crucible::fixy::modality::Comonad_t::kind == ::crucible::fixy::modality::ModalityKind::Comonad);
static_assert(::crucible::fixy::modality::Quotient_t::kind == ::crucible::fixy::modality::ModalityKind::Quotient);

static_assert(sizeof(::crucible::fixy::modality::Comonad_t) == 1);
static_assert(sizeof(::crucible::fixy::modality::RelativeMonad_t) == 1);
static_assert(sizeof(::crucible::fixy::modality::Absolute_t) == 1);
static_assert(sizeof(::crucible::fixy::modality::Relative_t) == 1);
static_assert(sizeof(::crucible::fixy::modality::Quotient_t) == 1);
static_assert(sizeof(::crucible::fixy::modality::Coeffect_t) == 1);

static_assert(::crucible::fixy::modality::modality_name(::crucible::fixy::modality::ModalityKind::Comonad)
              == "Comonad");
static_assert(::crucible::fixy::modality::modality_name(::crucible::fixy::modality::ModalityKind::Coeffect)
              == "Coeffect");

// The exact count is pinned next to the substrate constant. This side
// holds only a lower bound, which catches the opposite mistake of
// removing a modality form.
static_assert(::crucible::fixy::modality::modality_kind_count == ::crucible::algebra::modality_kind_count);
static_assert(::crucible::fixy::modality::modality_kind_count >= 6,
              "The modality form count has fallen below six. A form was removed "
              "without updating the pins that bracket the count.");

// fix-21: all three counts are DERIVED from the namespace.  Each used to be a
// hand-written literal pinned against the same literal, so none of the three
// assertions could fire: adding an eighth concept, a seventh dispatch tag or a
// fourth query would have left every "exactly N" claim standing.  Reflection
// makes the surface itself the catalog — the three member kinds map one-to-one
// onto the three counts, so no separate list has to be kept in step.
//
// The `self_test` namespace this code sits in is also a member of
// fixy::modality::, but it is a namespace, not a concept, type alias or
// variable template, so it lands in none of the three buckets.
enum class ModalitySurfaceKind : std::uint8_t { Concept, Tag, Query };

[[nodiscard]] consteval int count_modality_surface(ModalitySurfaceKind kind) {
    int found = 0;
    for (auto member : std::meta::members_of(^^::crucible::fixy::modality, std::meta::access_context::current())) {
        const bool matches = (kind == ModalitySurfaceKind::Concept)  ? std::meta::is_concept(member)
                             : (kind == ModalitySurfaceKind::Tag)    ? std::meta::is_type_alias(member)
                                                                     : std::meta::is_variable_template(member);
        if (matches) ++found;
    }
    return found;
}

constexpr int u060_concept_cardinality = count_modality_surface(ModalitySurfaceKind::Concept);
constexpr int u060_tag_cardinality = count_modality_surface(ModalitySurfaceKind::Tag);
constexpr int u060_query_cardinality = count_modality_surface(ModalitySurfaceKind::Query);

static_assert(u060_concept_cardinality == 7,
              "The concept surface is one well-formedness gate plus one concept per modality form.");
static_assert(u060_tag_cardinality == 6, "There is one dispatch tag per modality form.");
static_assert(u060_query_cardinality == 3, "There are three compile-time queries over a modality form.");

}  // namespace crucible::fixy::modality::self_test
