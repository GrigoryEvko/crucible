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
// Here a type that cannot be classified is a compile error naming the
// type, and the answer for a specialization comes from three rosters
// read by reflection:
//
//   row_carrying   the family carries a row of its own, and the rule
//                  that reads it is written beside the roster entry
//   transparent    the family hides a payload and adds nothing, so the
//                  answer is the answer for what it hides
//   leaf           the family is a value with no row, stated once
//
// A type that is not a specialization is read for what it holds.  The
// walk in foundation/reflect/TypeComponents.h reads its bases and its
// by-value members, the target of a pointer or a reference, and the
// element of an array, and each of those answers by the same rules.  So
// `struct S { Computation<Row<Bg>, int> c; };` carries Row<Bg>, and a
// scalar, which holds nothing, carries the empty row.
//
// Three shapes are refused, each because the extractor cannot say what
// the shape holds:
//
//   * a specialization whose family is on no roster, wherever the walk
//     reaches it;
//   * a class that is only declared, whose members nobody can read;
//   * a class that holds state the walk cannot read.  That is a class
//     that is not empty and that reflects no base and no data member,
//     which is the shape of a lambda with captures: GCC 16 reflects no
//     capture.
//
// The empty row is therefore an answer the walk derives, never a default
// it falls back to.  The diagnostic names the refused type.
//
// A Computation carries its own row and the row of its payload.  The
// payload is a value the receiver can take out of the carrier, so a
// capability inside a carrier at the empty row still conveys its effect.

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
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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

[[nodiscard]] consteval bool family_is_on(std::meta::info type, std::span<const std::meta::info> roster) noexcept {
    if (!is_specialization(type)) return false;
    const auto family = family_of_payload(type);
    for (const auto entry : roster) {
        if (entry == family) return true;
    }
    return false;
}

// The three membership questions.  Each reads one roster and nothing
// else, so a family is on a roster because this header lists it.
[[nodiscard]] consteval bool carries_row(std::meta::info type) noexcept {
    return family_is_on(type, row_carrying_payload_families);
}

[[nodiscard]] consteval bool is_transparent(std::meta::info type) noexcept {
    return family_is_on(type, transparent_payload_families);
}

[[nodiscard]] consteval bool is_rostered_leaf(std::meta::info type) noexcept {
    return family_is_on(type, leaf_payload_families);
}

// The payload a transparent family hides.  Reading the member alias
// instantiates the wrapper, which each rostered family permits.
template <class T>
using hidden_payload_t = typename T::value_type;

// The answer of the walk: the effects found, as one bit per underlying
// value, or the first type the walk refused.
struct PayloadRowWalk {
    std::uint64_t effect_mask = 0;
    bool is_refused = false;
    std::meta::info refused_type{};
};

[[nodiscard]] consteval std::uint64_t effect_bit(::foundation::effects::Effect effect) noexcept {
    return std::uint64_t{1} << static_cast<unsigned>(std::to_underlying(effect));
}

[[nodiscard]] consteval std::uint64_t effect_mask_of_row(std::meta::info row) {
    std::uint64_t mask = 0;
    for (const std::meta::info effect : std::meta::template_arguments_of(std::meta::dealias(row))) {
        mask |= effect_bit(std::meta::extract<::foundation::effects::Effect>(effect));
    }
    return mask;
}

// A class whose state the walk cannot read: complete, not empty, and
// with no reflected base or data member.
[[nodiscard]] consteval bool holds_unreadable_state(std::meta::info type) {
    if (!std::meta::is_class_type(type) || is_specialization(type)) return false;
    if (!std::meta::is_complete_type(type) || std::meta::is_empty_type(type)) return false;
    const auto unchecked = std::meta::access_context::unchecked();
    return std::meta::bases_of(type, unchecked).empty() && std::meta::nonstatic_data_members_of(type, unchecked).empty();
}

// The walk.  Each node answers by its kind:
//
//   * a row-carrying family adds its row, and a Computation also hands
//     its payload to the walk;
//   * a transparent family hands its hidden payload to the walk;
//   * a rostered leaf adds nothing;
//   * any other specialization is refused;
//   * any other type hands its components to the walk, and a class that
//     is only declared, or that holds state the walk cannot read, is
//     refused.
//
// Each node is visited once, so a type that reaches itself through a
// pointer ends the walk.  Complexity: linear in the number of distinct
// nodes, times the cost of the visited-list scan.
[[nodiscard]] consteval PayloadRowWalk walk_payload_row(std::meta::info root) {
    namespace refl = ::foundation::reflect;
    PayloadRowWalk walked;
    std::vector<refl::TypeNode> pending{refl::TypeNode{refl::bare_type(root), true}};
    std::vector<std::meta::info> visited;
    auto refuse = [&walked](std::meta::info type) consteval {
        walked.is_refused = true;
        walked.refused_type = type;
    };
    while (!pending.empty() && !walked.is_refused) {
        const refl::TypeNode node = pending.back();
        pending.pop_back();
        const std::meta::info type = node.type;
        bool was_visited = false;
        for (const std::meta::info seen : visited) {
            if (seen == type) {
                was_visited = true;
                break;
            }
        }
        if (was_visited) continue;
        visited.push_back(type);

        if (is_specialization(type)) {
            if (carries_row(type)) {
                const auto family = family_of_payload(type);
                const auto arguments = std::meta::template_arguments_of(type);
                if (family == ^^::foundation::effects::Computation) {
                    walked.effect_mask |= effect_mask_of_row(arguments[0]);
                    pending.push_back(refl::node_reached_indirectly(arguments[1]));
                } else {
                    walked.effect_mask |= effect_bit(std::meta::extract<::foundation::effects::Effect>(arguments[0]));
                }
            } else if (is_transparent(type)) {
                pending.push_back(refl::node_reached_indirectly(std::meta::substitute(^^hidden_payload_t, {type})));
            } else if (!is_rostered_leaf(type)) {
                refuse(type);
            }
            continue;
        }

        const bool is_class = std::meta::is_class_type(type) || std::meta::is_union_type(type);
        if (is_class && !std::meta::is_complete_type(type)) {
            refuse(type);
            continue;
        }
        if (holds_unreadable_state(type)) {
            refuse(type);
            continue;
        }
        for (const refl::TypeNode& component : refl::argument_components_of(node)) pending.push_back(component);
        if (is_class) {
            const refl::TypeNode readable{type, true};
            for (const refl::TypeNode& component : refl::member_components_of(readable)) pending.push_back(component);
        }
    }
    return walked;
}

// The row with one effect for each bit of the mask, in the canonical
// order of foundation/effects/Row.h, which sorts by underlying value.
[[nodiscard]] consteval std::meta::info row_of_effect_mask(std::uint64_t mask) {
    std::vector<std::meta::info> effects;
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^::foundation::effects::Effect)) {
        const auto effect = std::meta::extract<::foundation::effects::Effect>(enumerator);
        if ((mask & effect_bit(effect)) != 0) effects.push_back(std::meta::reflect_constant(effect));
    }
    return std::meta::dealias(
        std::meta::substitute(^^::foundation::effects::canonical_row_t,
                              {std::meta::substitute(^^::foundation::effects::Row, effects)}));
}

template <std::uint64_t EffectMask>
using row_of_effect_mask_t = [:row_of_effect_mask(EffectMask):];

// The diagnostic text when the walk refuses a type.  The refused type is
// part of the text, because the walk can refuse a member deep inside the
// payload, which the instantiation note for payload_row<T> does not name.
[[nodiscard]] consteval std::string_view payload_row_refusal(PayloadRowWalk walked) {
    if (!walked.is_refused) return {};
    std::string text{
        "payload_row<T>: T is, or holds, a type this extractor cannot classify.  A class template "
        "specialization whose family is on none of the three payload rosters in "
        "fixy/concurrent/PayloadRow.h is refused, and so is a class that is only declared, or that "
        "holds state the walk cannot read, such as a lambda with captures.  A payload that hides "
        "another type must say what it hides: add the family to transparent_payload_families if it "
        "unwraps, to row_carrying_payload_families with its rule if it carries a row of its own, or to "
        "leaf_payload_families if it holds no row at all.  Answering Row<> for an unclassified type "
        "would let it satisfy every execution context.  The refused type: "};
    text += std::meta::display_string_of(walked.refused_type);
    return std::define_static_string(text);
}

// A type this extractor can answer for.
template <class T>
[[nodiscard]] consteval bool payload_row_is_classified() noexcept {
    return !walk_payload_row(^^T).is_refused;
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
// There is one template and no specialization.  The walk decides every
// answer, and a refusal is a hard error whose text names the refused
// type, which is what the negative fixtures match on.
template <class T>
struct payload_row {
    static constexpr detail::PayloadRowWalk walked = detail::walk_payload_row(^^T);
    static_assert(!walked.is_refused, detail::payload_row_refusal(walked));
    using type = detail::row_of_effect_mask_t<walked.effect_mask>;
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

// ── a plain class answers for what it holds ─────────────────────────
struct HoldsEngaged {
    int tag = 0;
    BgComp held;
};
struct DerivesEngaged : BgComp {};
struct PointsAtCapability {
    eff::Capability<eff::Effect::IO, eff::Init> const* held = nullptr;
};
struct HoldsTwoRows {
    HoldsEngaged first;
    ::fixy::Secret<eff::Capability<eff::Effect::Alloc, eff::Bg>> second[2];
};
struct SelfReferential {
    SelfReferential* next = nullptr;
    int value = 0;
};
static_assert(std::is_same_v<payload_row_t<HoldsEngaged>, eff::Row<eff::Effect::Bg>>,
              "A member's row is the class's row.  The empty row here is the fail-open answer this walk "
              "replaced.");
static_assert(std::is_same_v<payload_row_t<DerivesEngaged>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<PointsAtCapability>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<HoldsTwoRows>, eff::Row<eff::Effect::Alloc, eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<SelfReferential>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>*>, eff::Row<eff::Effect::IO>>);

// A carrier at the empty row still conveys what its payload conveys.
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<>, eff::Capability<eff::Effect::IO, eff::Bg>>>,
                             eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, HoldsEngaged>>,
                             eff::Row<eff::Effect::Bg>>);

// ── what the walk refuses outside the rosters ───────────────────────
struct OnlyDeclared;
static_assert(!payload_row_is_classified<OnlyDeclared*>(),
              "a class that is only declared cannot say what it holds, so a pointer to it is refused.");
inline constexpr auto captures_state = [held = 7] { return held; };
inline constexpr auto captures_nothing = [] { return 7; };
static_assert(!payload_row_is_classified<decltype(captures_state)>(),
              "a lambda with captures holds state the walk cannot read, so it is refused.");
static_assert(std::is_same_v<payload_row_t<decltype(captures_nothing)>, eff::Row<>>);

// A template the rosters do not name is refused, whatever it holds.
template <class T>
struct UnclassifiedWrapper {
    using value_type = T;
};
static_assert(!payload_row_is_classified<UnclassifiedWrapper<int>>(),
              "A class template on none of the three rosters must be refused.  Admitting it as a leaf is "
              "the fail-open shape: it would report the empty row and satisfy every context, including "
              "when it hides an engaged computation.");
struct HoldsUnclassified {
    UnclassifiedWrapper<int> held;
};
static_assert(!payload_row_is_classified<HoldsUnclassified>(),
              "An unclassified wrapper in a member is refused as it is at the root.");
static_assert(!payload_row_is_classified<UnclassifiedWrapper<BgComp>>(),
              "The refusal must not depend on what the unclassified wrapper holds.  A wrapper hiding an "
              "engaged computation is the case that matters, and it is refused for the same reason as one "
              "hiding an int: nothing said what it hides.");

}  // namespace detail::payload_row_self_test

}  // namespace fixy::concurrent
