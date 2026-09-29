#pragma once

// The effect row a channel payload carries.
//
// A stage's admission check asks this of the payload on each of its
// ends: the row the payload needs, weighed against the row the context
// permits.  So the answer for a payload nobody classified decides
// whether an unclassified payload passes every gate downstream or none.
//
// No primary template answers Row<>.  An empty row means "this payload
// needs no capability", so such a default is fail-open: a wrapper that
// nobody classified would satisfy every context.  A class template can
// also be specialized from any translation unit, so a specialization
// ladder is open at both ends.
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
//
// A layer above this one can give the walk rules for families of its
// own.  A rule names a family and the template arguments that hold what
// the family carries.  The walk reads those arguments and no others.
// The session layer gives rules for its message carriers and its
// protocol combinators, and one walk then reads the row of each payload
// of a protocol.  A rule cannot name a family that a roster of this
// header names, so a rule does not change an answer of this header.

#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/FixedArray.h>
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

// A family that a layer above this header names for the walk, and the
// template arguments of its specializations that hold what it carries.
// Bit I of `carried_arguments` names template argument I.  The walk reads
// each named argument that is a type, and no other argument.  A rule with
// no bit set names a family that carries nothing.
struct payload_family_rule {
    std::meta::info family{};
    std::uint64_t carried_arguments = 0;
};

namespace detail {

// ── the three rosters ───────────────────────────────────────────────
//
// Each entry is the reflection of a class TEMPLATE, so one entry covers
// every specialization of that family.  The Graded entry covers every
// band wrapper, because every band wrapper is a Graded spelling.

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
    ^^::fixy::WriteOnce,
    ^^::fixy::FixedArray,
    ^^std::vector,  // the elements; the allocator holds no row
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

// The rule of the layer for the family of `type`, or a rule with a null
// family when the layer has none.
[[nodiscard]] consteval payload_family_rule layer_rule_of(std::meta::info type,
                                                          std::span<const payload_family_rule> layer_rules) noexcept {
    if (!is_specialization(type)) return {};
    const auto family = family_of_payload(type);
    for (const payload_family_rule& rule : layer_rules) {
        if (rule.family == family) return rule;
    }
    return {};
}

// True when no rule of the layer names a family that a roster of this
// header names, and no two rules name one family.
[[nodiscard]] consteval bool layer_rules_are_disjoint(std::span<const payload_family_rule> layer_rules) noexcept {
    for (std::size_t first = 0; first < layer_rules.size(); ++first) {
        const std::meta::info family = layer_rules[first].family;
        for (const auto roster : {std::span<const std::meta::info>{row_carrying_payload_families},
                                  std::span<const std::meta::info>{transparent_payload_families},
                                  std::span<const std::meta::info>{leaf_payload_families}}) {
            for (const auto entry : roster) {
                if (entry == family) return false;
            }
        }
        for (std::size_t second = first + 1; second < layer_rules.size(); ++second) {
            if (layer_rules[second].family == family) return false;
        }
    }
    return true;
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

// The walk.  Each node answers by its kind:
//
//   * a row-carrying family adds its row, and a Computation also hands
//     its payload to the walk;
//   * a transparent family hands its hidden payload to the walk;
//   * a rostered leaf adds nothing;
//   * a family with a rule of the layer hands the arguments that the
//     rule names to the walk;
//   * any other specialization is refused;
//   * any other type hands its components to the walk, and a class that
//     is only declared, or that holds state the walk cannot read, is
//     refused.
//
// Each node is visited once, so a type that reaches itself through a
// pointer ends the walk.  Complexity: linear in the number of distinct
// nodes, times the cost of the visited-list scan and of the scan of the
// layer rules.
[[nodiscard]] consteval PayloadRowWalk walk_payload_row(std::meta::info root,
                                                        std::span<const payload_family_rule> layer_rules = {}) {
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
            } else if (is_rostered_leaf(type)) {
                continue;
            } else if (const payload_family_rule rule = layer_rule_of(type, layer_rules);
                       rule.family != std::meta::info{}) {
                const auto arguments = std::meta::template_arguments_of(std::meta::dealias(type));
                for (std::size_t index = 0; index < arguments.size() && index < 64; ++index) {
                    const bool is_carried = ((rule.carried_arguments >> index) & std::uint64_t{1}) != 0;
                    if (is_carried && std::meta::is_type(arguments[index])) {
                        pending.push_back(refl::node_reached_indirectly(arguments[index]));
                    }
                }
            } else {
                refuse(type);
            }
            continue;
        }

        const bool is_class = std::meta::is_class_type(type) || std::meta::is_union_type(type);
        if (is_class && !std::meta::is_complete_type(type)) {
            refuse(type);
            continue;
        }
        // Only a complete non-specialization gets here, which is the case
        // that the shared predicate reads.
        if (refl::holds_unreadable_state(refl::TypeNode{type, true})) {
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
        "fixy/concurrent/PayloadRow.h, and on no rule of the layer that asks, is refused, and so is a "
        "class that is only declared, or that holds state the walk cannot read, such as a lambda with "
        "captures.  A payload that hides "
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

// The row a payload carries, with the rules of a layer above this header
// for the families that the rosters of this header do not name.
// LayerRules is an array of payload_family_rule with static storage.
template <class T, auto const& LayerRules>
struct payload_row_under {
    static_assert(detail::layer_rules_are_disjoint(LayerRules),
                  "payload_row_under<T, LayerRules>: a rule of the layer names a family that a roster of "
                  "fixy/concurrent/PayloadRow.h names, or two rules name one family.  A rule must not change "
                  "an answer of the rosters, so give each family one rule, outside the rosters.");
    static constexpr detail::PayloadRowWalk walked = detail::walk_payload_row(^^T, LayerRules);
    static_assert(!walked.is_refused, detail::payload_row_refusal(walked));
    using type = detail::row_of_effect_mask_t<walked.effect_mask>;
};

template <class T, auto const& LayerRules>
using payload_row_under_t = typename payload_row_under<T, LayerRules>::type;

// A row-admission check wants the effect row alone, and
// payload_effect_row_t is its name at the call sites that spell it.  A
// pair that keeps a numerical-tolerance grade beside the row answers a
// different question from row admission, so this layer gives the one
// projection.
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
// One Graded entry covers every band, so no band needs an arm of its
// own.
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

// ── a family with a rule of a layer above ───────────────────────────
//
// The rule names argument 0.  The walk reads it, and it does not read
// argument 1, which the rosters would refuse.
template <class Carried, class Named>
struct LayerCarrier {};
inline constexpr payload_family_rule stand_in_layer_rules[] = {
    {.family = ^^LayerCarrier, .carried_arguments = std::uint64_t{1}},
};
static_assert(std::is_same_v<payload_row_under_t<LayerCarrier<BgComp, UnclassifiedWrapper<int>>, stand_in_layer_rules>,
                             eff::Row<eff::Effect::Bg>>,
              "A family with a rule reports the row of the arguments that the rule names, and no other.");
static_assert(std::is_same_v<payload_row_under_t<HoldsTwoRows, stand_in_layer_rules>, payload_row_t<HoldsTwoRows>>,
              "A rule of a layer does not change the answer for a type that the rosters classify.");
static_assert(detail::walk_payload_row(^^LayerCarrier<int, int>).is_refused,
              "Without the rule of its layer, the family is on no roster, and the walk refuses it.");
inline constexpr payload_family_rule overlapping_layer_rules[] = {
    {.family = ^^::fixy::Secret, .carried_arguments = std::uint64_t{0}},
};
static_assert(!detail::layer_rules_are_disjoint(overlapping_layer_rules),
              "A rule that names a rostered family would change an answer of the rosters, so it is refused.");

}  // namespace detail::payload_row_self_test

}  // namespace fixy::concurrent
