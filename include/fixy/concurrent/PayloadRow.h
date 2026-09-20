#pragma once

// The effect row a channel payload carries.
//
// A stage's admission check asks this of the payload on each of its
// ends: the row the payload needs, weighed against the row the context
// permits.  So the answer for a payload nobody classified decides
// whether an unclassified payload passes every gate downstream or none.
//
// Old spelling: payload_row in include/crucible/sessions/
// SessionRowExtraction.h, 36 arms of a class-template specialization
// ladder, one per wrapper, with a primary template answering Row<>.
// That primary is fail-open, and the header said so: "a wrapper with no
// specialisation reports the empty row, which makes an omitted
// specialisation a soundness bug rather than a missing feature: the
// walker then undercounts the effects of anything the unrecognised
// wrapper hides."  An empty row means "this payload needs no
// capability", so a wrapper nobody wrote an arm for satisfied every
// context.  A class template can also be specialized from any
// translation unit, so the ladder was open at both ends.
//
// Here the primary is a compile error naming the type, and the three
// answers are three rosters read by reflection:
//
//   row_carrying   the family carries a row of its own, and the rule
//                  that reads it is written beside the roster entry
//   transparent    the family hides a payload and adds nothing, so the
//                  answer is the answer for what it hides
//   leaf           the family is a value with no row, stated once
//
// A type that is not a specialization at all is a leaf by construction:
// it has no template parameter in which to hide a payload.  A
// specialization whose family is on no roster is refused, and the
// diagnostic carries the type.
//
// Not covered, stated rather than implied: a plain class with an
// effectful MEMBER, as in `struct S { Computation<Row<Bg>, int> c; };`.
// S is not a specialization, so it reads as a leaf and its member's row
// is not seen.  Catching that needs a member walk over arbitrary
// payloads, which is a larger question than this extractor answers; the
// old tree did not catch it either.  What changed is that an
// unclassified WRAPPER is now refused instead of admitted.

#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Saturated.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/algebra/Graded.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace fixy::concurrent {

namespace detail {

// ── the three rosters ───────────────────────────────────────────────
//
// Each entry is the reflection of a class TEMPLATE, so one entry covers
// every specialization of that family.  The Graded entry is why this is
// nine lines rather than thirty-six: every band wrapper is a Graded
// spelling, so they arrive together.

inline constexpr std::meta::info row_carrying_payload_families[] = {
    ^^::foundation::effects::Computation,
    ^^::foundation::effects::Capability,
};

inline constexpr std::meta::info transparent_payload_families[] = {
    ^^::foundation::algebra::Graded,  // every band: DetSafe, HotPath, AllocClass, Wait, ...
    ^^::fixy::Refinement,             // Refined and SealedRefined are one template
    ^^::fixy::Tagged,
    ^^::fixy::Secret,
    ^^::fixy::Stale,
    ^^::fixy::Qtt,  // Linear and Affine are one template
    ^^::fixy::Borrowed,
    ^^::fixy::Monotonic,
    ^^::fixy::AppendOnly,
};

// A family admitted as carrying nothing.  Every entry is a claim that
// the template hides no effect row, and the claim is the reason the
// entry needs a line of its own rather than falling through.
inline constexpr std::meta::info leaf_payload_families[] = {
    ^^::fixy::Saturated,  // a value and a clamped flag; no row
};

[[nodiscard]] consteval bool is_specialization(std::meta::info type) noexcept {
    return std::meta::has_template_arguments(std::meta::dealias(type));
}

[[nodiscard]] consteval std::meta::info family_of_payload(std::meta::info type) noexcept {
    return std::meta::template_of(std::meta::dealias(type));
}

// The three membership questions.  Each reads one roster and nothing
// else, so a family is on a roster because this header lists it.
[[nodiscard]] consteval bool carries_row(std::meta::info type) noexcept {
    if (!is_specialization(type)) return false;
    const auto family = family_of_payload(type);
    for (const auto entry : row_carrying_payload_families) {
        if (entry == family) return true;
    }
    return false;
}

[[nodiscard]] consteval bool is_transparent(std::meta::info type) noexcept {
    if (!is_specialization(type)) return false;
    const auto family = family_of_payload(type);
    for (const auto entry : transparent_payload_families) {
        if (entry == family) return true;
    }
    return false;
}

[[nodiscard]] consteval bool is_rostered_leaf(std::meta::info type) noexcept {
    if (!is_specialization(type)) return false;
    const auto family = family_of_payload(type);
    for (const auto entry : leaf_payload_families) {
        if (entry == family) return true;
    }
    return false;
}

// The three questions asked of a type rather than of a reflection.  A
// constraint cannot spell `f(^^T)` without parenthesizing the
// reflection, so the specializations below name these instead.
template <class T>
[[nodiscard]] consteval bool is_transparent_payload() noexcept {
    return is_transparent(^^T);
}

template <class T>
[[nodiscard]] consteval bool is_leaf_payload() noexcept {
    return is_rostered_leaf(^^T);
}

// A type this extractor can answer for: a non-specialization, or a
// specialization whose family is on exactly one of the three rosters.
// This is the gate, and the primary template's static_assert is its
// only consumer.
template <class T>
[[nodiscard]] consteval bool payload_row_is_classified() noexcept {
    constexpr auto type = ^^T;
    if constexpr (!is_specialization(type)) {
        return true;
    } else {
        return carries_row(type) || is_transparent(type) || is_rostered_leaf(type);
    }
}

// No family may sit on two rosters, or the order of the checks below
// would decide the answer instead of the roster.
[[nodiscard]] consteval bool rosters_are_disjoint() noexcept {
    for (const auto carrying : row_carrying_payload_families) {
        for (const auto transparent : transparent_payload_families) {
            if (carrying == transparent) return false;
        }
        for (const auto leaf : leaf_payload_families) {
            if (carrying == leaf) return false;
        }
    }
    for (const auto transparent : transparent_payload_families) {
        for (const auto leaf : leaf_payload_families) {
            if (transparent == leaf) return false;
        }
    }
    return true;
}

static_assert(rosters_are_disjoint(), "A payload family is on two of the three rosters, so the order in which "
                                      "payload_row checks them decides the answer.  Each family belongs to "
                                      "exactly one: it carries a row, it hides a payload, or it is a leaf.");

}  // namespace detail

// The row a payload carries.
//
// The primary is a hard error rather than an answer.  Its static_assert
// names no type in its text, because the text is fixed; the type is in
// the instantiation note the compiler prints beside it, which is what
// the negative fixture matches on.
template <class T>
struct payload_row {
    static_assert(detail::payload_row_is_classified<T>(),
                  "payload_row<T>: T is a class template specialization whose family is on none of the "
                  "three payload rosters in fixy/concurrent/PayloadRow.h.  A payload that hides another "
                  "type must say what it hides: add the family to transparent_payload_families if it "
                  "unwraps, to row_carrying_payload_families with its rule if it carries a row of its "
                  "own, or to leaf_payload_families if it holds no row at all.  Answering Row<> for an "
                  "unclassified wrapper would let it satisfy every execution context, which is the "
                  "fail-open shape this roster replaced.");

    // Reached only for a non-specialization, which has no template
    // parameter in which to hide a payload.
    using type = ::foundation::effects::Row<>;
};

// Sending a computation says the payload was produced under row R, and
// the receiver inherits the obligation that R was authorized.
template <class R, class T>
struct payload_row<::foundation::effects::Computation<R, T>> {
    using type = R;
};

// Sending a capability conveys the effect itself, because the receiver
// gains the authority to perform it.  The source is informational at
// the row level.
template <::foundation::effects::Effect E, class S>
struct payload_row<::foundation::effects::Capability<E, S>> {
    using type = ::foundation::effects::Row<E>;
};

// Every transparent family, in one partial specialization rather than
// one per wrapper.  The constraint is what keeps this from swallowing an
// unclassified specialization: a family absent from the roster does not
// match here and falls to the primary, where it is refused.
template <class T>
    requires(detail::is_transparent_payload<T>())
struct payload_row<T> {
    using type = typename payload_row<typename T::value_type>::type;
};

// Every rostered leaf, likewise in one specialization.
template <class T>
    requires(detail::is_leaf_payload<T>())
struct payload_row<T> {
    using type = ::foundation::effects::Row<>;
};

template <class T>
using payload_row_t = typename payload_row<T>::type;

// The two consumers want different things.  A row-admission check wants
// the effect row alone and uses payload_effect_row_t; the old tree kept
// a second spelling that preserved a numerical-tolerance grade beside
// the row, for an audit of the full payload contract.  Nothing in the
// new tree reads that second spelling, and a grade-preserving pair is a
// different question from row admission, so this layer ships the one
// projection and payload_effect_row_t is its name at the call sites that
// already spell it.
template <class T>
using payload_effect_row_t = payload_row_t<T>;

namespace detail::payload_row_self_test {

namespace eff = ::foundation::effects;

// ── leaves by construction: not specializations ──────────────────────
static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<double>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<char>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<void*>, eff::Row<>>);

struct UserPod {
    int x = 0;
    double y = 0.0;
};
static_assert(std::is_same_v<payload_row_t<UserPod>, eff::Row<>>);

// ── a computation carries its own row ───────────────────────────────
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<>, int>>, eff::Row<>>);
static_assert(
    std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, int>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Alloc, eff::Effect::IO>, int>>,
                             eff::Row<eff::Effect::Alloc, eff::Effect::IO>>);

// ── a capability conveys the effect ─────────────────────────────────
static_assert(std::is_same_v<payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg>>,
                             eff::Row<eff::Effect::Alloc>>);
static_assert(std::is_same_v<payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>>, eff::Row<eff::Effect::IO>>);

// ── a transparent wrapper reports what it hides ─────────────────────
//
// One Graded entry covers every band, which is the whole of the
// consolidation: the old tree wrote one arm for each of DetSafe,
// HotPath, AllocClass, CipherTier, Wait, MemOrder, Progress,
// NumericalTier, ResidencyHeat, RecipeSpec and OpaqueLifetime.
using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

static_assert(std::is_same_v<payload_row_t<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, BgComp>>,
                             eff::Row<eff::Effect::Bg>>,
              "A band over an engaged computation must report the computation's row.  A band that reported "
              "the empty row would hide the effect it wraps.");
static_assert(std::is_same_v<payload_row_t<::fixy::Secret<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::fixy::Stale<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::fixy::Secret<int>>, eff::Row<>>);

// A stack of wrappers reports the row at the bottom, however deep.
static_assert(std::is_same_v<payload_row_t<::fixy::Secret<::fixy::Stale<BgComp>>>, eff::Row<eff::Effect::Bg>>);
static_assert(
    std::is_same_v<payload_row_t<::fixy::Stale<::fixy::Secret<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, BgComp>>>>,
                   eff::Row<eff::Effect::Bg>>);

// ── a rostered leaf reports nothing, and says so deliberately ───────
static_assert(std::is_same_v<payload_row_t<::fixy::Saturated<unsigned>>, eff::Row<>>);

// ── the gate itself ─────────────────────────────────────────────────
//
// What the gate admits, and what it refuses.  The refusals are the
// property the roster exists for: the primary template is not an answer.
static_assert(payload_row_is_classified<int>());
static_assert(payload_row_is_classified<UserPod>());
static_assert(payload_row_is_classified<eff::Computation<eff::Row<>, int>>());
static_assert(payload_row_is_classified<eff::Capability<eff::Effect::Alloc, eff::Bg>>());
static_assert(payload_row_is_classified<::fixy::Secret<int>>());
static_assert(payload_row_is_classified<::fixy::Saturated<unsigned>>());

// A template the rosters do not name is refused, whatever it holds.
template <class T>
struct UnclassifiedWrapper {
    using value_type = T;
};
static_assert(!payload_row_is_classified<UnclassifiedWrapper<int>>(),
              "A class template on none of the three rosters must be refused.  Admitting it as a leaf is "
              "the fail-open shape: it would report the empty row and satisfy every context, including "
              "when it hides an engaged computation.");
static_assert(!payload_row_is_classified<UnclassifiedWrapper<BgComp>>(),
              "The refusal must not depend on what the unclassified wrapper holds.  A wrapper hiding an "
              "engaged computation is the case that matters, and it is refused for the same reason as one "
              "hiding an int: nothing said what it hides.");

}  // namespace detail::payload_row_self_test

}  // namespace fixy::concurrent
