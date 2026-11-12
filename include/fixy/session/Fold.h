#pragma once

// The transition algebra: one fold over a protocol spine.
//
// A protocol is a type built from combinators.  Duality, sequential
// composition, well-formedness and the refinement preorder are each an
// algebra that this header folds over that type.  The fold decomposes a
// node in one place, so a combinator is known to every operation, or to
// none.
//
// This algebra is a peer of Graded and not an instance of it.  A graded
// value carries one grade, and its order is covariant in that grade.
// The refinement preorder of a protocol has mixed variance: a payload
// that a node sends is covariant, and a payload that a node receives is
// contravariant.  A single lattice order cannot state that, so each
// registration states the variance of each of its positions.
//
// fixy/session/Registry.h gives the registration contract, the seal and
// the coherence of a registration.  This header gives the node of a type,
// the members of a node, the fold, and the algebras.  fixy/session/Wire.h
// gives the wire word of a choice, and fixy/session/Refinement.h gives the
// refinement preorder over the type graph.
//
// ── Branches and labels ───────────────────────────────────────────────
//
// An endpoint picks a label branch with a wire word, and the peer
// dispatches on that word.  The word comes from one of two sources:
//
//   keyed       Each label branch names a label key.  The word of a
//               branch is the label word of its key: the stable type id
//               of the key, with the top bit set.  Both endpoints read
//               the word from the type of the label, so an order of the
//               branches is not part of the wire.
//   positional  No label branch names a key.  The word of a branch is
//               its position.  A different order of the same branches is
//               then a different choice.
//
// The top bit keeps the two kinds of word apart: a position never has
// it, and a label word always has it.  A label word is stable where the
// stable type id is stable: across the translation units of one build,
// and across rebuilds with one toolchain (foundation/reflect/Hash.h).
//
// A branch that is no label has no word on the wire.  The endpoint
// enters it on an event, for example the detection of a crash, and
// finds it by its payload.  So refinement matches such a branch by its
// payload, wherever it stands.  Seven rules keep the kinds apart:
//
//   1. Each choice has one label branch or more.  An empty internal
//      choice has no branch to pick, and an empty external choice has
//      no label the peer can send, so a handle there is stuck.  Under
//      the branch rule such a choice also refines each larger internal
//      choice, and a substitute of that type never sends.  The choice
//      types of Gay and Hole (2005) have one label or more.
//   2. In an input choice, no label branch follows a branch that is no
//      label, so the position of each label branch is its position among
//      the label branches.  An output choice has no branch that is no
//      label.
//   3. No two branches that are no label receive the same payload, and
//      no two label branches name the same `label_key`, so each match
//      is unique.
//   4. Either each label branch of a choice names a key, or none does.
//      A choice that mixes the two has no single kind of wire word.
//   5. No two label words of one choice are equal.  Two distinct stable
//      ids can have one word, and the peer would enter the wrong branch.
//   6. In a keyed choice, each label branch is its label step, with no
//      wrapper and no binder above it.  The endpoint enters a keyed
//      branch past its label step, so nothing may stand above that step.
//      The label step of a branch that starts with a binder is also the
//      entry of the loop, where it is a choice of its own.
//   7. Each label key of a choice has a stable identity
//      (foundation/reflect/Hash.h), so it has a label word.  A closure
//      type, an unnamed class and a class with internal linkage print a
//      name that is not a function of the type.  Two such keys can print
//      one name and share a word, so a choice that names one is not
//      well-formed, and the check reads no word.  A keyed step outside a
//      choice reads its word when it sends, and the stable id refuses
//      such a key there.
//
// A keyed step is a choice with one branch.  In the papers a message is
// a choice with one label, p⊕q:m(B) and p&q:m(B) (Barwell, Hou, Yoshida
// and Zhou, LMCS 2025, Definition 4.9), so a keyed step and a keyed
// choice of one branch are one type.  A step whose payload names a label
// key, anywhere but as a branch of a choice, is read as the keyed choice
// of its registration with the step as its one branch.  The type graph
// below reads it so, and the relation pairs it with a choice by label.
// The endpoint sends its label word, as a choice sends the word of the
// branch it picks.  An input step takes the note that the payload rule
// names.  A step whose payload names no label is a plain step.
//
// ── Recursion ─────────────────────────────────────────────────────────
//
// Recursion is iso-recursive.  A back node binds the nearest binder
// above it, and the fold never folds a body back into its binder.  The
// refinement walk follows a back node to its binder and then into the
// body, which is an unfold, and it never does the inverse (Ekici,
// Kamegai and Yoshida, ITP 2025, Remark 17).  A protocol is well-formed
// only when each back node is guarded: some step or choice lies between
// it and its binder.  Without that guard the unfold does not stop.
//
// ── No entry point for a specialization ───────────────────────────────
//
// The fold recurses in place.  It asks no template of a layer for the
// answer at a child, so no specialization of a template of the layer
// changes an answer at any depth.  The registry alone decides what a
// node is, and a seal closes the registry.
//
// Complexity: a translation unit reads the view of each type once, and
// each fold computes its result for each distinct (type, algebra,
// context) once.  So a fold is linear in the distinct subtrees of the
// protocol, and a subtree that two protocols share costs one read in the
// second.

#include <fixy/session/Registry.h>

#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <type_traits>

namespace fixy::session::transition {

// ── One node ──────────────────────────────────────────────────────────

// The branches live in static storage, so a node is a structural type,
// and decompose keeps one node for each type.
struct node {
    std::meta::info type{};
    bool is_registered = false;
    bool is_malformed = false;
    combinator entry{};
    std::meta::info payload{};
    std::meta::info next{};
    std::meta::info annotation{};
    std::meta::info value{};
    static_run<std::meta::info> branches{};
};

namespace detail {

// The registered view of one type, read now.  `type` is a dealiased
// type.  decompose below keeps the answer for each type.
[[nodiscard]] consteval node decompose_now(std::meta::info registry, std::meta::info type) {
    node result{};
    result.type = type;
    const combinator_lookup lookup = lookup_combinator(registry, shape_of(result.type));
    if (!lookup.is_found) return result;
    result.entry = lookup.entry;
    result.is_registered = true;
    const bool has_arguments = std::meta::has_template_arguments(result.type);
    std::vector<std::meta::info> arguments;
    if (has_arguments) arguments = std::meta::template_arguments_of(result.type);
    const std::meta::info* const argument = arguments.data();
    const std::size_t count = arguments.size();
    switch (result.entry.kind) {
        case shape_kind::terminal:
        case shape_kind::back:
            result.is_malformed = has_arguments;
            break;
        case shape_kind::step:
            result.is_malformed = count != 2 || !std::meta::is_type(argument[0]) || !std::meta::is_type(argument[1]);
            if (!result.is_malformed) {
                result.payload = std::meta::dealias(argument[0]);
                result.next = std::meta::dealias(argument[1]);
            }
            break;
        case shape_kind::binder:
        case shape_kind::marker:
            result.is_malformed = count != 1 || !std::meta::is_type(argument[0]);
            if (!result.is_malformed) result.next = std::meta::dealias(argument[0]);
            break;
        case shape_kind::wrapper:
            result.is_malformed = count != 2 || std::meta::is_type(argument[0]) || !std::meta::is_type(argument[1]);
            if (!result.is_malformed) {
                result.value = argument[0];
                result.next = std::meta::dealias(argument[1]);
            }
            break;
        case shape_kind::choice: {
            std::size_t first = 0;
            if (count != 0 && result.entry.annotation != std::meta::info{} && std::meta::is_type(argument[0])
                && shape_of(argument[0]) == result.entry.annotation) {
                result.annotation = std::meta::dealias(argument[0]);
                first = 1;
            }
            stack<std::meta::info> branches{};
            make_room(branches, count - first);
            std::meta::info* const branch = branches.items;
            std::size_t made = 0;
            for (std::size_t index = first; index < count; ++index) {
                if (!std::meta::is_type(argument[index])) {
                    result.is_malformed = true;
                    break;
                }
                branch[made++] = std::meta::dealias(argument[index]);
            }
            result.branches = to_static_run(std::span<const std::meta::info>{branch, made});
            break;
        }
        default:
            result.is_malformed = true;
            break;
    }
    if (result.is_malformed) result.is_registered = false;
    return result;
}

}  // namespace detail

// The registered view of one type, read once per translation unit.
template <std::meta::info Registry, std::meta::info Type>
inline constexpr node node_v = detail::decompose_now(Registry, Type);

// The registered view of one type.  A type whose shape has no
// registration, or whose arguments do not match the layout of its kind,
// is not registered.  The view of a type is read once per translation
// unit, so each walk that meets a type again, in the same protocol or in
// another one, reads the kept view.  Complexity: linear in the number of
// arguments at the first read of a type.
[[nodiscard]] consteval node decompose(std::meta::info registry, std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    if (!std::meta::is_type(dealiased)) {
        node result{};
        result.type = dealiased;
        return result;
    }
    return std::meta::extract<node>(std::meta::substitute(
        ^^node_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(dealiased)}));
}

[[nodiscard]] consteval bool is_registered(std::meta::info registry, std::meta::info type) {
    return decompose(registry, type).is_registered;
}

// ── The members of a node ─────────────────────────────────────────────
//
// The fold reads a node from its template arguments.  A reader of a layer
// can read a nested member of the node instead, such as `next`, and an
// explicit specialization of a combinator can give that member a value
// that the arguments do not give.  A member claim states the value that
// the arguments give to one member: a type for an alias member, or a
// reflected constant for a static data member.  members_agree checks each
// claim and the direct bases of the node.  A specialization that agrees
// with each claim is harmless, and it passes.
//
// A layer states its claims in one node reader: a function that is not a
// template, so no program can specialize a claim away.  The reader also
// names the children of the node that a reader of the layer can step to,
// and first_disagreeing_node walks them.

struct member_claim {
    std::string_view name{};
    std::meta::info value{};
};

// The claims, the direct bases and the children that a node reader gives
// for one node.  Each list is a stack, so a reader changes no vector.  A
// stack has no copy, so a reader fills the node_members of its caller.
struct node_members {
    bool is_node = false;
    stack<member_claim> claims{};
    stack<std::meta::info> bases{};
    stack<std::meta::info> children{};
};

using node_reader = void (*)(std::meta::info, node_members&);

// What a member list says about one claim.  `holds`: the first member with
// the identifier of the claim has the claimed value.  `differs`: that member
// has a different value.  `unlisted`: no member has the identifier.
enum class claim_reading : std::uint8_t {
    holds,
    differs,
    unlisted,
};

// The reading of `claim` in the `count` members at `members`.  A member has
// the claimed value when it is an alias of the claimed type, or a static
// data member with the claimed constant.  An inherited member is not a
// member here.  The list comes from a walk that sees private members, so
// the reflection of a member stays in this function and in members_agree.
[[nodiscard]] consteval claim_reading read_claim(const std::meta::info* members, std::size_t count,
                                                 const member_claim& claim) {
    for (std::size_t place = 0; place < count; ++place) {
        const std::meta::info member = members[place];
        if (!std::meta::has_identifier(member) || std::meta::identifier_of(member) != claim.name) continue;
        const bool is_claimed_value =
            std::meta::is_type(claim.value)
                ? std::meta::is_type_alias(member) && std::meta::dealias(member) == std::meta::dealias(claim.value)
                : std::meta::is_variable(member) && std::meta::constant_of(member) == claim.value;
        return is_claimed_value ? claim_reading::holds : claim_reading::differs;
    }
    return claim_reading::unlisted;
}

// ── A member behind a closure ─────────────────────────────────────────
//
// The members_of of GCC 16 lists no alias member whose target is a closure
// type.  So a claim on such a member reads `unlisted`, and a node with a
// closure argument, such as a keyed message with a closure label or a step
// with a closure payload, would disagree with its own arguments.  The node
// is then read through a copy: the same template, with a stand-in in place
// of each argument that is the closure.  The copy lists the member, and the
// claims of the node hold on the copy at the stand-in.
//
// The copy and the node must come from one definition, at one source
// location.  An explicit specialization for the closure has its own
// location, so it cannot hide a member that disagrees behind the closure.
// A claim whose value holds the closure inside another type does not hold on
// the copy, so such a node stays refused.

[[nodiscard]] consteval bool members_agree(std::meta::info type, const node_members& view);

namespace detail {

// The argument of a copy in place of a closure.
struct closure_stand_in {};

// The place of the definition of a class: its file, line and column.
struct definition_place {
    const char* file = "";
    std::uint_least32_t line = 0;
    std::uint_least32_t column = 0;
};

[[nodiscard]] consteval definition_place place_of(std::meta::info type) {
    return {std::meta::source_location_of(type).file_name(), std::meta::source_location_of(type).line(),
            std::meta::source_location_of(type).column()};
}

// True when the two classes come from one definition: the same file, line
// and column.
[[nodiscard]] consteval bool is_same_definition(std::meta::info left, std::meta::info right) {
    const definition_place left_place = place_of(left);
    const definition_place right_place = place_of(right);
    if (left_place.line != right_place.line || left_place.column != right_place.column) return false;
    const char* left_file = left_place.file;
    const char* right_file = right_place.file;
    for (; *left_file != '\0' && *left_file == *right_file; ++left_file, ++right_file) {}
    return *left_file == *right_file;
}

// The member answer of `type` when its claim on `closure` reads unlisted.
// The copy is `type` with closure_stand_in in place of each template
// argument that is `closure`.  No copy exists when no argument is
// `closure`, when `closure` is the stand-in, or when the template refuses
// the copied arguments.  Each copy has one argument fewer that is not the
// stand-in, so a chain of copies ends.  The members of the copy must agree
// with the claims and the bases of `view`, each with the stand-in in place
// of the closure.  Complexity: one member walk of the copy, and one more
// copy for each other closure argument.
[[nodiscard]] consteval bool agrees_behind_closure(std::meta::info type, const node_members& view,
                                                   std::meta::info closure) {
    if (closure == ^^closure_stand_in || !std::meta::has_template_arguments(type)) return false;
    const std::vector<std::meta::info> arguments = std::meta::template_arguments_of(type);
    const std::meta::info* const argument = arguments.data();
    const std::size_t argument_count = arguments.size();
    stack<std::meta::info> copied_arguments{};
    bool is_replaced = false;
    for (std::size_t place = 0; place < argument_count; ++place) {
        const bool is_closure = std::meta::is_type(argument[place]) && std::meta::dealias(argument[place]) == closure;
        push(copied_arguments, is_closure ? ^^closure_stand_in : argument[place]);
        is_replaced = is_replaced || is_closure;
    }
    const std::meta::info shape = std::meta::template_of(type);
    if (!is_replaced || !std::meta::can_substitute(shape, copied_arguments)) return false;
    const std::meta::info copy = std::meta::substitute(shape, copied_arguments);
    if (!is_same_definition(type, copy)) return false;
    // size_of completes the copy, so that members_agree reads its members.
    if (std::meta::size_of(copy) == 0 || !std::meta::is_complete_type(copy)) return false;
    node_members copied{};
    copied.is_node = view.is_node;
    for (std::size_t place = 0; place < view.claims.top; ++place) {
        const member_claim& claim = view.claims.items[place];
        const bool is_closure = std::meta::is_type(claim.value) && std::meta::dealias(claim.value) == closure;
        push(copied.claims, member_claim{claim.name, is_closure ? ^^closure_stand_in : claim.value});
    }
    for (std::size_t place = 0; place < view.bases.top; ++place) {
        const std::meta::info base = view.bases.items[place];
        push(copied.bases, std::meta::dealias(base) == closure ? ^^closure_stand_in : base);
    }
    return members_agree(copy, copied);
}

}  // namespace detail

// True when each member that a claim of `view` names exists in `type` with
// the claimed value, and the direct bases of `type` are exactly the bases
// of `view`, in order.  A type that is only declared in this translation
// unit has no member that a reader can read, so it agrees.  A claim on a
// member that the list does not show is read behind a closure, as the
// section above states.  The walk reads the members of `type` one time for
// all the claims.  Complexity: linear in the members of `type` for each
// claim.
[[nodiscard]] consteval bool members_agree(std::meta::info type, const node_members& view) {
    if (!std::meta::is_complete_type(type)) return true;
    const std::size_t claim_count = view.claims.top;
    if (claim_count != 0) {
        const std::vector<std::meta::info> members =
            std::meta::members_of(type, std::meta::access_context::unchecked());
        const std::meta::info* const member_list = members.data();
        const std::size_t member_count = members.size();
        const member_claim* const claim = view.claims.items;
        for (std::size_t place = 0; place < claim_count; ++place) {
            const claim_reading reading = read_claim(member_list, member_count, claim[place]);
            if (reading == claim_reading::unlisted && std::meta::is_type(claim[place].value)) {
                return detail::agrees_behind_closure(type, view, std::meta::dealias(claim[place].value));
            }
            if (reading != claim_reading::holds) return false;
        }
    }
    const std::vector<std::meta::info> actual = std::meta::bases_of(type, std::meta::access_context::unchecked());
    const std::size_t base_count = view.bases.top;
    if (actual.size() != base_count) return false;
    const std::meta::info* const base = actual.data();
    const std::meta::info* const expected = view.bases.items;
    for (std::size_t index = 0; index < base_count; ++index) {
        if (std::meta::dealias(std::meta::type_of(base[index])) != std::meta::dealias(expected[index])) return false;
    }
    return true;
}

// The first node of `type`, in the order of a depth-first walk over the
// children that `read` names, whose members or bases disagree with its
// arguments, or the null reflection.  A type that `read` does not know is
// no node, so the walk stops there.  Complexity: linear in the nodes,
// times their members.
[[nodiscard]] consteval std::meta::info first_disagreeing_node(node_reader read, std::meta::info type) {
    const std::meta::info node_type = std::meta::dealias(type);
    node_members view{};
    read(node_type, view);
    if (!view.is_node) return {};
    if (!members_agree(node_type, view)) return node_type;
    for (std::size_t place = 0; place < view.children.top; ++place) {
        const std::meta::info below = first_disagreeing_node(read, view.children.items[place]);
        if (below != std::meta::info{}) return below;
    }
    return {};
}

// The message of a node whose members disagree with its arguments.
[[nodiscard]] consteval std::string_view disagreeing_message(std::string_view prefix, std::meta::info type) {
    std::string text{prefix};
    text += "the node ";
    text += std::meta::display_string_of(type);
    text += " has a nested member or a base that its template arguments do not give.  An explicit "
            "specialization of a combinator must keep each member that a reader reads, or a handle would "
            "step to a protocol that the fold never checked.";
    return std::define_static_string(text);
}

// The type under every registered wrapper at the head of `type`.
[[nodiscard]] consteval std::meta::info strip_wrappers(std::meta::info registry, std::meta::info type) {
    std::meta::info current = std::meta::dealias(type);
    for (;;) {
        const node view = decompose(registry, current);
        if (!view.is_registered || view.entry.kind != shape_kind::wrapper) return current;
        current = view.next;
    }
}

// The shape at the head of `type` under its wrappers.  A recognizer
// compares this with one shape, so it answers false for a type that is
// not a protocol and needs no registration of that type.
[[nodiscard]] consteval std::meta::info head_shape(std::meta::info registry, std::meta::info type) {
    return shape_of(strip_wrappers(registry, type));
}

// The first node of the spine of `type` that has no registration, in
// the order a depth-first walk reaches it, or the null reflection.
// Payloads, values and notes are not nodes.  Complexity: linear.
[[nodiscard]] consteval std::meta::info first_unregistered(std::meta::info registry, std::meta::info type) {
    const node view = decompose(registry, type);
    if (!view.is_registered) return view.type;
    if (view.next != std::meta::info{}) {
        const std::meta::info below = first_unregistered(registry, view.next);
        if (below != std::meta::info{}) return below;
    }
    for (const std::meta::info branch : view.branches) {
        const std::meta::info below = first_unregistered(registry, branch);
        if (below != std::meta::info{}) return below;
    }
    return {};
}

// The first incoherent registration on the spine of `type`, or none.
[[nodiscard]] consteval coherence_verdict first_incoherent(std::meta::info registry, std::meta::info type) {
    const node view = decompose(registry, type);
    if (!view.is_registered) return {};
    const coherence_verdict own = check_combinator(registry, view.entry.shape);
    if (own.reason != incoherence::none) return own;
    if (view.next != std::meta::info{}) {
        const coherence_verdict below = first_incoherent(registry, view.next);
        if (below.reason != incoherence::none) return below;
    }
    for (const std::meta::info branch : view.branches) {
        const coherence_verdict below = first_incoherent(registry, branch);
        if (below.reason != incoherence::none) return below;
    }
    return {};
}

// The text of a refusal for one type.  A static assertion in a layer
// passes it as its message, so the build names the type it refused.
[[nodiscard]] consteval std::string_view unregistered_message(std::string_view prefix, std::meta::info type) {
    std::string text{prefix};
    text += "the combinator ";
    text += std::meta::display_string_of(type);
    text += " has no registration in the transition registry, or its template arguments do not match the "
            "layout of its kind.  Register it next to its declaration.  A combinator the fold does not know "
            "is refused, never passed.";
    return std::define_static_string(text);
}

[[nodiscard]] consteval std::string_view incoherent_message(std::string_view prefix, coherence_verdict verdict) {
    std::string text{prefix};
    text += "the registration of ";
    text += verdict.shape == std::meta::info{} ? std::string_view{"a combinator"}
                                               : std::meta::display_string_of(verdict.shape);
    text += " is incoherent: ";
    text += incoherence_name(verdict.reason);
    text += ".";
    return std::define_static_string(text);
}

// ── The fold ──────────────────────────────────────────────────────────
//
// An algebra is a class with these members:
//
//   using result = ...;      bool or std::meta::info
//   using context = ...;     the state passed down to a child
//   result terminal(node, context, child)
//   result back(node, context, child)
//   result step(node, context, child)
//   result choice(node, context, child)
//   result binder(node, context, child)
//   result wrapper(node, context, child)
//   result marker(node, context, child)
//   result unregistered(node, context)
//
// An algebra has one member for each kind, and the fold calls the member
// of the kind of the node.  An algebra that lacks the member of a kind
// does not compile, so a kind that an algebra does not handle is refused
// and never passed.  `child(type, context)` gives the result for one
// child.  An algebra asks only for the children it needs, so a refusal
// stops the walk.
//
// The algebra and its context are structural types, and the result of an
// algebra depends only on the node, the algebra and the context.  So the
// fold keeps one result for each (type, algebra, context) in a
// translation unit, and a subtree that occurs again, in the same protocol
// or in another one, costs one read.  An algebra keeps its context small
// for this reason: a context that counts without a bound gives a new
// result for each count.

template <class Algebra>
[[nodiscard]] consteval typename Algebra::result fold(std::meta::info registry, std::meta::info type,
                                                      const Algebra& algebra, typename Algebra::context context);

namespace detail {

// The fold at one node, read now.  `type` is dealiased.
template <class Algebra>
[[nodiscard]] consteval typename Algebra::result fold_now(std::meta::info registry, std::meta::info type,
                                                          const Algebra& algebra, typename Algebra::context context) {
    const node view = decompose(registry, type);
    if (!view.is_registered) return algebra.unregistered(view, context);
    const auto child = [&](std::meta::info child_type, typename Algebra::context child_context) {
        return fold(registry, child_type, algebra, child_context);
    };
    switch (view.entry.kind) {
        case shape_kind::terminal:
            return algebra.terminal(view, context, child);
        case shape_kind::back:
            return algebra.back(view, context, child);
        case shape_kind::step:
            return algebra.step(view, context, child);
        case shape_kind::choice:
            return algebra.choice(view, context, child);
        case shape_kind::binder:
            return algebra.binder(view, context, child);
        case shape_kind::wrapper:
            return algebra.wrapper(view, context, child);
        case shape_kind::marker:
            return algebra.marker(view, context, child);
        default:
            break;
    }
    return algebra.unregistered(view, context);
}

}  // namespace detail

// The result of the fold at one node, read once per translation unit.
template <std::meta::info Registry, std::meta::info Type, auto Algebra, auto Context>
inline constexpr typename std::remove_cvref_t<decltype(Algebra)>::result fold_v =
    detail::fold_now(Registry, Type, Algebra, Context);

template <class Algebra>
[[nodiscard]] consteval typename Algebra::result fold(std::meta::info registry, std::meta::info type,
                                                      const Algebra& algebra, typename Algebra::context context) {
    return std::meta::extract<typename Algebra::result>(std::meta::substitute(
        ^^fold_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(std::meta::dealias(type)),
                   std::meta::reflect_constant(algebra), std::meta::reflect_constant(context)}));
}

// ── Algebras ──────────────────────────────────────────────────────────

// The bit that every label word carries and no position carries.
inline constexpr std::uint64_t label_word_bit = std::uint64_t{1} << 63;

// The wire word of a label key: its stable type id, with the top bit
// set.  Complexity: linear in the length of the printed name of the key.
[[nodiscard]] consteval std::uint64_t label_word_of(std::meta::info key) {
    const std::uint64_t id = ::foundation::reflect::stable_type_id(std::meta::dealias(key));
    return id | label_word_bit;
}

// The head of one branch of a choice, under its wrappers.  `payload` is
// the payload of a head step, or null.  `label_key` is the label that
// the payload names through its registration, or null.
// `has_stable_key` is false when that key has no stable identity, and
// `label_word` is the word of a key with one, or zero.  `is_at_root` is
// false when a wrapper or a binder stands above the head step.
struct branch_head {
    bool is_label = true;
    std::meta::info payload{};
    std::meta::info label_key{};
    bool has_stable_key = true;
    std::uint64_t label_word = 0;
    bool is_at_root = true;
};

// Why a choice is not well-formed, by the rules of the section on
// branches and labels.
enum class choice_fault : std::uint8_t {
    none,
    no_label_branch,
    label_after_non_label,
    non_label_in_output,
    repeated_non_label_payload,
    repeated_label_key,
    mixed_label_keys,
    label_word_collision,
    keyed_label_below_root,
    label_key_without_identity,
};

namespace detail {

// A branch is a label unless its head, under wrappers and binders, is an
// input step whose payload a payload registration marks as no label.  A
// binder at the head is looked through, because its first action is the
// first action of its body.  `root` is a dealiased type, and
// head_of_branch below keeps the answer for each type.  Complexity:
// linear in the wrappers and binders at the head.
[[nodiscard]] consteval branch_head head_of_branch_now(std::meta::info registry, std::meta::info root) {
    branch_head result{};
    node head = decompose(registry, strip_wrappers(registry, root));
    while (head.is_registered && head.entry.kind == shape_kind::binder) {
        head = decompose(registry, strip_wrappers(registry, head.next));
    }
    result.is_at_root = head.type == root;
    if (!head.is_registered || head.entry.kind != shape_kind::step) return result;
    result.payload = head.payload;
    const payload_lookup rule = lookup_payload_rule(registry, head.payload);
    if (!rule.is_found) return result;
    result.is_label = head.entry.direction != polarity::input || rule.entry.is_label;
    if (rule.entry.label_key != std::meta::info{} && std::meta::can_substitute(rule.entry.label_key, {head.payload})) {
        result.label_key = std::meta::dealias(std::meta::substitute(rule.entry.label_key, {head.payload}));
        result.has_stable_key = ::foundation::reflect::has_stable_identity(result.label_key);
        if (result.has_stable_key) result.label_word = label_word_of(result.label_key);
    }
    return result;
}

}  // namespace detail

// The head of one branch, read once per translation unit.
template <std::meta::info Registry, std::meta::info Branch>
inline constexpr branch_head branch_head_v = detail::head_of_branch_now(Registry, Branch);

namespace detail {

[[nodiscard]] consteval branch_head head_of_branch(std::meta::info registry, std::meta::info branch) {
    return std::meta::extract<branch_head>(
        std::meta::substitute(^^branch_head_v, {std::meta::reflect_constant(registry),
                                                std::meta::reflect_constant(std::meta::dealias(branch))}));
}

[[nodiscard]] consteval bool is_label_branch(std::meta::info registry, std::meta::info branch) {
    return head_of_branch(registry, branch).is_label;
}

// The label key of a step whose payload names one, or null.  Complexity:
// one read of the payload rules.
[[nodiscard]] consteval std::meta::info step_label_key(std::meta::info registry, const node& step) {
    if (!step.is_registered || step.entry.kind != shape_kind::step) return {};
    const payload_lookup rule = lookup_payload_rule(registry, step.payload);
    if (!rule.is_found || rule.entry.label_key == std::meta::info{}) return {};
    if (step.entry.direction == polarity::input && !rule.entry.is_label) return {};
    if (!std::meta::can_substitute(rule.entry.label_key, {step.payload})) return {};
    return std::meta::dealias(std::meta::substitute(rule.entry.label_key, {step.payload}));
}

[[nodiscard]] consteval std::size_t label_count(std::meta::info registry, const node& choice) {
    std::size_t count = 0;
    for (const std::meta::info branch : choice.branches) {
        if (is_label_branch(registry, branch)) ++count;
    }
    return count;
}

}  // namespace detail

// The first rule of the section on branches and labels that the choice
// breaks, or none.  Complexity: O(b²) for b branches, from the pairwise
// uniqueness rule.
[[nodiscard]] consteval choice_fault fault_of_choice(std::meta::info registry, const node& choice) {
    const std::size_t count = choice.branches.size();
    stack<branch_head> head_storage{};
    for (std::size_t place = 0; place < count; ++place)
        push(head_storage, detail::head_of_branch(registry, choice.branches.data[place]));
    const branch_head* const heads = head_storage.items;
    bool has_label = false;
    bool non_label_seen = false;
    for (std::size_t place = 0; place < count; ++place) {
        if (heads[place].is_label) {
            has_label = true;
            if (non_label_seen) return choice_fault::label_after_non_label;
        } else {
            if (choice.entry.direction == polarity::output) return choice_fault::non_label_in_output;
            non_label_seen = true;
        }
    }
    if (!has_label) return choice_fault::no_label_branch;
    std::size_t keyed = 0;
    std::size_t labels = 0;
    for (std::size_t place = 0; place < count; ++place) {
        if (!heads[place].is_label) continue;
        ++labels;
        if (heads[place].label_key != std::meta::info{}) ++keyed;
    }
    if (keyed != 0 && keyed != labels) return choice_fault::mixed_label_keys;
    for (std::size_t place = 0; place < count; ++place) {
        if (keyed != 0 && heads[place].is_label && !heads[place].is_at_root) {
            return choice_fault::keyed_label_below_root;
        }
        if (heads[place].is_label && !heads[place].has_stable_key) return choice_fault::label_key_without_identity;
    }
    for (std::size_t first = 0; first < count; ++first) {
        for (std::size_t second = first + 1; second < count; ++second) {
            const branch_head& left = heads[first];
            const branch_head& right = heads[second];
            if (!left.is_label && !right.is_label && left.payload == right.payload) {
                return choice_fault::repeated_non_label_payload;
            }
            if (left.is_label && right.is_label && left.label_key != std::meta::info{}
                && left.label_key == right.label_key) {
                return choice_fault::repeated_label_key;
            }
            if (left.is_label && right.is_label && left.label_key != std::meta::info{}
                && left.label_word == right.label_word) {
                return choice_fault::label_word_collision;
            }
        }
    }
    return choice_fault::none;
}

[[nodiscard]] consteval std::string_view choice_fault_name(choice_fault fault) {
    switch (fault) {
        case choice_fault::none:
            return "none";
        case choice_fault::no_label_branch:
            return "the choice has no label branch, so no endpoint can pick a branch or send a label";
        case choice_fault::label_after_non_label:
            return "a label branch follows a branch that is no label, so its wire label is not its position";
        case choice_fault::non_label_in_output:
            return "an internal choice holds a branch that is no label, which no endpoint can pick";
        case choice_fault::repeated_non_label_payload:
            return "two branches that are no label receive the same payload, so the event that enters one is "
                   "ambiguous";
        case choice_fault::repeated_label_key:
            return "two label branches name the same label";
        case choice_fault::mixed_label_keys:
            return "some label branches name a label key and some do not, so the choice has no single kind of wire "
                   "word";
        case choice_fault::label_word_collision:
            return "two label keys of the choice have one label word, so the peer cannot tell the two labels apart";
        case choice_fault::keyed_label_below_root:
            return "a label branch of a keyed choice starts with a wrapper or a binder, not with its label step.  The "
                   "endpoint enters the branch past the label word of that step, so a wrapper or a loop entry above "
                   "the step has no place on the wire";
        case choice_fault::label_key_without_identity:
            return "a label key of the choice has no stable identity, so it has no label word.  A closure type, an "
                   "unnamed class and a class with internal linkage print a name that is not a function of the "
                   "type.  Give the label a declared name in a named namespace";
        default:
            break;
    }
    return "an unknown fault";
}

// True when the node is a step whose payload names a label key.  Outside
// a choice such a step is a choice with one branch.
[[nodiscard]] consteval bool is_keyed_step(std::meta::info registry, const node& step) {
    return detail::step_label_key(registry, step) != std::meta::info{};
}

struct choice_verdict {
    choice_fault fault = choice_fault::none;
    std::meta::info choice{};
};

// The first choice on the spine of `type` that breaks a rule of the
// section on branches and labels, or a verdict with no fault.
// Complexity: the sum of O(b²) over the choices of the spine.
[[nodiscard]] consteval choice_verdict first_faulty_choice(std::meta::info registry, std::meta::info type) {
    const node view = decompose(registry, type);
    if (!view.is_registered) return {};
    if (view.entry.kind == shape_kind::choice) {
        const choice_fault own = fault_of_choice(registry, view);
        if (own != choice_fault::none) return {own, view.type};
    }
    if (view.next != std::meta::info{}) {
        const choice_verdict below = first_faulty_choice(registry, view.next);
        if (below.fault != choice_fault::none) return below;
    }
    for (const std::meta::info branch : view.branches) {
        const choice_verdict below = first_faulty_choice(registry, branch);
        if (below.fault != choice_fault::none) return below;
    }
    return {};
}

namespace detail {

[[nodiscard]] consteval bool value_is_admitted(const node& wrapper) {
    if (wrapper.entry.value_admits == std::meta::info{}) return true;
    if (!std::meta::can_substitute(wrapper.entry.value_admits, {wrapper.value})) return false;
    return std::meta::extract<bool>(std::meta::substitute(wrapper.entry.value_admits, {wrapper.value}));
}

// Puts the branches of a choice after a mapping on `arguments`, with the
// note first when the target shape carries a note of the same template.
consteval void push_choice_arguments(std::meta::info registry, const node& choice, std::meta::info target_shape,
                                     const stack<std::meta::info>& mapped, stack<std::meta::info>& arguments) {
    const combinator_lookup target = lookup_combinator(registry, target_shape);
    if (choice.annotation != std::meta::info{} && target.is_found
        && target.entry.annotation == choice.entry.annotation) {
        push(arguments, choice.annotation);
    }
    for (const std::meta::info branch : mapped)
        push(arguments, branch);
}

// The choice of `target_shape` over the mapped branches of `choice`.
[[nodiscard]] consteval std::meta::info rebuild_choice(std::meta::info registry, const node& choice,
                                                       std::meta::info target_shape,
                                                       const stack<std::meta::info>& mapped) {
    stack<std::meta::info> arguments{};
    push_choice_arguments(registry, choice, target_shape, mapped, arguments);
    return std::meta::substitute(target_shape, arguments);
}

}  // namespace detail

// True for a node where a protocol may stop: a terminal, under any
// wrappers.  A marker is a place on the spine, not a stop.
struct terminal_algebra {
    using result = bool;
    using context = int;

    template <class Child>
    consteval bool terminal(const node&, context, const Child&) const {
        return true;
    }
    template <class Child>
    consteval bool back(const node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool step(const node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool choice(const node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool binder(const node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool wrapper(const node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool marker(const node&, context, const Child&) const {
        return false;
    }
    consteval bool unregistered(const node&, context) const { return false; }
};

// True when a choice somewhere on the spine has no label branch.  A
// handle at such a choice is stuck: an empty internal choice has no
// branch to pick, and an empty external choice has no label the peer
// can send.  A payload that is a protocol becomes a session of its own,
// where an empty choice leaves its holder stuck, so the walk looks into
// it too.
struct empty_choice_algebra {
    using result = bool;
    using context = int;
    std::meta::info registry{};

    template <class Child>
    consteval bool terminal(const node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool back(const node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool step(const node& view, context ctx, const Child& child) const {
        if (view.entry.payload_is_protocol && child(view.payload, ctx)) return true;
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool choice(const node& view, context ctx, const Child& child) const {
        if (detail::label_count(registry, view) == 0) return true;
        for (const std::meta::info branch : view.branches) {
            if (child(branch, ctx)) return true;
        }
        return false;
    }
    template <class Child>
    consteval bool binder(const node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool wrapper(const node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool marker(const node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    consteval bool unregistered(const node&, context) const { return true; }
};

// Well-formedness:
//
//   1. Each back node has a binder above it, and a step or a choice
//      lies between the two (the guard).
//   2. No binder body is a terminal.  A loop over a terminal never
//      reaches its back node.
//   3. No output step sends a payload that a payload registration
//      marks as not sendable.
//   4. Each wrapper value is admitted by the value filter.
//   5. Each choice obeys the six rules of the section on branches and
//      labels.
//   6. Each step whose payload names a label has a keyed choice in its
//      registration, so the step reads as that choice.
//   7. Each combinator is plain.  A combinator that no plain protocol
//      holds is refused at every depth.
//   8. A payload that is a protocol is well-formed outside every binder.
//      The endpoint travels to another participant, so a back node of it
//      cannot name a binder of the carrier.
//
// A back node binds the nearest binder, so a rule asks only whether a
// binder stands above a node, never how many.  The position holds that
// one bit, and the fold keeps at most four results for each type.
struct well_formed_algebra {
    struct position {
        bool is_in_binder = false;
        bool is_guarded = true;
    };
    using result = bool;
    using context = position;
    std::meta::info registry{};

    template <class Child>
    consteval bool terminal(const node& view, context, const Child&) const {
        return view.entry.is_plain;
    }
    template <class Child>
    consteval bool back(const node& view, context ctx, const Child&) const {
        return view.entry.is_plain && ctx.is_in_binder && ctx.is_guarded;
    }
    template <class Child>
    consteval bool step(const node& view, context ctx, const Child& child) const {
        if (!view.entry.is_plain) return false;
        if (view.entry.direction == polarity::output) {
            const payload_lookup rule = lookup_payload_rule(registry, view.payload);
            if (rule.is_found && !rule.entry.is_sendable) return false;
        }
        if (is_keyed_step(registry, view) && view.entry.keyed_choice == std::meta::info{}) return false;
        if (view.entry.payload_is_protocol && !child(view.payload, position{false, true})) return false;
        return child(view.next, position{ctx.is_in_binder, true});
    }
    template <class Child>
    consteval bool choice(const node& view, context ctx, const Child& child) const {
        if (!view.entry.is_plain) return false;
        if (fault_of_choice(registry, view) != choice_fault::none) return false;
        for (const std::meta::info branch : view.branches) {
            if (!child(branch, position{ctx.is_in_binder, true})) return false;
        }
        return true;
    }
    template <class Child>
    consteval bool binder(const node& view, context, const Child& child) const {
        if (!view.entry.is_plain) return false;
        if (fold(registry, view.next, terminal_algebra{}, 0)) return false;
        return child(view.next, position{true, false});
    }
    template <class Child>
    consteval bool wrapper(const node& view, context ctx, const Child& child) const {
        return view.entry.is_plain && detail::value_is_admitted(view) && child(view.next, ctx);
    }
    // A marker carries no message, so it is no guard.
    template <class Child>
    consteval bool marker(const node& view, context ctx, const Child& child) const {
        return view.entry.is_plain && child(view.next, ctx);
    }
    consteval bool unregistered(const node&, context) const { return false; }
};

// The dual: each combinator becomes its registered dual, and each child
// becomes its dual.  A payload and a value do not change.
struct dual_algebra {
    using result = std::meta::info;
    using context = int;
    std::meta::info registry{};

    template <class Child>
    consteval std::meta::info terminal(const node& view, context, const Child&) const {
        return rebuild_nullary(view);
    }
    template <class Child>
    consteval std::meta::info back(const node& view, context, const Child&) const {
        return rebuild_nullary(view);
    }
    template <class Child>
    consteval std::meta::info step(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.dual, {view.payload, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info choice(const node& view, context ctx, const Child& child) const {
        stack<std::meta::info> mapped{};
        for (const std::meta::info branch : view.branches)
            push(mapped, child(branch, ctx));
        return detail::rebuild_choice(registry, view, view.entry.dual, mapped);
    }
    template <class Child>
    consteval std::meta::info binder(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.dual, {child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info wrapper(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.dual, {view.value, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info marker(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.dual, {child(view.next, ctx)});
    }
    consteval std::meta::info unregistered(const node&, context) const { return {}; }

private:
    static consteval std::meta::info rebuild_nullary(const node& view) {
        return view.entry.dual == std::meta::info{} ? view.type : view.entry.dual;
    }
};

// The note that a keyed step stands for as a choice: the note that its
// payload rule names for an input step, or null.
[[nodiscard]] consteval std::meta::info implied_note(std::meta::info registry, const node& step) {
    if (step.entry.direction != polarity::input) return {};
    const payload_lookup rule = lookup_payload_rule(registry, step.payload);
    if (!rule.is_found || rule.entry.input_note == std::meta::info{}) return {};
    if (!std::meta::can_substitute(rule.entry.input_note, {step.payload})) return {};
    return std::meta::dealias(std::meta::substitute(rule.entry.input_note, {step.payload}));
}

// The canonical spelling of a protocol.  A keyed step and the keyed
// choice of that one branch are one type, so the choice of one label
// branch, with no other branch and the note that the step stands for, is
// written as its step.  Every other node keeps its shape.  Two spellings
// of one protocol have one canonical spelling, so a test that compares
// types, such as duality, compares canonical spellings.  A node the
// registry does not know stays as it is.
struct canonical_algebra {
    using result = std::meta::info;
    using context = int;
    std::meta::info registry{};

    template <class Child>
    consteval std::meta::info terminal(const node& view, context, const Child&) const {
        return view.type;
    }
    template <class Child>
    consteval std::meta::info back(const node& view, context, const Child&) const {
        return view.type;
    }
    template <class Child>
    consteval std::meta::info step(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.payload, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info choice(const node& view, context ctx, const Child& child) const {
        if (view.branches.size() == 1) {
            const node head = decompose(registry, view.branches[0]);
            if (is_keyed_step(registry, head) && head.entry.keyed_choice == view.entry.shape
                && implied_note(registry, head) == view.annotation) {
                return child(view.branches[0], ctx);
            }
        }
        stack<std::meta::info> mapped{};
        for (const std::meta::info branch : view.branches)
            push(mapped, child(branch, ctx));
        return detail::rebuild_choice(registry, view, view.entry.shape, mapped);
    }
    template <class Child>
    consteval std::meta::info binder(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info wrapper(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.value, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info marker(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    consteval std::meta::info unregistered(const node& view, context) const { return view.type; }
};

// Sequential composition with a suffix: each terminal that does not
// absorb its suffix becomes the suffix.  A back node stays, because it
// marks a loop-back and not an end.
struct compose_algebra {
    using result = std::meta::info;
    using context = int;
    std::meta::info registry{};
    std::meta::info suffix{};

    template <class Child>
    consteval std::meta::info terminal(const node& view, context, const Child&) const {
        return view.entry.absorbs_suffix ? view.type : suffix;
    }
    template <class Child>
    consteval std::meta::info back(const node& view, context, const Child&) const {
        return view.type;
    }
    template <class Child>
    consteval std::meta::info step(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.payload, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info choice(const node& view, context ctx, const Child& child) const {
        stack<std::meta::info> mapped{};
        for (const std::meta::info branch : view.branches)
            push(mapped, child(branch, ctx));
        return detail::rebuild_choice(registry, view, view.entry.shape, mapped);
    }
    template <class Child>
    consteval std::meta::info binder(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info wrapper(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.value, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info marker(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    consteval std::meta::info unregistered(const node&, context) const { return {}; }
};

// Composition at one branch: the walk passes each step, binder, wrapper
// and marker, and at the first choice it composes the suffix into branch
// `index` alone.  The layer refuses a spine that reaches a terminal or
// a back node first, and an index past the last branch.  The algebra
// answers the null reflection for both.
struct compose_at_choice_algebra {
    using result = std::meta::info;
    using context = int;
    std::meta::info registry{};
    std::size_t index = 0;
    std::meta::info suffix{};

    template <class Child>
    consteval std::meta::info terminal(const node&, context, const Child&) const {
        return {};
    }
    template <class Child>
    consteval std::meta::info back(const node&, context, const Child&) const {
        return {};
    }
    template <class Child>
    consteval std::meta::info step(const node& view, context ctx, const Child& child) const {
        const std::meta::info below = child(view.next, ctx);
        if (below == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.shape, {view.payload, below});
    }
    template <class Child>
    consteval std::meta::info choice(const node& view, context, const Child&) const {
        if (index >= view.branches.size()) return {};
        stack<std::meta::info> mapped{};
        for (std::size_t place = 0; place < view.branches.size(); ++place) {
            push(mapped, place == index ? fold(registry, view.branches[place], compose_algebra{registry, suffix}, 0)
                                        : view.branches[place]);
        }
        return detail::rebuild_choice(registry, view, view.entry.shape, mapped);
    }
    template <class Child>
    consteval std::meta::info binder(const node& view, context ctx, const Child& child) const {
        const std::meta::info below = child(view.next, ctx);
        if (below == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.shape, {below});
    }
    template <class Child>
    consteval std::meta::info wrapper(const node& view, context ctx, const Child& child) const {
        const std::meta::info below = child(view.next, ctx);
        if (below == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.shape, {view.value, below});
    }
    template <class Child>
    consteval std::meta::info marker(const node& view, context ctx, const Child& child) const {
        const std::meta::info below = child(view.next, ctx);
        if (below == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.shape, {below});
    }
    consteval std::meta::info unregistered(const node&, context) const { return {}; }
};

namespace detail {

// True for a node that the walk to the first stop of a spine passes.
[[nodiscard]] consteval bool passes_to_first_stop(const node& view) {
    return view.is_registered
        && (view.entry.kind == shape_kind::step || view.entry.kind == shape_kind::binder
            || view.entry.kind == shape_kind::wrapper || view.entry.kind == shape_kind::marker);
}

}  // namespace detail

// The first node on the spine where composition at a branch stops: the
// first choice, terminal or back node under the steps, binders, wrappers
// and markers at the head.
[[nodiscard]] consteval node first_stop_of_spine(std::meta::info registry, std::meta::info type) {
    node view = decompose(registry, type);
    while (detail::passes_to_first_stop(view))
        view = decompose(registry, view.next);
    return view;
}

// The number of binders on the spine above its first stop.
[[nodiscard]] consteval std::size_t binders_above_first_stop(std::meta::info registry, std::meta::info type) {
    std::size_t depth = 0;
    node view = decompose(registry, type);
    while (detail::passes_to_first_stop(view)) {
        if (view.entry.kind == shape_kind::binder) ++depth;
        view = decompose(registry, view.next);
    }
    return depth;
}

// ── Capture under composition ─────────────────────────────────────────
//
// A back node binds the nearest binder above it.  A suffix with a back
// node that no binder of the suffix binds is open: its loop-back names a
// binder of the context.  Composition puts the suffix where a terminal of
// the prefix stood.  When a binder of the prefix stands above that
// terminal, the open back node binds it, and the suffix loops in a loop
// that it never named.  This is the capture of a free variable under
// substitution.  With a bare back node as the suffix, every exit of a
// loop becomes a loop-back, and the protocol can never end.
//
// A composition without capture keeps terminability: when each node of
// the prefix and of the suffix can reach a terminal, each node of the
// result can too, because a replaced terminal leads into the suffix and
// every other path is unchanged.
//
// The probe reads no hook.  A node that the registry does not know can
// hide a binder or a back node, so the probe answers true at it, and a
// composition over such a node with an open suffix is refused.

// Finds a node by the binders above it.  `target` names what to find: a
// back node with no binder above it, or a terminal that composition
// replaces with a binder above it.  The context is true when a binder
// stands above the node.  Each target asks only that one bit.
struct binding_probe_algebra {
    enum class probe : std::uint8_t {
        open_back,
        bound_terminal
    };
    using result = bool;
    using context = bool;
    probe target = probe::open_back;

    template <class Child>
    consteval bool terminal(const node& view, context is_bound, const Child&) const {
        return target == probe::bound_terminal && is_bound && !view.entry.absorbs_suffix;
    }
    template <class Child>
    consteval bool back(const node&, context is_bound, const Child&) const {
        return target == probe::open_back && !is_bound;
    }
    template <class Child>
    consteval bool step(const node& view, context is_bound, const Child& child) const {
        return child(view.next, is_bound);
    }
    template <class Child>
    consteval bool choice(const node& view, context is_bound, const Child& child) const {
        for (const std::meta::info branch : view.branches) {
            if (child(branch, is_bound)) return true;
        }
        return false;
    }
    template <class Child>
    consteval bool binder(const node& view, context, const Child& child) const {
        return child(view.next, true);
    }
    template <class Child>
    consteval bool wrapper(const node& view, context is_bound, const Child& child) const {
        return child(view.next, is_bound);
    }
    template <class Child>
    consteval bool marker(const node& view, context is_bound, const Child& child) const {
        return child(view.next, is_bound);
    }
    consteval bool unregistered(const node&, context) const { return true; }
};

// True when `type` holds a back node that no binder of `type` binds.
// Complexity: linear in the size of the protocol.
[[nodiscard]] consteval bool has_open_back(std::meta::info registry, std::meta::info type) {
    return fold(registry, type, binding_probe_algebra{binding_probe_algebra::probe::open_back}, false);
}

// True when composition replaces a terminal of `type` that stands below a
// binder, with `binders_above` binders counted above `type` itself.
// Complexity: linear in the size of the protocol.
[[nodiscard]] consteval bool has_bound_terminal(std::meta::info registry, std::meta::info type,
                                                std::size_t binders_above = 0) {
    return fold(registry, type, binding_probe_algebra{binding_probe_algebra::probe::bound_terminal}, binders_above > 0);
}

}  // namespace fixy::session::transition
