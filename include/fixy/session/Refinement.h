#pragma once

// The payload preorder, the type graph and the refinement preorder of the
// transition algebra.
//
// fixy/session/Fold.h gives the node of a type and the fold, and
// fixy/session/Wire.h tells a keyed choice from a positional one.  This
// header builds one type graph for each protocol and walks two graphs
// together for the refinement preorder.
//
// Complexity: refinement visits each pair of graph nodes at most once, so
// it is O(|T|·|U|) on the two type graphs (Udomsrirungruang and Yoshida,
// POPL 2025, section 5).  A graph has one node for each distinct subtree.

#include <fixy/session/Wire.h>

#include <cstddef>
#include <cstdint>
#include <meta>

namespace fixy::session::transition {

// ── The payload preorder ──────────────────────────────────────────────
//
// The reflexive relation that the axioms of one namespace generate.
// Each drop makes the subtype smaller, so the walk stops.  The depth
// bound is a second stop for an axiom that breaks that rule.  A seal of
// ^^subsort_axiom closes the namespace, as it closes a payload registry.
// Complexity: linear in the number of axioms per layer of the subtype.

namespace detail {

[[nodiscard]] consteval bool ask(std::meta::info predicate, const std::vector<std::meta::info>& arguments) {
    if (predicate == std::meta::info{}) return false;
    if (!std::meta::can_substitute(predicate, arguments)) return false;
    return std::meta::extract<bool>(std::meta::substitute(predicate, arguments));
}

inline constexpr std::size_t subsort_depth_bound = 64;

[[nodiscard]] consteval bool subsorts_within(std::meta::info axioms, std::meta::info sub, std::meta::info super,
                                             std::size_t depth);

// The congruence of one axiom on one pair.  A type argument at a
// covariant position recurs into the order, and every other argument is
// compared for identity.  Complexity: linear in the argument count.
[[nodiscard]] consteval bool congruent_within(std::meta::info axioms, const subsort_axiom& axiom, std::meta::info sub,
                                              std::meta::info super, std::size_t depth) {
    if (axiom.congruence == std::meta::info{}) return false;
    if (!std::meta::has_template_arguments(sub) || !std::meta::has_template_arguments(super)) return false;
    if (std::meta::template_of(sub) != axiom.congruence || std::meta::template_of(super) != axiom.congruence) {
        return false;
    }
    const std::vector<std::meta::info> low = std::meta::template_arguments_of(sub);
    const std::vector<std::meta::info> high = std::meta::template_arguments_of(super);
    if (low.size() != high.size()) return false;
    for (std::size_t position = 0; position < low.size(); ++position) {
        const bool both_types = std::meta::is_type(low[position]) && std::meta::is_type(high[position]);
        const std::meta::info left = both_types ? std::meta::dealias(low[position]) : low[position];
        const std::meta::info right = both_types ? std::meta::dealias(high[position]) : high[position];
        const bool is_covariant = position < 64 && ((axiom.covariant >> position) & 1U) != 0;
        if (is_covariant && both_types) {
            if (!subsorts_within(axioms, left, right, depth - 1)) return false;
        } else if (left != right) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval bool subsorts_within(std::meta::info axioms, std::meta::info sub, std::meta::info super,
                                             std::size_t depth) {
    if (sub == super) return true;
    if (depth == 0) return false;
    require_seal_holds(axioms, ^^subsort_axiom);
    std::vector<subsort_axiom> found;
    for (const std::meta::info member : std::meta::members_of(axioms, std::meta::access_context::unchecked())) {
        if (is_registration_of(member, ^^subsort_axiom)) found.push_back(std::meta::extract<subsort_axiom>(member));
    }
    for (const subsort_axiom& axiom : found) {
        if (ask(axiom.weakens, {sub, super})) return true;
    }
    for (const subsort_axiom& axiom : found) {
        if (congruent_within(axioms, axiom, sub, super, depth)) return true;
    }
    for (const subsort_axiom& axiom : found) {
        if (!ask(axiom.drops, {sub}) || axiom.inner == std::meta::info{}) continue;
        if (!std::meta::can_substitute(axiom.inner, {sub})) continue;
        const std::meta::info inner = std::meta::dealias(std::meta::substitute(axiom.inner, {sub}));
        if (subsorts_within(axioms, inner, super, depth - 1)) return true;
    }
    return false;
}

}  // namespace detail

// The answer for one pair, computed once per translation unit.  Declare
// every axiom of a namespace before the first query against it.
template <std::meta::info Axioms, std::meta::info Sub, std::meta::info Super>
inline constexpr bool subsorts_v = detail::subsorts_within(Axioms, Sub, Super, detail::subsort_depth_bound);

[[nodiscard]] consteval bool subsorts(std::meta::info axioms, std::meta::info sub, std::meta::info super) {
    const std::meta::info low = std::meta::dealias(sub);
    const std::meta::info high = std::meta::dealias(super);
    if (low == high) return true;
    return std::meta::extract<bool>(
        std::meta::substitute(^^subsorts_v, {std::meta::reflect_constant(axioms), std::meta::reflect_constant(low),
                                             std::meta::reflect_constant(high)}));
}

// ── The type graph ────────────────────────────────────────────────────
//
// One node per distinct subtree.  A back node points to the binder it
// binds.  A binder points to its body.  A keyed step outside a choice is
// two nodes: the keyed choice of its registration, and the step as the
// one branch of that choice.
//
// Two occurrences of one type share one node when both are branches of a
// choice or neither is, and when the back nodes of the type with no
// binder in the type bind the same binder.  A back node binds the nearest
// binder, so a type with no such back node has the same future at each
// occurrence, and a type with one has the same future under the same
// nearest binder.  Each relation over the graph then gives the answer of
// the graph with one node per occurrence, and it visits each distinct
// subtree once.  A protocol that holds a subtree many times, such as a
// loop unfolded at each of its back nodes, keeps a graph of the size of
// its distinct subtrees.

// `is_label` is the answer of the payload rules for this node as a
// branch, and `head_payload` is the payload of its head step under its
// wrappers, or null.  A branch that is no label is matched by that
// payload.  `label_key` and `label_word` are the label key of this node
// as a branch and the word of that key, or null and zero.
// `has_restricted_payload` is true for a step whose payload a payload
// registration marks as not sendable or as no label.  For a keyed
// choice, `is_keyed` is true, and `first_label` and `label_count` name
// its label branches in the sorted run of the graph, in the order of
// their label words.  `can_end` is true when a path of the graph leads
// from this node to a terminal.
struct graph_node {
    std::meta::info type{};
    combinator entry{};
    std::meta::info payload{};
    std::meta::info value{};
    std::meta::info annotation{};
    std::size_t next = npos;
    std::size_t first_child = 0;
    std::size_t child_count = 0;
    bool is_label = true;
    std::meta::info head_payload{};
    std::meta::info label_key{};
    std::uint64_t label_word = 0;
    bool has_restricted_payload = false;
    bool is_keyed = false;
    std::size_t first_label = 0;
    std::size_t label_count = 0;
    bool can_end = false;
};

// One node that the build made, under the identity that it shares: its
// type, whether it is a branch, and the node of the binder that the back
// nodes of the type with no binder in the type bind, or npos.
struct built_node {
    std::meta::info type{};
    bool is_branch = false;
    std::size_t scope = npos;
    std::size_t index = npos;
};

namespace detail {

// The state of one build.  `built` lists each node that the build made,
// so an occurrence of a subtree that the graph holds reads its node.
// `binders` holds the binders above the node that the build is at, the
// nearest one on top.  `scratch` holds the branches of each choice that
// the build is in, one region for each choice.
struct type_graph {
    stack<graph_node> nodes{};
    stack<std::size_t> children{};
    stack<std::size_t> sorted_labels{};
    std::meta::info unregistered{};
    stack<built_node> built{};
    stack<std::size_t> binders{};
    stack<std::size_t> scratch{};
};

consteval std::size_t add_to_graph(std::meta::info registry, type_graph& graph, std::meta::info type, bool is_branch);

// The nearest binder above the node that the build is at, or npos.
[[nodiscard]] consteval std::size_t nearest_binder(const type_graph& graph) {
    return graph.binders.top == 0 ? npos : graph.binders.items[graph.binders.top - 1];
}

// The node that the graph holds for this identity, or npos.  Complexity:
// linear in the nodes of the graph.  A protocol has few distinct
// subtrees, and each compare is one compare of a reflection.
[[nodiscard]] consteval std::size_t find_built(const type_graph& graph, std::meta::info type, bool is_branch,
                                               std::size_t scope) {
    const built_node* const made = graph.built.items;
    for (std::size_t place = 0; place < graph.built.top; ++place) {
        const built_node& entry = made[place];
        if (entry.type == type && entry.is_branch == is_branch && entry.scope == scope) return entry.index;
    }
    return npos;
}

// The note of the choice that a keyed step stands for: the note that the
// payload rule names for an input step, or none.  A note of a template
// other than the note template of the choice is refused, and the graph
// records the payload as the node it cannot read.
[[nodiscard]] consteval std::meta::info keyed_step_note(std::meta::info registry, type_graph& graph, const node& step,
                                                        const combinator& choice) {
    if (step.entry.direction != polarity::input) return {};
    const payload_lookup rule = lookup_payload_rule(registry, step.payload);
    if (!rule.is_found || rule.entry.input_note == std::meta::info{}) return {};
    if (choice.annotation == std::meta::info{} || !std::meta::can_substitute(rule.entry.input_note, {step.payload})) {
        if (graph.unregistered == std::meta::info{}) graph.unregistered = step.payload;
        return {};
    }
    const std::meta::info note = std::meta::dealias(std::meta::substitute(rule.entry.input_note, {step.payload}));
    if (shape_of(note) != choice.annotation) {
        if (graph.unregistered == std::meta::info{}) graph.unregistered = step.payload;
        return {};
    }
    return note;
}

// A keyed step outside a choice, as the choice of one branch that it
// stands for.  The branch is the step itself, read as a branch.
consteval std::size_t add_keyed_step(std::meta::info registry, type_graph& graph, const node& step) {
    const combinator choice = lookup_combinator(registry, step.entry.keyed_choice).entry;
    const std::size_t here = graph.nodes.top;
    push(graph.nodes, graph_node{});
    graph_node made{};
    made.type = step.type;
    made.entry = choice;
    made.annotation = keyed_step_note(registry, graph, step, choice);
    const branch_head head = head_of_branch(registry, step.type);
    made.is_label = head.is_label;
    made.head_payload = head.payload;
    made.label_key = head.label_key;
    made.label_word = head.label_word;
    const std::size_t branch = add_to_graph(registry, graph, step.type, true);
    made.first_child = graph.children.top;
    made.child_count = 1;
    push(graph.children, branch);
    made.is_keyed = true;
    made.first_label = graph.sorted_labels.top;
    made.label_count = 1;
    push(graph.sorted_labels, branch);
    graph.nodes.items[here] = made;
    return here;
}

// The children of a choice, from region [base, base + count) of the
// scratch stack.  A keyed choice also lists its label branches in the
// order of their label words, by an insertion sort, because a choice has
// few branches.  Complexity: quadratic in the label branches of the choice.
consteval void add_choice_children(type_graph& graph, graph_node& made, std::size_t base, std::size_t count,
                                   bool is_keyed) {
    made.first_child = graph.children.top;
    made.child_count = count;
    make_room(graph.children, count);
    make_room(graph.scratch, count);
    std::size_t* const scratch = graph.scratch.items;
    const graph_node* const nodes = graph.nodes.items;
    for (std::size_t k = 0; k < count; ++k)
        graph.children.items[graph.children.top + k] = scratch[base + k];
    graph.children.top += count;
    if (!is_keyed) return;
    const std::size_t first = base + count;
    std::size_t end = first;
    for (std::size_t k = 0; k < count; ++k) {
        if (nodes[scratch[base + k]].is_label) scratch[end++] = scratch[base + k];
    }
    for (std::size_t outer = first + 1; outer < end; ++outer) {
        for (std::size_t inner = outer; inner > first; --inner) {
            if (nodes[scratch[inner - 1]].label_word <= nodes[scratch[inner]].label_word) break;
            const std::size_t moved = scratch[inner - 1];
            scratch[inner - 1] = scratch[inner];
            scratch[inner] = moved;
        }
    }
    made.is_keyed = true;
    made.first_label = graph.sorted_labels.top;
    made.label_count = end - first;
    make_room(graph.sorted_labels, end - first);
    for (std::size_t place = first; place < end; ++place)
        graph.sorted_labels.items[graph.sorted_labels.top++] = scratch[place];
}

// `is_branch` is true for a branch of a choice, where a keyed step is the
// branch itself and not a choice of its own.  An occurrence whose identity
// the graph holds reads that node, and the build does not enter it again.
// The node takes its place before its children, and the build writes it
// there when its children are made.
consteval std::size_t add_to_graph(std::meta::info registry, type_graph& graph, std::meta::info type, bool is_branch) {
    const node view = decompose(registry, type);
    const std::size_t scope =
        graph.binders.top == 0 || !has_open_back(registry, view.type) ? npos : nearest_binder(graph);
    const std::size_t shared = find_built(graph, view.type, is_branch, scope);
    if (shared != npos) return shared;
    push(graph.built, built_node{view.type, is_branch, scope, graph.nodes.top});
    if (!is_branch && view.is_registered && view.entry.keyed_choice != std::meta::info{}
        && is_keyed_step(registry, view)) {
        return add_keyed_step(registry, graph, view);
    }
    const std::size_t here = graph.nodes.top;
    push(graph.nodes, graph_node{});
    graph_node made{};
    made.type = view.type;
    if (!view.is_registered) {
        if (graph.unregistered == std::meta::info{}) graph.unregistered = view.type;
        graph.nodes.items[here] = made;
        return here;
    }
    made.entry = view.entry;
    made.payload = view.payload;
    made.value = view.value;
    made.annotation = view.annotation;
    const branch_head head = head_of_branch(registry, view.type);
    made.is_label = head.is_label;
    made.head_payload = head.payload;
    made.label_key = head.label_key;
    made.label_word = head.label_word;
    if (view.entry.kind == shape_kind::step) {
        const payload_lookup rule = lookup_payload_rule(registry, view.payload);
        made.has_restricted_payload = rule.is_found && (!rule.entry.is_label || !rule.entry.is_sendable);
    }
    switch (view.entry.kind) {
        case shape_kind::terminal:
            break;
        case shape_kind::back:
            made.next = nearest_binder(graph);
            break;
        case shape_kind::step:
        case shape_kind::wrapper:
        case shape_kind::marker:
            made.next = add_to_graph(registry, graph, view.next, false);
            break;
        case shape_kind::binder:
            push(graph.binders, here);
            made.next = add_to_graph(registry, graph, view.next, false);
            --graph.binders.top;
            break;
        case shape_kind::choice: {
            const std::size_t base = graph.scratch.top;
            for (const std::meta::info branch : view.branches) {
                const std::size_t child = add_to_graph(registry, graph, branch, true);
                push(graph.scratch, child);
            }
            add_choice_children(graph, made, base, graph.scratch.top - base, is_keyed_choice(registry, view));
            graph.scratch.top = base;
            break;
        }
        default:
            break;
    }
    graph.nodes.items[here] = made;
    return here;
}

// Marks each node from which a path of the graph reaches a terminal.  A
// back node leads to its binder, and a free back node leads nowhere.
// A node stands after the parent that made it, so one pass in the reverse
// order settles each edge to a node that its parent made.  Each further
// pass settles one more level of loop-back, or one more edge to a shared
// node that an earlier parent made.  Complexity: O(N²) at worst for N
// nodes.
consteval void mark_can_end(type_graph& graph) {
    graph_node* const nodes = graph.nodes.items;
    const std::size_t* const children = graph.children.items;
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t index = graph.nodes.top; index-- > 0;) {
            graph_node& current = nodes[index];
            if (current.can_end) continue;
            bool reaches = false;
            switch (current.entry.kind) {
                case shape_kind::terminal:
                    reaches = current.entry.shape != std::meta::info{};
                    break;
                case shape_kind::step:
                case shape_kind::wrapper:
                case shape_kind::binder:
                case shape_kind::back:
                case shape_kind::marker:
                    reaches = current.next != npos && nodes[current.next].can_end;
                    break;
                case shape_kind::choice:
                    for (std::size_t k = 0; k < current.child_count && !reaches; ++k) {
                        reaches = nodes[children[current.first_child + k]].can_end;
                    }
                    break;
                default:
                    break;
            }
            if (reaches) {
                current.can_end = true;
                is_changed = true;
            }
        }
    }
}

}  // namespace detail

// A graph in static storage.  The graph of one protocol is built once
// per translation unit, and each relation that reads it shares it.
struct graph_view {
    static_run<graph_node> nodes{};
    static_run<std::size_t> children{};
    static_run<std::size_t> sorted_labels{};
    std::meta::info unregistered{};
};

// The build keeps its runs on stacks, which cannot leave the function, so
// it gives the graph in static storage.
[[nodiscard]] consteval graph_view build_graph(std::meta::info registry, std::meta::info type) {
    detail::type_graph graph{};
    detail::add_to_graph(registry, graph, type, false);
    detail::mark_can_end(graph);
    return graph_view{to_static_run(graph.nodes), to_static_run(graph.children), to_static_run(graph.sorted_labels),
                      graph.unregistered};
}

template <std::meta::info Registry, std::meta::info Type>
inline constexpr graph_view graph_v = build_graph(Registry, Type);

[[nodiscard]] consteval graph_view graph_of(std::meta::info registry, std::meta::info type) {
    return std::meta::extract<graph_view>(std::meta::substitute(
        ^^graph_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(std::meta::dealias(type))}));
}

// The node a walk reaches from `index` when it passes each binder into
// its body and follows each back node to its binder.  A spine that does
// not stop has an unguarded back node, and the answer is npos.
[[nodiscard]] consteval std::size_t settle(const graph_view& graph, std::size_t index) {
    const graph_node* const nodes = graph.nodes.data;
    for (std::size_t budget = graph.nodes.length + 1; budget > 0; --budget) {
        if (index == npos) return npos;
        const shape_kind kind = nodes[index].entry.kind;
        if (kind != shape_kind::binder && kind != shape_kind::back) return index;
        index = nodes[index].next;
    }
    return npos;
}

// ── The refinement preorder ───────────────────────────────────────────
//
// T refines U when a process of type T can stand where a process of
// type U is expected.  The rules, position by position:
//
//   terminal  the same terminal on both sides.
//   step      the same shape.  The payload order follows the payload
//             variance of the shape, then the continuations refine.
//   choice    the same shape, the same note, and the same kind of wire
//             word: both keyed or both positional.  A keyed step outside
//             a choice is the choice of one branch that it stands for,
//             so it pairs with a choice by label.
//             In a keyed choice the label branches pair by label.  Each
//             label of an output choice of T is a label of U, and each
//             label of an input choice of U is a label of T (Gay and
//             Hole, 2005).  Two labels pair when their words and their
//             keys are equal.  Equal words with different keys cannot
//             stand on one wire, so the relation refuses them.
//             In a positional choice the label branches pair by
//             position, because the position is the wire label.  An
//             output choice of T has no more label branches than U, and
//             an input choice of T has no fewer.
//             Each branch that is no label pairs with the branch of the
//             other side that receives the same payload, wherever the
//             two stand.  Rule Sub-& adds two conditions: each such
//             branch of U has its partner in T, and T has no such branch
//             without a partner in U.
//   wrapper   the same shape.  The value order follows the value
//             variance, then the inner types refine.
//   marker    the same shape, then the continuations refine.
//
// A binder and a back node are unfolded before the comparison, so the
// relation is the coinductive one on the infinite unfoldings.  A pair
// that the walk meets a second time holds by assumption.  Every rule is
// a conjunction, so an assumption that later fails stops the whole
// walk, and one visited set serves the walk.
//
// Exit preservation.  The pairs that the walk visits, and the edges
// between them, are the product of T with the peer of U: each run of T
// against that peer follows the edges.  A pair can end when a path of
// the product reaches a pair of terminals.  The relation also requires
// that each visited pair whose node of U can end, as a node of the graph
// of U, can end as a pair.  So a subtype never removes an exit that the
// supertype offers at the same position, and a terminable supertype has
// only terminable subtypes.  This is the fair subtyping of Padovani and
// Zavattaro (TOPLAS 2026), as a sufficient condition that reads the
// product and no set of peers.  It refuses some pairs that fair
// subtyping holds.  It is not closed under duality: a loop that never
// picks its exit does not refine the loop that can, but the dual of the
// second refines the dual of the first, because a receiver that never
// ends keeps no exit.
//
// Complexity: each pair of graph nodes is visited once, so O(|T|·|U|)
// pairs.  The exit check settles the edges of the product in passes, one
// per level of loop-back, so it is O(P·E) at worst for P pairs and E
// edges.

enum class mismatch : std::uint8_t {
    none,
    shape,
    payload,
    branch_count,
    value,
    annotation,
    non_label_branch,
    pure_non_label_choice,
    unguarded,
    unregistered,
    ill_formed,
    missing_non_label_branch,
    loses_termination,
    label_set,
    label_discipline,
    label_word_clash,
};

struct verdict {
    bool holds = false;
    mismatch reason = mismatch::none;
    std::meta::info sub{};
    std::meta::info super{};
};

[[nodiscard]] consteval std::string_view mismatch_name(mismatch reason) {
    switch (reason) {
        case mismatch::none:
            return "none";
        case mismatch::shape:
            return "the two nodes are different combinators";
        case mismatch::payload:
            return "the payloads are not in the payload order";
        case mismatch::branch_count:
            return "the branch counts are in the wrong direction";
        case mismatch::value:
            return "the wrapper values are not in the value order";
        case mismatch::annotation:
            return "the choice notes differ";
        case mismatch::non_label_branch:
            return "an extra input branch of the subtype receives a payload that is no label";
        case mismatch::pure_non_label_choice:
            return "the input choice of the supertype has no label branch";
        case mismatch::unguarded:
            return "a back node is not guarded, so the unfold does not stop";
        case mismatch::unregistered:
            return "a combinator has no registration";
        case mismatch::ill_formed:
            return "an operand is not well-formed";
        case mismatch::missing_non_label_branch:
            return "a branch of the supertype that is no label has no branch in the subtype that receives the "
                   "same payload";
        case mismatch::loses_termination:
            return "the supertype can end from this pair and the subtype cannot, so the subtype removes an exit "
                   "(fair subtyping)";
        case mismatch::label_set:
            return "an output choice of the subtype sends a label that the supertype does not send, or an input "
                   "choice of the supertype receives a label that the subtype does not receive";
        case mismatch::label_discipline:
            return "one choice is keyed and the other is positional, so the two sides put different kinds of word "
                   "on the wire";
        case mismatch::label_word_clash:
            return "two labels have one label word and different label keys, so the peer cannot tell them apart";
        default:
            break;
    }
    return "an unknown reason";
}

namespace detail {

[[nodiscard]] consteval bool value_in_order(const graph_node& sub, const graph_node& super) {
    const variance direction = sub.entry.value_variance;
    const std::meta::info order = sub.entry.value_order;
    const auto holds = [&](std::meta::info low, std::meta::info high) {
        if (order == std::meta::info{}) return low == high;
        return ask(order, {low, high});
    };
    switch (direction) {
        case variance::invariant:
            return sub.value == super.value;
        case variance::covariant:
            return holds(sub.value, super.value);
        case variance::contravariant:
            return holds(super.value, sub.value);
        default:
            break;
    }
    return false;
}

// An edge of the product, from one pair key to another.
struct product_edge {
    std::size_t from = npos;
    std::size_t to = npos;
};

// The mark bit of one pair key in an array of 64-bit words.
[[nodiscard]] consteval bool is_marked(const std::uint64_t* marks, std::size_t key) {
    return ((marks[key / 64] >> (key % 64)) & std::uint64_t{1}) != 0;
}

consteval void set_mark(std::uint64_t* marks, std::size_t key, bool is_set) {
    const std::uint64_t bit = std::uint64_t{1} << (key % 64);
    marks[key / 64] = is_set ? (marks[key / 64] | bit) : (marks[key / 64] & ~bit);
}

// The branches of one choice node, as graph indices on a scratch stack:
// the label branches in their order from place `labels`, then the
// branches that are no label in their order from place `non_labels`, up
// to place `end`.
struct split_branches {
    std::size_t labels = 0;
    std::size_t non_labels = 0;
    std::size_t end = 0;
};

[[nodiscard]] consteval split_branches split_onto(stack<std::size_t>& scratch, const graph_view& graph,
                                                  const graph_node& choice) {
    make_room(scratch, choice.child_count);
    std::size_t* const items = scratch.items;
    split_branches result{scratch.top, scratch.top, scratch.top};
    for (std::size_t k = 0; k < choice.child_count; ++k) {
        const std::size_t child = graph.children.data[choice.first_child + k];
        if (graph.nodes.data[child].is_label) items[result.end++] = child;
    }
    result.non_labels = result.end;
    for (std::size_t k = 0; k < choice.child_count; ++k) {
        const std::size_t child = graph.children.data[choice.first_child + k];
        if (!graph.nodes.data[child].is_label) items[result.end++] = child;
    }
    scratch.top = result.end;
    return result;
}

// The branch among places [first, end) of `candidates` whose head receives
// `payload`, or npos.  Complexity: linear in the candidates.
[[nodiscard]] consteval std::size_t partner_of(const graph_view& graph, const std::size_t* candidates,
                                               std::size_t first, std::size_t end, std::meta::info payload) {
    for (std::size_t place = first; place < end; ++place) {
        if (graph.nodes.data[candidates[place]].head_payload == payload) return candidates[place];
    }
    return npos;
}

// True when two label branches of one keyed choice have one label word,
// with one key or with two.  The run of the choice is in the order of the
// label words, so a repeated word stands next to its twin.
// Complexity: linear in the label branches of the choice.
[[nodiscard]] consteval bool repeats_a_label_word(const graph_view& graph, const graph_node& choice) {
    for (std::size_t rank = 1; rank < choice.label_count; ++rank) {
        const std::size_t earlier = graph.sorted_labels.data[choice.first_label + rank - 1];
        const std::size_t later = graph.sorted_labels.data[choice.first_label + rank];
        if (graph.nodes.data[earlier].label_word == graph.nodes.data[later].label_word) return true;
    }
    return false;
}

// Pairs the label branches of two keyed choices by label, in one merge of
// the two runs that the graphs sorted by label word, and pushes each pair
// onto `pending`, sub then super.  An output choice of the subtype sends
// only labels that the supertype sends, and an input choice of the
// supertype receives only labels that the subtype receives.  Two branches
// with one word pair only when their keys are equal too.  A choice that
// repeats a label word is not well-formed, and the merge refuses it also
// when the layer above did not check well-formedness.  Returns the reason
// the labels do not refine, or none.  The caller stops at a reason, so the
// pairs that a refused merge pushed are never read.  Complexity: linear in
// the label branches of the two choices.
[[nodiscard]] consteval mismatch pair_by_label(const graph_view& sub_graph, const graph_node& sub,
                                               const graph_view& super_graph, const graph_node& super, bool is_output,
                                               stack<std::size_t>& pending) {
    if (repeats_a_label_word(sub_graph, sub) || repeats_a_label_word(super_graph, super)) return mismatch::ill_formed;
    make_room(pending, 2 * (sub.label_count < super.label_count ? sub.label_count : super.label_count));
    std::size_t* const items = pending.items;
    std::size_t sub_rank = 0;
    std::size_t super_rank = 0;
    while (sub_rank < sub.label_count || super_rank < super.label_count) {
        const bool has_sub = sub_rank < sub.label_count;
        const bool has_super = super_rank < super.label_count;
        const std::size_t sub_index = has_sub ? sub_graph.sorted_labels.data[sub.first_label + sub_rank] : npos;
        const std::size_t super_index =
            has_super ? super_graph.sorted_labels.data[super.first_label + super_rank] : npos;
        const std::uint64_t sub_word = has_sub ? sub_graph.nodes.data[sub_index].label_word : 0;
        const std::uint64_t super_word = has_super ? super_graph.nodes.data[super_index].label_word : 0;
        if (has_sub && has_super && sub_word == super_word) {
            if (sub_graph.nodes.data[sub_index].label_key != super_graph.nodes.data[super_index].label_key) {
                return mismatch::label_word_clash;
            }
            items[pending.top++] = sub_index;
            items[pending.top++] = super_index;
            ++sub_rank;
            ++super_rank;
        } else if (has_sub && (!has_super || sub_word < super_word)) {
            // A label of the subtype that the supertype does not have.
            if (is_output) return mismatch::label_set;
            ++sub_rank;
        } else {
            // A label of the supertype that the subtype does not have.
            if (!is_output) return mismatch::label_set;
            ++super_rank;
        }
    }
    return mismatch::none;
}

[[nodiscard]] consteval bool payload_in_order(std::meta::info axioms, const graph_node& sub, const graph_node& super) {
    switch (sub.entry.payload_variance) {
        case variance::invariant:
            return sub.payload == super.payload;
        case variance::covariant:
            return subsorts(axioms, sub.payload, super.payload);
        case variance::contravariant:
            return subsorts(axioms, super.payload, sub.payload);
        default:
            break;
    }
    return false;
}

}  // namespace detail

// `axioms` is the namespace of the payload preorder.  Both types must be
// well-formed; the layer checks that first.
[[nodiscard]] consteval verdict refines(std::meta::info registry, std::meta::info axioms, std::meta::info sub,
                                        std::meta::info super) {
    const graph_view left = graph_of(registry, sub);
    const graph_view right = graph_of(registry, super);
    if (left.unregistered != std::meta::info{}) return {false, mismatch::unregistered, left.unregistered, {}};
    if (right.unregistered != std::meta::info{}) return {false, mismatch::unregistered, {}, right.unregistered};
    const std::size_t width = right.nodes.size();
    const graph_node* const sub_nodes = left.nodes.data;
    const graph_node* const super_nodes = right.nodes.data;
    // One mark bit for each pair, at the pair key sub * width + super.
    // During the walk a mark is set for a visited pair, and after the walk
    // it is set for a pair that can end.
    const std::size_t mark_word_count = (left.nodes.size() * width + 63) / 64;
    stack<std::uint64_t> mark_words{};
    make_room(mark_words, mark_word_count);
    std::uint64_t* const marks = mark_words.items;
    for (std::size_t word = 0; word < mark_word_count; ++word)
        marks[word] = 0;
    // The visited pairs in the order of the walk, and the edges of the
    // product between them, each as a pair key.
    stack<std::size_t> order{};
    stack<detail::product_edge> edges{};
    stack<std::size_t> pending{};
    stack<std::size_t> scratch{};
    push(pending, std::size_t{0});
    push(pending, std::size_t{0});
    while (pending.top != 0) {
        const std::size_t super_index = settle(right, pending.items[pending.top - 1]);
        const std::size_t sub_index = settle(left, pending.items[pending.top - 2]);
        pending.top -= 2;
        if (sub_index == npos || super_index == npos) return {false, mismatch::unguarded, sub, super};
        const std::size_t key = sub_index * width + super_index;
        if (detail::is_marked(marks, key)) continue;
        detail::set_mark(marks, key, true);
        push(order, key);
        const graph_node& a = sub_nodes[sub_index];
        const graph_node& b = super_nodes[super_index];
        if (a.entry.shape != b.entry.shape) return {false, mismatch::shape, a.type, b.type};
        const std::size_t before = pending.top;
        switch (a.entry.kind) {
            case shape_kind::terminal:
            case shape_kind::back:
            case shape_kind::binder:
                break;
            case shape_kind::step:
                if (!detail::payload_in_order(axioms, a, b)) {
                    if (a.entry.payload_variance == variance::contravariant) {
                        return {false, mismatch::payload, b.payload, a.payload};
                    }
                    return {false, mismatch::payload, a.payload, b.payload};
                }
                push(pending, a.next);
                push(pending, b.next);
                break;
            case shape_kind::wrapper:
                if (!detail::value_in_order(a, b)) return {false, mismatch::value, a.type, b.type};
                push(pending, a.next);
                push(pending, b.next);
                break;
            case shape_kind::marker:
                push(pending, a.next);
                push(pending, b.next);
                break;
            case shape_kind::choice: {
                if (a.annotation != b.annotation) return {false, mismatch::annotation, a.type, b.type};
                const detail::split_branches own = detail::split_onto(scratch, left, a);
                const detail::split_branches other = detail::split_onto(scratch, right, b);
                const std::size_t* const split = scratch.items;
                const std::size_t own_labels = own.non_labels - own.labels;
                const std::size_t other_labels = other.non_labels - other.labels;
                if (other.end != other.non_labels && other_labels == 0) {
                    return {false, mismatch::pure_non_label_choice, a.type, b.type};
                }
                // A choice with no label branch is not well-formed.  The
                // layer refuses it first, and the relation refuses it too,
                // so a layer that skips the check stays sound.
                if (own_labels == 0 || other_labels == 0) return {false, mismatch::ill_formed, a.type, b.type};
                const bool is_output = a.entry.direction == polarity::output;
                if (a.is_keyed != b.is_keyed) return {false, mismatch::label_discipline, a.type, b.type};
                if (a.is_keyed) {
                    const mismatch reason = detail::pair_by_label(left, a, right, b, is_output, pending);
                    if (reason != mismatch::none) return {false, reason, a.type, b.type};
                } else {
                    const bool count_is_wrong = is_output ? own_labels > other_labels : own_labels < other_labels;
                    if (count_is_wrong) return {false, mismatch::branch_count, a.type, b.type};
                    const std::size_t shared = own_labels < other_labels ? own_labels : other_labels;
                    make_room(pending, 2 * shared);
                    for (std::size_t k = 0; k < shared; ++k) {
                        pending.items[pending.top++] = split[own.labels + k];
                        pending.items[pending.top++] = split[other.labels + k];
                    }
                }
                for (std::size_t place = own.non_labels; place < own.end; ++place) {
                    const std::meta::info payload = sub_nodes[split[place]].head_payload;
                    if (detail::partner_of(right, split, other.non_labels, other.end, payload) == npos) {
                        return {false, mismatch::non_label_branch, a.type, b.type};
                    }
                }
                make_room(pending, 2 * (other.end - other.non_labels));
                for (std::size_t place = other.non_labels; place < other.end; ++place) {
                    const std::size_t theirs = split[place];
                    const std::meta::info payload = super_nodes[theirs].head_payload;
                    const std::size_t mine = detail::partner_of(left, split, own.non_labels, own.end, payload);
                    if (mine == npos) return {false, mismatch::missing_non_label_branch, a.type, b.type};
                    pending.items[pending.top++] = mine;
                    pending.items[pending.top++] = theirs;
                }
                scratch.top = 0;
                break;
            }
            default:
                break;
        }
        // Each child pair is an edge of the product.  A child that does
        // not settle stops the walk when it is popped.
        make_room(edges, (pending.top - before) / 2);
        for (std::size_t k = before; k + 1 < pending.top; k += 2) {
            const std::size_t child_sub = settle(left, pending.items[k]);
            const std::size_t child_super = settle(right, pending.items[k + 1]);
            if (child_sub == npos || child_super == npos) continue;
            edges.items[edges.top++] = detail::product_edge{key, child_sub * width + child_super};
        }
        // Pairs are pushed in order and popped from the back, so the
        // walk reaches the last child first.  The pushed block is
        // reversed pair by pair to keep the first child first, which
        // makes the reported mismatch the leftmost one.
        std::size_t* const block = pending.items;
        for (std::size_t low = before, high = pending.top; low + 2 < high; low += 2, high -= 2) {
            const std::size_t first_sub = block[low];
            const std::size_t first_super = block[low + 1];
            block[low] = block[high - 2];
            block[low + 1] = block[high - 1];
            block[high - 2] = first_sub;
            block[high - 1] = first_super;
        }
    }
    // Exit preservation.  A pair of terminals can end, and a pair with an
    // edge to a pair that can end can end.  The edges are recorded in the
    // order of the walk, so a pass from the last edge back settles every
    // edge of the tree, and each further pass settles one more loop-back.
    // Only a visited pair has its mark set here, so a clear mark for each
    // visited pair that is not a pair of terminals clears the marks.
    const std::size_t* const visited = order.items;
    for (std::size_t place = 0; place < order.top; ++place) {
        const std::size_t key = visited[place];
        detail::set_mark(marks, key, sub_nodes[key / width].entry.kind == shape_kind::terminal);
    }
    const detail::product_edge* const links = edges.items;
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t edge = edges.top; edge-- > 0;) {
            if (detail::is_marked(marks, links[edge].from) || !detail::is_marked(marks, links[edge].to)) continue;
            detail::set_mark(marks, links[edge].from, true);
            is_changed = true;
        }
    }
    for (std::size_t place = 0; place < order.top; ++place) {
        const std::size_t key = visited[place];
        const graph_node& a = sub_nodes[key / width];
        const graph_node& b = super_nodes[key % width];
        if (b.can_end && !detail::is_marked(marks, key)) return {false, mismatch::loses_termination, a.type, b.type};
    }
    return {true, mismatch::none, {}, {}};
}

// True when every node of the protocol can reach a terminal: from each
// position the protocol can still end.  A stream or a server loop with no
// exit is not terminable.  Complexity: linear in the nodes of the graph,
// after the graph.
[[nodiscard]] consteval bool is_terminable(std::meta::info registry, std::meta::info type) {
    const graph_view graph = graph_of(registry, type);
    if (graph.unregistered != std::meta::info{}) return false;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        if (!graph.nodes[index].can_end) return false;
    }
    return true;
}

}  // namespace fixy::session::transition
