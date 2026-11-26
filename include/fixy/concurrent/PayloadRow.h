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
// scalar, which holds nothing, carries the empty row.  A bare tag carries
// the row of its effect, because its holder can call each door that takes
// the tag.
//
// Three shapes are refused, each because the extractor cannot say what
// the shape holds:
//
//   * a specialization whose family is on no roster, wherever the walk
//     reaches it;
//   * a class that is only declared, whose members nobody can read;
//   * a class that holds state the walk cannot read.  That is a class
//     that is not empty and that reflects no base and no data member,
//     such as a class that holds only an unnamed bit-field, or a lambda
//     with captures on a compiler without the sixth GCC patch.  With the
//     patch, a lambda is a class whose captures are its members.
//
// The empty row is therefore an answer the walk derives, never a default
// it falls back to.  The diagnostic names the refused type.
//
// An Option is transparent, because it holds its payload by value, so an
// Option of an owner, such as a Box, carries the row of what the owner
// holds.  An Option of an address is refused: its payload is a borrow of an
// object that the payload does not own.  An Option of a borrow of the base,
// such as a Ref or a View, is refused where the walk reaches the borrow,
// whose family is on no roster.
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
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Saturated.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/algebra/Graded.h>
#include <foundation/core/ArenaFrame.h>
#include <foundation/core/Array.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/PackedWord.h>
#include <foundation/core/Ref.h>
#include <foundation/core/RunSet.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Anchor.h>
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
// no bit set and no carried member names a family that carries nothing.
//
// `carried_member` is an alias template `template <class T> using`, or null.
// For a family that carries a type that no argument spells, such as a member
// type of an argument, the walk reads the type that the alias gives for the
// specialization.  A specialization for which the alias gives no type is
// refused.
struct payload_family_rule {
    std::meta::info family{};
    std::uint64_t carried_arguments = 0;
    std::meta::info carried_member{};
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
    ^^::foundation::algebra::Graded,  // every band: DetSafe, AllocClass, Wait, CipherTier, ...
    ^^::fixy::Refinement,  // Refined and SealedRefined are one template
    ^^::fixy::Tagged,
    ^^::fixy::Secret,
    ^^::fixy::Stale,
    ^^::fixy::Qtt,  // Linear and Affine are one template
    ^^::fixy::Monotonic,
    ^^::fixy::AppendOnly,
    ^^::fixy::WriteOnce,
    ^^::foundation::core::FixedArray,  // the elements; the count holds no row
    ^^::foundation::core::HeapArray,  // the elements; the alignment holds no row
    ^^::foundation::core::RunSet,  // the element of each run; the counts hold no row
    ^^::foundation::core::ArenaRunSet,  // as a RunSet; the brand holds no row
    ^^::foundation::core::FrameRunSetSlot,  // as its ArenaRunSet; only a store of a frame fills it
    ^^::foundation::core::ArenaRun,  // the elements of one run of an arena; the brand holds no row
    ^^::foundation::core::ArenaParcel,  // the elements of each run; the arena and the family hold no row
    ^^::foundation::core::Box,  // the one object that the Box owns
    ^^::foundation::core::Option,  // the payload, held by value; an Option of an address is refused
    ^^std::vector,  // the elements; the allocator holds no row
};

// A family admitted as carrying nothing.  Every entry is a claim that
// the template hides no effect row, and the claim is the reason the
// entry needs a line of its own rather than falling through.
inline constexpr std::meta::info leaf_payload_families[] = {
    ^^::fixy::Saturated,  // a value and a clamped flag; no row
    ^^::foundation::core::PackedWord,  // one unsigned word of bit fields; no row
    ^^::foundation::core::BumpArena,  // an arena holds storage and no effect row
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
// instantiates the wrapper, which each rostered family permits.  A
// FixedArray and a HeapArray of the Region family name their element
// element_type, a Box, an Option and an ArenaRun hide the type of their
// payload, and each other family names it value_type.
template <class T>
struct hidden_payload {
    using type = typename T::value_type;
};

template <class T, std::size_t N, class KeyBrand>
struct hidden_payload<::foundation::core::FixedArray<T, N, KeyBrand>> {
    using type = typename ::foundation::core::FixedArray<T, N, KeyBrand>::element_type;
};

template <class T, std::size_t Align>
struct hidden_payload<::foundation::core::HeapArray<T, Align>> {
    using type = typename ::foundation::core::HeapArray<T, Align>::element_type;
};

template <::foundation::core::BoxPayload T>
struct hidden_payload<::foundation::core::Box<T>> {
    using type = T;
};

template <::foundation::core::ViewElement T, ::foundation::brand::IsBrand Brand>
struct hidden_payload<::foundation::core::ArenaRun<T, Brand>> {
    using type = T;
};

template <::foundation::core::ChoicePayload T>
struct hidden_payload<::foundation::core::Option<T>> {
    using type = T;
};

// A parcel hides the elements of each of its runs, as a RunSet does.  It owns
// the arena of the runs and an object of each element type (its owner mark
// by_arguments{}, foundation/Transfer.h), so what it carries is what its
// elements carry.  The walk refuses an element that it refuses alone, such as
// a borrow.  The arena holds storage and no effect row, and so does the
// family tag.
template <class Family, class... Ts>
struct hidden_payload<::foundation::core::ArenaParcel<Family, Ts...>> {
    using type = void(Ts*...);
};

// True when the type is an address: a pointer, a pointer to a member or a
// reference.  An Option holds its payload by value, so an Option of an
// address holds a borrow of an object that the Option does not own.
[[nodiscard]] consteval bool is_address(std::meta::info type) noexcept {
    if (std::meta::is_reference_type(type)) return true;
    const std::meta::info bare = std::meta::dealias(std::meta::remove_cv(std::meta::dealias(type)));
    return std::meta::is_pointer_type(bare) || std::meta::is_member_pointer_type(bare);
}

template <class T>
using hidden_payload_t = typename hidden_payload<T>::type;

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

// The effect that a bare tag gives to its holder, as a mask of one bit, or 0
// for a type that is no bare tag.  The type holds no alias and no qualifier.
// The check file of this header checks the tag of each effect that has one.
[[nodiscard]] consteval std::uint64_t effect_mask_of_bare_tag(std::meta::info type) {
    using ::foundation::effects::Effect;
    if (type == ^^::foundation::core::Alloc) return effect_bit(Effect::Alloc);
    if (type == ^^::foundation::effects::cap::IO) return effect_bit(Effect::IO);
    if (type == ^^::foundation::effects::cap::Block) return effect_bit(Effect::Block);
    if (type == ^^::foundation::effects::cap::Nondet) return effect_bit(Effect::Nondet);
    return 0;
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
//     rule names to the walk, and the type of its carried member;
//   * any other specialization is refused;
//   * a bare tag adds its effect, because a holder of the tag can call
//     each door of the effect;
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
    // The one door for a type that a node holds through a family: an
    // argument, a hidden payload or a carried member.
    auto reach = [&pending](std::meta::info type) consteval { pending.push_back(refl::node_reached_indirectly(type)); };
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
                    reach(arguments[1]);
                } else {
                    walked.effect_mask |= effect_bit(std::meta::extract<::foundation::effects::Effect>(arguments[0]));
                }
            } else if (is_transparent(type)) {
                const std::meta::info hidden = std::meta::substitute(^^hidden_payload_t, {type});
                // An Option of an address carries a borrow of an object that
                // no part of the payload owns, so the walk refuses it.  A
                // borrow of the base, such as a Ref or a View, is on no
                // roster, so the walk refuses an Option of one when it
                // reaches the borrow.
                const bool is_option = family_of_payload(type) == ^^::foundation::core::Option;
                if (is_option && is_address(hidden)) {
                    refuse(type);
                    continue;
                }
                reach(hidden);
            } else if (is_rostered_leaf(type)) {
                continue;
            } else if (const payload_family_rule rule = layer_rule_of(type, layer_rules);
                       rule.family != std::meta::info{}) {
                const auto arguments = std::meta::template_arguments_of(std::meta::dealias(type));
                for (std::size_t index = 0; index < arguments.size() && index < 64; ++index) {
                    const bool is_carried = ((rule.carried_arguments >> index) & std::uint64_t{1}) != 0;
                    if (is_carried && std::meta::is_type(arguments[index])) reach(arguments[index]);
                }
                if (rule.carried_member != std::meta::info{}) {
                    if (!std::meta::can_substitute(rule.carried_member, {type})) {
                        refuse(type);
                        continue;
                    }
                    reach(std::meta::dealias(std::meta::substitute(rule.carried_member, {type})));
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
        walked.effect_mask |= effect_mask_of_bare_tag(type);
        // Only a complete non-specialization gets here, which is the case
        // that the shared predicate reads.
        if (refl::holds_unreadable_state(refl::TypeNode{type, true})) {
            refuse(type);
            continue;
        }
        for (const refl::TypeNode& component : refl::argument_components_of(node))
            pending.push_back(component);
        if (is_class) {
            const refl::TypeNode readable{type, true};
            for (const refl::TypeNode& component : refl::member_components_of(readable))
                pending.push_back(component);
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
    return std::meta::dealias(std::meta::substitute(^^::foundation::effects::canonical_row_t,
                                                    {std::meta::substitute(^^::foundation::effects::Row, effects)}));
}

template <std::uint64_t EffectMask>
using row_of_effect_mask_t = [:row_of_effect_mask(EffectMask):];

// The diagnostic text when the walk refuses a type.  The refused type is
// part of the text, because the walk can refuse a member deep inside the
// payload, which the instantiation note for payload_row<T> does not name.
// The function is a template only so that its text can depend on Anchor
// (foundation/reflect/Anchor.h).  No check reads the text.
template <class Anchor = void>
[[nodiscard]] consteval std::string_view payload_row_refusal(PayloadRowWalk walked) {
    if (!walked.is_refused) return {};
    ::foundation::reflect::anchored_t<^^Anchor, std::string> text{
        "payload_row<T>: T is, or holds, a type this extractor cannot classify.  A class template "
        "specialization whose family is on none of the three payload rosters in "
        "fixy/concurrent/PayloadRow.h, and on no rule of the layer that asks, is refused, and so is a "
        "class that is only declared, or that holds state the walk cannot read, such as a class that "
        "holds only an unnamed bit-field.  A payload that hides "
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

}  // namespace detail

// The row a payload carries.
//
// There is one template and no specialization.  The walk decides every
// answer, and a refusal is a hard error whose text names the refused
// type, which is what the negative fixtures match on.
template <class T>
struct payload_row {
    static constexpr detail::PayloadRowWalk walked = detail::walk_payload_row(^^T);
    static_assert(!walked.is_refused,
                  detail::payload_row_refusal<::foundation::reflect::anchored_t<^^T, void>>(walked));
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
    static_assert(!walked.is_refused,
                  detail::payload_row_refusal<::foundation::reflect::anchored_t<^^T, void>>(walked));
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

}  // namespace fixy::concurrent
