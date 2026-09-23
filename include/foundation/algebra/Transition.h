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
// ── The registration contract ─────────────────────────────────────────
//
// A registry is a namespace.  Each namespace-scope variable in it whose
// type is `combinator`, with or without const, is one registration.  A
// variable of type `payload_rule` is one payload registration.  Every
// other member is skipped: a function, a type, a nested namespace, or a
// variable of a different type.  A nested namespace is not opened.
//
//     namespace my_protocol::combinators {
//     inline constexpr foundation::algebra::transition::combinator put{
//         .shape = ^^Put, .kind = shape_kind::step, .direction = polarity::output,
//         .dual = ^^Take, .payload_variance = variance::covariant};
//     }
//
// A header that declares a combinator registers it in the same header,
// next to the declaration.  A different header can add a registration
// to a registry when it opens the registry namespace again.  The fold reads the
// registry when it first meets a shape, and that answer holds for the
// rest of the translation unit.  A query that meets a shape which is not
// registered stops the build, so a registration that comes too late is
// an error and never a silent answer.
//
// The kind fixes the layout of the template arguments:
//
//   step      Shape<Payload, Next>        one message, then Next
//   choice    Shape<Branch...>            one label, then that branch.
//             Shape<Note, Branch...>      when the first argument is a
//                                         specialization of `annotation`.
//                                         The note is not a branch.
//   binder    Shape<Body>                 a recursion binder
//   back      Shape                       a jump to the nearest binder
//   terminal  Shape                       the protocol stops
//   wrapper   Shape<Value, Inner>         a value slot over Inner
//
// A registration whose shape has a different layout is refused at the
// first query that meets it.
//
// The fields of a registration:
//
//   shape             The class template, or the class of a nullary
//                     combinator.
//   kind, direction   The layout above, and output or input for a step
//                     or a choice.  Every other kind is neutral.
//   dual              The registered shape of the dual combinator.  The
//                     dual of the dual must be the shape itself.
//   payload_variance  The variance of the payload of a step.  An output
//                     step is covariant or invariant, and an input step
//                     is contravariant or invariant.
//   annotation        The template of the note of a choice, or null.
//                     The dual keeps the note only when the dual shape
//                     names the same template.  Refinement compares
//                     notes for equality.
//   value_variance    The variance of the value of a wrapper.
//   value_order       A variable template `template <auto A, auto B>
//                     constexpr bool`, the order of the values.  Null
//                     means equality.
//   value_admits      A variable template `template <auto V> constexpr
//                     bool`.  A wrapper whose value it refuses is not
//                     well-formed.  Null admits every value.
//   absorbs_suffix    For a terminal.  False: composition replaces it
//                     with the suffix.  True: composition keeps it, for
//                     an endpoint that never resumes.
//
// Coherence.  Refinement must be closed under duality: when T refines
// U, the dual of U refines the dual of T (Padovani and Zavattaro, TOPLAS
// 2026, page 3, where this closure is what makes the type system
// sound).  A registration keeps that closure when its dual has the same
// kind, the opposite direction, the opposite payload variance, the
// opposite value variance and the same absorption.  A self-dual wrapper
// must therefore have an invariant value.  The flip alone admits a pair
// with each side backwards, so a step also needs the variance of its
// direction: the receiver of an output step then accepts each payload
// that a subtype sends.  `check_combinator` refuses a
// registration that breaks one of these rules, and each query that
// meets the registration refuses with it.
//
// A payload registration names a payload template by its shape:
//
//   is_sendable  False: an output step with this payload is not
//                well-formed.
//   is_label     False: an input branch that receives this payload is
//                not a label the peer can send.  A choice with no label
//                branch is empty.  In refinement, an input choice of the
//                subtype has no extra branch of this kind, and the input
//                choice of the supertype is not made only of such
//                branches (Barwell, Hou, Yoshida and Zhou, LMCS 2025,
//                Definition 4.4, rule Sub-&).
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
// ── The hook ──────────────────────────────────────────────────────────
//
// An algebra can take the result for a child node from a template
// instead of from the fold itself.  A layer passes the reflection of a
// variable template or an alias template as the hook, and the fold asks
// that template for each child.  The template of the layer is then the
// entry point for every node, and an explicit specialization of it for
// one node answers at every depth.  Without a hook the fold recurses in
// place.
//
// Complexity: each fold visits each node of the spine once, so it is
// linear in the size of the protocol.  Refinement visits each pair of
// nodes at most once, so it is O(|T|·|U|) on the two type graphs
// (Udomsrirungruang and Yoshida, POPL 2025, section 5).

#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation::algebra::transition {

// ── Registration vocabulary ───────────────────────────────────────────

enum class shape_kind : std::uint8_t { step, choice, binder, back, terminal, wrapper };

enum class polarity : std::uint8_t { neutral, output, input };

enum class variance : std::uint8_t { invariant, covariant, contravariant };

struct combinator {
    std::meta::info shape{};
    shape_kind kind = shape_kind::terminal;
    polarity direction = polarity::neutral;
    std::meta::info dual{};
    variance payload_variance = variance::invariant;
    std::meta::info annotation{};
    variance value_variance = variance::invariant;
    std::meta::info value_order{};
    std::meta::info value_admits{};
    bool absorbs_suffix = false;
};

struct payload_rule {
    std::meta::info shape{};
    bool is_sendable = true;
    bool is_label = true;
};

// One axiom of a payload preorder.  An axiom drops a wrapper, weakens a
// type in place, or lifts the order through a class template:
//
//   drops       A variable template `template <class T> constexpr bool`.
//               True when T sheds its outer layer.
//   inner       An alias template `template <class T> using`, the type
//               T sheds to.  It must be a template argument of T, so
//               each drop makes the type smaller.
//   weakens     A variable template `template <class T, class U>
//               constexpr bool`.  True when one step takes T to U.
//   congruence  A class template.  Two of its specializations are in the
//               order when each argument at a position in `covariant` is
//               in the order, and each other argument is the same.
//   covariant   A bit mask over the argument positions of `congruence`.
//               Bit I is position I.  A position past bit 63 is compared
//               for identity.
//
// The templates must accept every type and answer false where they do
// not apply.  A template that cannot take a type is read as false.
struct subsort_axiom {
    std::meta::info drops{};
    std::meta::info inner{};
    std::meta::info weakens{};
    std::meta::info congruence{};
    std::uint64_t covariant = 0;
};

// The position of a node relative to the binders above it.  Depth is
// the number of binders above the node.  Guarded is true when some step or
// choice lies between the node and its nearest binder.  A layer passes
// this type to its hook as the context of a child.
template <std::size_t Depth, bool Guarded>
struct scope {
    static constexpr std::size_t depth = Depth;
    static constexpr bool guarded = Guarded;
};

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

// ── The registry ──────────────────────────────────────────────────────

// The template of a specialization, or the class itself.
[[nodiscard]] consteval std::meta::info shape_of(std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    if (std::meta::is_type(dealiased) && std::meta::has_template_arguments(dealiased)) {
        return std::meta::template_of(dealiased);
    }
    return dealiased;
}

struct combinator_lookup {
    bool is_found = false;
    std::size_t count = 0;
    combinator entry{};
};

struct payload_lookup {
    bool is_found = false;
    payload_rule entry{};
};

namespace detail {

[[nodiscard]] consteval bool is_registration_of(std::meta::info member, std::meta::info type) {
    if (!std::meta::is_variable(member)) return false;
    return std::meta::remove_cvref(std::meta::type_of(member)) == type;
}

}  // namespace detail

// Reads the registry now.  Complexity: linear in its members.
[[nodiscard]] consteval combinator_lookup read_combinator(std::meta::info registry, std::meta::info shape) {
    combinator_lookup result{};
    for (const std::meta::info member : std::meta::members_of(registry, std::meta::access_context::unchecked())) {
        if (!detail::is_registration_of(member, ^^combinator)) continue;
        const combinator entry = std::meta::extract<combinator>(member);
        if (entry.shape != shape) continue;
        ++result.count;
        if (!result.is_found) {
            result.is_found = true;
            result.entry = entry;
        }
    }
    return result;
}

[[nodiscard]] consteval payload_lookup read_payload_rule(std::meta::info registry, std::meta::info shape) {
    payload_lookup result{};
    for (const std::meta::info member : std::meta::members_of(registry, std::meta::access_context::unchecked())) {
        if (!detail::is_registration_of(member, ^^payload_rule)) continue;
        const payload_rule entry = std::meta::extract<payload_rule>(member);
        if (entry.shape == shape) {
            result.is_found = true;
            result.entry = entry;
            return result;
        }
    }
    return result;
}

// The registry answer for one shape, read once per translation unit.
template <std::meta::info Registry, std::meta::info Shape>
inline constexpr combinator_lookup combinator_lookup_v = read_combinator(Registry, Shape);

template <std::meta::info Registry, std::meta::info Shape>
inline constexpr payload_lookup payload_lookup_v = read_payload_rule(Registry, Shape);

[[nodiscard]] consteval combinator_lookup lookup_combinator(std::meta::info registry, std::meta::info shape) {
    return std::meta::extract<combinator_lookup>(std::meta::substitute(
        ^^combinator_lookup_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(shape)}));
}

[[nodiscard]] consteval payload_lookup lookup_payload_rule(std::meta::info registry, std::meta::info payload) {
    const std::meta::info shape = shape_of(payload);
    return std::meta::extract<payload_lookup>(std::meta::substitute(
        ^^payload_lookup_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(shape)}));
}

// ── Coherence of one registration ─────────────────────────────────────

enum class incoherence : std::uint8_t {
    none,
    no_shape,
    registered_twice,
    dual_unregistered,
    dual_not_involutive,
    dual_kind_differs,
    direction_not_flipped,
    payload_variance_not_flipped,
    value_variance_not_flipped,
    absorption_differs,
    direction_on_neutral_kind,
    order_on_non_wrapper,
    variance_against_direction,
};

struct coherence_verdict {
    incoherence reason = incoherence::none;
    std::meta::info shape{};
};

namespace detail {

[[nodiscard]] consteval polarity flipped(polarity value) {
    if (value == polarity::output) return polarity::input;
    if (value == polarity::input) return polarity::output;
    return polarity::neutral;
}

[[nodiscard]] consteval variance flipped(variance value) {
    if (value == variance::covariant) return variance::contravariant;
    if (value == variance::contravariant) return variance::covariant;
    return variance::invariant;
}

[[nodiscard]] consteval bool is_directed_kind(shape_kind kind) {
    return kind == shape_kind::step || kind == shape_kind::choice;
}

}  // namespace detail

// The first rule this registration breaks, or none.  A shape with no
// registration is not incoherent: the fold refuses it on its own.
[[nodiscard]] consteval coherence_verdict check_combinator(std::meta::info registry, std::meta::info shape) {
    const combinator_lookup own = lookup_combinator(registry, shape);
    if (!own.is_found) return {};
    const combinator& entry = own.entry;
    if (entry.shape == std::meta::info{}) return {incoherence::no_shape, shape};
    if (own.count > 1) return {incoherence::registered_twice, shape};
    if (detail::is_directed_kind(entry.kind) == (entry.direction == polarity::neutral)) {
        return {incoherence::direction_on_neutral_kind, shape};
    }
    if (entry.kind != shape_kind::wrapper
        && (entry.value_order != std::meta::info{} || entry.value_admits != std::meta::info{})) {
        return {incoherence::order_on_non_wrapper, shape};
    }
    const combinator_lookup dual = lookup_combinator(registry, entry.dual);
    if (entry.dual == std::meta::info{} || !dual.is_found) return {incoherence::dual_unregistered, shape};
    const combinator& mirror = dual.entry;
    if (mirror.dual != entry.shape) return {incoherence::dual_not_involutive, shape};
    if (mirror.kind != entry.kind) return {incoherence::dual_kind_differs, shape};
    if (mirror.direction != detail::flipped(entry.direction)) return {incoherence::direction_not_flipped, shape};
    if (mirror.payload_variance != detail::flipped(entry.payload_variance)) {
        return {incoherence::payload_variance_not_flipped, shape};
    }
    if (mirror.value_variance != detail::flipped(entry.value_variance)) {
        return {incoherence::value_variance_not_flipped, shape};
    }
    if (mirror.absorbs_suffix != entry.absorbs_suffix) return {incoherence::absorption_differs, shape};
    // A pair can flip its variance under duality and still have each side
    // backwards.  An output that is contravariant lets the subtype send a
    // wider payload than the peer of the supertype receives.
    const bool is_backwards = (entry.direction == polarity::output && entry.payload_variance == variance::contravariant)
                              || (entry.direction == polarity::input && entry.payload_variance == variance::covariant);
    if (entry.kind == shape_kind::step && is_backwards) return {incoherence::variance_against_direction, shape};
    return {};
}

// The first incoherent registration of the registry, or none.
// Complexity: quadratic in the number of registrations.
[[nodiscard]] consteval coherence_verdict check_registry(std::meta::info registry) {
    for (const std::meta::info member : std::meta::members_of(registry, std::meta::access_context::unchecked())) {
        if (!detail::is_registration_of(member, ^^combinator)) continue;
        const coherence_verdict verdict = check_combinator(registry, std::meta::extract<combinator>(member).shape);
        if (verdict.reason != incoherence::none) return verdict;
    }
    return {};
}

[[nodiscard]] consteval std::string_view incoherence_name(incoherence reason) {
    switch (reason) {
        case incoherence::none:
            return "coherent";
        case incoherence::no_shape:
            return "the registration names no shape";
        case incoherence::registered_twice:
            return "the shape has more than one registration";
        case incoherence::dual_unregistered:
            return "the dual shape has no registration";
        case incoherence::dual_not_involutive:
            return "the dual of the dual is not the shape, so duality is not an involution";
        case incoherence::dual_kind_differs:
            return "the dual has a different kind";
        case incoherence::direction_not_flipped:
            return "the dual has the same direction, so a send would face a send";
        case incoherence::payload_variance_not_flipped:
            return "the dual payload variance is not the opposite, so refinement is not closed under duality";
        case incoherence::value_variance_not_flipped:
            return "the dual value variance is not the opposite, so refinement is not closed under duality";
        case incoherence::absorption_differs:
            return "the dual composes differently";
        case incoherence::direction_on_neutral_kind:
            return "a step or a choice has no direction, or a different kind has one";
        case incoherence::order_on_non_wrapper:
            return "a value order or a value filter on a kind with no value";
        case incoherence::variance_against_direction:
            return "an output step with a contravariant payload, or an input step with a covariant payload, lets a "
                   "subtype send a payload that the peer does not receive";
        default:
            break;
    }
    return "an unknown reason";
}

// ── One node ──────────────────────────────────────────────────────────

struct node {
    std::meta::info type{};
    bool is_registered = false;
    bool is_malformed = false;
    combinator entry{};
    std::meta::info payload{};
    std::meta::info next{};
    std::meta::info annotation{};
    std::meta::info value{};
    std::vector<std::meta::info> branches{};
};

// The registered view of one type.  A type whose shape has no
// registration, or whose arguments do not match the layout of its kind,
// is not registered.  Complexity: linear in the number of arguments.
[[nodiscard]] consteval node decompose(std::meta::info registry, std::meta::info type) {
    node result{};
    result.type = std::meta::dealias(type);
    if (!std::meta::is_type(result.type)) return result;
    const combinator_lookup lookup = lookup_combinator(registry, shape_of(result.type));
    if (!lookup.is_found) return result;
    result.entry = lookup.entry;
    result.is_registered = true;
    const bool has_arguments = std::meta::has_template_arguments(result.type);
    std::vector<std::meta::info> arguments;
    if (has_arguments) arguments = std::meta::template_arguments_of(result.type);
    switch (result.entry.kind) {
        case shape_kind::terminal:
        case shape_kind::back:
            result.is_malformed = has_arguments;
            break;
        case shape_kind::step:
            result.is_malformed = arguments.size() != 2 || !std::meta::is_type(arguments[0])
                                  || !std::meta::is_type(arguments[1]);
            if (!result.is_malformed) {
                result.payload = std::meta::dealias(arguments[0]);
                result.next = std::meta::dealias(arguments[1]);
            }
            break;
        case shape_kind::binder:
            result.is_malformed = arguments.size() != 1 || !std::meta::is_type(arguments[0]);
            if (!result.is_malformed) result.next = std::meta::dealias(arguments[0]);
            break;
        case shape_kind::wrapper:
            result.is_malformed = arguments.size() != 2 || std::meta::is_type(arguments[0])
                                  || !std::meta::is_type(arguments[1]);
            if (!result.is_malformed) {
                result.value = arguments[0];
                result.next = std::meta::dealias(arguments[1]);
            }
            break;
        case shape_kind::choice: {
            std::size_t first = 0;
            if (!arguments.empty() && result.entry.annotation != std::meta::info{}
                && std::meta::is_type(arguments[0]) && shape_of(arguments[0]) == result.entry.annotation) {
                result.annotation = std::meta::dealias(arguments[0]);
                first = 1;
            }
            for (std::size_t index = first; index < arguments.size(); ++index) {
                if (!std::meta::is_type(arguments[index])) {
                    result.is_malformed = true;
                    break;
                }
                result.branches.push_back(std::meta::dealias(arguments[index]));
            }
            break;
        }
        default:
            result.is_malformed = true;
            break;
    }
    if (result.is_malformed) result.is_registered = false;
    return result;
}

[[nodiscard]] consteval bool is_registered(std::meta::info registry, std::meta::info type) {
    return decompose(registry, type).is_registered;
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
//   result unregistered(node, context)
//   std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context)
//   result from_hook(std::meta::info)
//
// `child(type, context)` gives the result for one child.  It asks the
// hook when the fold has one, and folds in place otherwise.  An algebra
// asks only for the children it needs, so a refusal stops the walk.

template <class Algebra>
[[nodiscard]] consteval typename Algebra::result fold(std::meta::info registry, std::meta::info type,
                                                      const Algebra& algebra, typename Algebra::context context,
                                                      std::meta::info hook = {}) {
    const node view = decompose(registry, type);
    if (!view.is_registered) return algebra.unregistered(view, context);
    const auto child = [&](std::meta::info child_type, typename Algebra::context child_context) {
        if (hook != std::meta::info{}) {
            return algebra.from_hook(std::meta::substitute(hook, algebra.hook_arguments(child_type, child_context)));
        }
        return fold(registry, child_type, algebra, child_context, hook);
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
        default:
            break;
    }
    return algebra.unregistered(view, context);
}

// ── Algebras ──────────────────────────────────────────────────────────

namespace detail {

// A branch is a label unless its head, under wrappers, is an input step
// whose payload a payload registration marks as no label.
[[nodiscard]] consteval bool is_label_branch(std::meta::info registry, std::meta::info branch) {
    const node head = decompose(registry, strip_wrappers(registry, branch));
    if (!head.is_registered || head.entry.kind != shape_kind::step || head.entry.direction != polarity::input) {
        return true;
    }
    const payload_lookup rule = lookup_payload_rule(registry, head.payload);
    return !rule.is_found || rule.entry.is_label;
}

[[nodiscard]] consteval std::size_t label_count(std::meta::info registry, const node& choice) {
    std::size_t count = 0;
    for (const std::meta::info branch : choice.branches) {
        if (is_label_branch(registry, branch)) ++count;
    }
    return count;
}

[[nodiscard]] consteval bool value_is_admitted(const node& wrapper) {
    if (wrapper.entry.value_admits == std::meta::info{}) return true;
    if (!std::meta::can_substitute(wrapper.entry.value_admits, {wrapper.value})) return false;
    return std::meta::extract<bool>(std::meta::substitute(wrapper.entry.value_admits, {wrapper.value}));
}

[[nodiscard]] consteval std::meta::info scope_type(std::size_t depth, bool guarded) {
    return std::meta::substitute(^^scope, {std::meta::reflect_constant(depth), std::meta::reflect_constant(guarded)});
}

// The branches of a choice after a mapping, with the note kept when the
// target shape carries a note of the same template.
[[nodiscard]] consteval std::vector<std::meta::info> choice_arguments(std::meta::info registry, const node& choice,
                                                                      std::meta::info target_shape,
                                                                      const std::vector<std::meta::info>& mapped) {
    std::vector<std::meta::info> arguments;
    const combinator_lookup target = lookup_combinator(registry, target_shape);
    if (choice.annotation != std::meta::info{} && target.is_found
        && target.entry.annotation == choice.entry.annotation) {
        arguments.push_back(choice.annotation);
    }
    for (const std::meta::info branch : mapped) arguments.push_back(branch);
    return arguments;
}

}  // namespace detail

// True for a node where a protocol may stop: a terminal, under any
// wrappers.
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
    consteval bool unregistered(const node&, context) const { return false; }
    consteval std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context) const {
        return {child_type};
    }
    consteval bool from_hook(std::meta::info answer) const { return std::meta::extract<bool>(answer); }
};

// True when a choice somewhere on the spine has no label branch.  A
// handle at such a choice is stuck: an empty internal choice has no
// branch to pick, and an empty external choice has no label the peer
// can send.
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
    consteval bool unregistered(const node&, context) const { return true; }
    consteval std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context) const {
        return {child_type};
    }
    consteval bool from_hook(std::meta::info answer) const { return std::meta::extract<bool>(answer); }
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
//
// The hook receives the child type and a `scope` type.
struct well_formed_algebra {
    struct position {
        std::size_t depth = 0;
        bool is_guarded = true;
    };
    using result = bool;
    using context = position;
    std::meta::info registry{};
    std::meta::info terminal_hook{};

    template <class Child>
    consteval bool terminal(const node&, context, const Child&) const {
        return true;
    }
    template <class Child>
    consteval bool back(const node&, context ctx, const Child&) const {
        return ctx.depth > 0 && ctx.is_guarded;
    }
    template <class Child>
    consteval bool step(const node& view, context ctx, const Child& child) const {
        if (view.entry.direction == polarity::output) {
            const payload_lookup rule = lookup_payload_rule(registry, view.payload);
            if (rule.is_found && !rule.entry.is_sendable) return false;
        }
        return child(view.next, position{ctx.depth, true});
    }
    template <class Child>
    consteval bool choice(const node& view, context ctx, const Child& child) const {
        for (const std::meta::info branch : view.branches) {
            if (!child(branch, position{ctx.depth, true})) return false;
        }
        return true;
    }
    template <class Child>
    consteval bool binder(const node& view, context ctx, const Child& child) const {
        const bool body_is_terminal =
            terminal_hook != std::meta::info{}
                ? std::meta::extract<bool>(std::meta::substitute(terminal_hook, {view.next}))
                : fold(registry, view.next, terminal_algebra{}, 0);
        if (body_is_terminal) return false;
        return child(view.next, position{ctx.depth + 1, false});
    }
    template <class Child>
    consteval bool wrapper(const node& view, context ctx, const Child& child) const {
        return detail::value_is_admitted(view) && child(view.next, ctx);
    }
    consteval bool unregistered(const node&, context) const { return false; }
    consteval std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context ctx) const {
        return {child_type, detail::scope_type(ctx.depth, ctx.is_guarded)};
    }
    consteval bool from_hook(std::meta::info answer) const { return std::meta::extract<bool>(answer); }
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
        std::vector<std::meta::info> mapped;
        for (const std::meta::info branch : view.branches) mapped.push_back(child(branch, ctx));
        return std::meta::substitute(view.entry.dual,
                                     detail::choice_arguments(registry, view, view.entry.dual, mapped));
    }
    template <class Child>
    consteval std::meta::info binder(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.dual, {child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info wrapper(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.dual, {view.value, child(view.next, ctx)});
    }
    consteval std::meta::info unregistered(const node&, context) const { return {}; }
    consteval std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context) const {
        return {child_type};
    }
    consteval std::meta::info from_hook(std::meta::info answer) const { return std::meta::dealias(answer); }

private:
    static consteval std::meta::info rebuild_nullary(const node& view) {
        return view.entry.dual == std::meta::info{} ? view.type : view.entry.dual;
    }
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
        std::vector<std::meta::info> mapped;
        for (const std::meta::info branch : view.branches) mapped.push_back(child(branch, ctx));
        return std::meta::substitute(view.entry.shape,
                                     detail::choice_arguments(registry, view, view.entry.shape, mapped));
    }
    template <class Child>
    consteval std::meta::info binder(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info wrapper(const node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.value, child(view.next, ctx)});
    }
    consteval std::meta::info unregistered(const node&, context) const { return {}; }
    consteval std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context) const {
        return {child_type, suffix};
    }
    consteval std::meta::info from_hook(std::meta::info answer) const { return std::meta::dealias(answer); }
};

// Composition at one branch: the walk passes each step, binder and
// wrapper, and at the first choice it composes the suffix into branch
// `index` alone.  The layer refuses a spine that reaches a terminal or
// a back node first, and an index past the last branch.  The algebra
// answers the null reflection for both.
struct compose_at_choice_algebra {
    using result = std::meta::info;
    using context = int;
    std::meta::info registry{};
    std::size_t index = 0;
    std::meta::info suffix{};
    std::meta::info compose_hook{};

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
        std::vector<std::meta::info> mapped = view.branches;
        mapped[index] = compose_hook != std::meta::info{}
                            ? std::meta::dealias(std::meta::substitute(compose_hook, {mapped[index], suffix}))
                            : fold(registry, mapped[index], compose_algebra{registry, suffix}, 0);
        return std::meta::substitute(view.entry.shape,
                                     detail::choice_arguments(registry, view, view.entry.shape, mapped));
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
    consteval std::meta::info unregistered(const node&, context) const { return {}; }
    consteval std::vector<std::meta::info> hook_arguments(std::meta::info child_type, context) const {
        return {child_type, std::meta::reflect_constant(index), suffix};
    }
    consteval std::meta::info from_hook(std::meta::info answer) const { return std::meta::dealias(answer); }
};

// The first node on the spine where composition at a branch stops: the
// first choice, terminal or back node under the steps, binders and
// wrappers at the head.
[[nodiscard]] consteval node first_stop_of_spine(std::meta::info registry, std::meta::info type) {
    node view = decompose(registry, type);
    while (view.is_registered
           && (view.entry.kind == shape_kind::step || view.entry.kind == shape_kind::binder
               || view.entry.kind == shape_kind::wrapper)) {
        view = decompose(registry, view.next);
    }
    return view;
}

// ── The payload preorder ──────────────────────────────────────────────
//
// The reflexive relation that the axioms of one namespace generate.
// Each drop makes the subtype smaller, so the walk stops.  The depth
// bound is a second stop for an axiom that breaks that rule.
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
    return std::meta::extract<bool>(std::meta::substitute(
        ^^subsorts_v,
        {std::meta::reflect_constant(axioms), std::meta::reflect_constant(low), std::meta::reflect_constant(high)}));
}

// ── The type graph ────────────────────────────────────────────────────
//
// One node per occurrence of a combinator.  A back node points to the
// binder it binds.  A binder points to its body.  The graph of a
// protocol has as many nodes as the protocol has combinators.

// `is_label` is the answer of the payload rules for this node as a
// branch.  `has_restricted_payload` is true for a step whose payload a
// payload registration marks as not sendable or as no label.
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
    bool has_restricted_payload = false;
};

struct type_graph {
    std::vector<graph_node> nodes{};
    std::vector<std::size_t> children{};
    std::meta::info unregistered{};
};

namespace detail {

consteval std::size_t add_to_graph(std::meta::info registry, type_graph& graph, std::meta::info type,
                                   std::vector<std::size_t>& binders) {
    const node view = decompose(registry, type);
    const std::size_t here = graph.nodes.size();
    graph.nodes.push_back(graph_node{});
    if (!view.is_registered) {
        if (graph.unregistered == std::meta::info{}) graph.unregistered = view.type;
        graph.nodes[here].type = view.type;
        return here;
    }
    graph.nodes[here].type = view.type;
    graph.nodes[here].entry = view.entry;
    graph.nodes[here].payload = view.payload;
    graph.nodes[here].value = view.value;
    graph.nodes[here].annotation = view.annotation;
    graph.nodes[here].is_label = is_label_branch(registry, view.type);
    if (view.entry.kind == shape_kind::step) {
        const payload_lookup rule = lookup_payload_rule(registry, view.payload);
        graph.nodes[here].has_restricted_payload = rule.is_found && (!rule.entry.is_label || !rule.entry.is_sendable);
    }
    switch (view.entry.kind) {
        case shape_kind::terminal:
            break;
        case shape_kind::back:
            graph.nodes[here].next = binders.empty() ? npos : binders.back();
            break;
        case shape_kind::step:
        case shape_kind::wrapper: {
            const std::size_t below = add_to_graph(registry, graph, view.next, binders);
            graph.nodes[here].next = below;
            break;
        }
        case shape_kind::binder: {
            binders.push_back(here);
            const std::size_t below = add_to_graph(registry, graph, view.next, binders);
            binders.pop_back();
            graph.nodes[here].next = below;
            break;
        }
        case shape_kind::choice: {
            std::vector<std::size_t> own;
            for (const std::meta::info branch : view.branches) own.push_back(add_to_graph(registry, graph, branch, binders));
            graph.nodes[here].first_child = graph.children.size();
            graph.nodes[here].child_count = own.size();
            for (const std::size_t index : own) graph.children.push_back(index);
            break;
        }
        default:
            break;
    }
    return here;
}

}  // namespace detail

[[nodiscard]] consteval type_graph build_graph(std::meta::info registry, std::meta::info type) {
    type_graph graph{};
    std::vector<std::size_t> binders;
    detail::add_to_graph(registry, graph, type, binders);
    return graph;
}

// A run of elements in static storage.  It is a structural type, so a
// constant of it can be read back through reflection.
template <class T>
struct static_run {
    const T* data = nullptr;
    std::size_t length = 0;

    [[nodiscard]] constexpr const T& operator[](std::size_t index) const { return data[index]; }
    [[nodiscard]] constexpr std::size_t size() const { return length; }
};

template <class T>
[[nodiscard]] consteval static_run<T> to_static_run(const std::vector<T>& values) {
    const std::span<const T> stored = std::define_static_array(values);
    return static_run<T>{stored.data(), stored.size()};
}

// A graph in static storage.  The graph of one protocol is built once
// per translation unit, and each relation that reads it shares it.
struct graph_view {
    static_run<graph_node> nodes{};
    static_run<std::size_t> children{};
    std::meta::info unregistered{};
};

[[nodiscard]] consteval graph_view freeze(const type_graph& graph) {
    return graph_view{to_static_run(graph.nodes), to_static_run(graph.children), graph.unregistered};
}

template <std::meta::info Registry, std::meta::info Type>
inline constexpr graph_view graph_v = freeze(build_graph(Registry, Type));

[[nodiscard]] consteval graph_view graph_of(std::meta::info registry, std::meta::info type) {
    return std::meta::extract<graph_view>(std::meta::substitute(
        ^^graph_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(std::meta::dealias(type))}));
}

// The node a walk reaches from `index` when it passes each binder into
// its body and follows each back node to its binder.  A spine that does
// not stop has an unguarded back node, and the answer is npos.
[[nodiscard]] consteval std::size_t settle(const graph_view& graph, std::size_t index) {
    for (std::size_t budget = graph.nodes.size() + 1; budget > 0; --budget) {
        if (index == npos) return npos;
        const shape_kind kind = graph.nodes[index].entry.kind;
        if (kind != shape_kind::binder && kind != shape_kind::back) return index;
        index = graph.nodes[index].next;
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
//   choice    the same shape and the same note.  An output choice of T
//             has no more branches than U, and an input choice of T has
//             no fewer.  The branches both sides have refine
//             position by position.  The payload rules add the two
//             conditions of rule Sub-& that a payload registration
//             states.
//   wrapper   the same shape.  The value order follows the value
//             variance, then the inner types refine.
//
// A binder and a back node are unfolded before the comparison, so the
// relation is the coinductive one on the infinite unfoldings.  A pair
// that the walk meets a second time holds by assumption.  Every rule is
// a conjunction, so an assumption that later fails stops the whole
// walk, and one visited set serves the walk.  Complexity: each pair of
// graph nodes is visited once, so O(|T|·|U|) pairs.

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
    std::vector<std::uint8_t> visited(left.nodes.size() * width, 0);
    std::vector<std::size_t> pending{0, 0};
    while (!pending.empty()) {
        const std::size_t super_index = settle(right, pending.back());
        pending.pop_back();
        const std::size_t sub_index = settle(left, pending.back());
        pending.pop_back();
        if (sub_index == npos || super_index == npos) return {false, mismatch::unguarded, sub, super};
        std::uint8_t& seen = visited[sub_index * width + super_index];
        if (seen != 0) continue;
        seen = 1;
        const graph_node& a = left.nodes[sub_index];
        const graph_node& b = right.nodes[super_index];
        if (a.entry.shape != b.entry.shape) return {false, mismatch::shape, a.type, b.type};
        const std::size_t before = pending.size();
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
                pending.push_back(a.next);
                pending.push_back(b.next);
                break;
            case shape_kind::wrapper:
                if (!detail::value_in_order(a, b)) return {false, mismatch::value, a.type, b.type};
                pending.push_back(a.next);
                pending.push_back(b.next);
                break;
            case shape_kind::choice: {
                if (a.annotation != b.annotation) return {false, mismatch::annotation, a.type, b.type};
                const std::size_t own = a.child_count;
                const std::size_t other = b.child_count;
                const bool is_output = a.entry.direction == polarity::output;
                if (is_output ? own > other : own < other) return {false, mismatch::branch_count, a.type, b.type};
                if (!is_output) {
                    for (std::size_t extra = other; extra < own; ++extra) {
                        if (!left.nodes[left.children[a.first_child + extra]].is_label) {
                            return {false, mismatch::non_label_branch, a.type, b.type};
                        }
                    }
                    std::size_t labels = 0;
                    for (std::size_t k = 0; k < other; ++k) {
                        if (right.nodes[right.children[b.first_child + k]].is_label) ++labels;
                    }
                    if (other > 0 && labels == 0) return {false, mismatch::pure_non_label_choice, a.type, b.type};
                }
                const std::size_t shared = own < other ? own : other;
                for (std::size_t k = 0; k < shared; ++k) {
                    pending.push_back(left.children[a.first_child + k]);
                    pending.push_back(right.children[b.first_child + k]);
                }
                break;
            }
            default:
                break;
        }
        // Pairs are pushed in order and popped from the back, so the
        // walk reaches the last child first.  The pushed block is
        // reversed pair by pair to keep the first child first, which
        // makes the reported mismatch the leftmost one.
        for (std::size_t low = before, high = pending.size(); low + 2 < high; low += 2, high -= 2) {
            const std::size_t first_sub = pending[low];
            const std::size_t first_super = pending[low + 1];
            pending[low] = pending[high - 2];
            pending[low + 1] = pending[high - 1];
            pending[high - 2] = first_sub;
            pending[high - 1] = first_super;
        }
    }
    return {true, mismatch::none, {}, {}};
}

// ── Self-test over a stand-in registry ────────────────────────────────

namespace detail::transition_self_test {

template <class T, class K>
struct Put {};
template <class T, class K>
struct Take {};
template <class... Bs>
struct Pick {};
template <class... Bs>
struct Wait {};
template <class B>
struct Again {};
struct Back {};
struct Done {};
struct Halt {};
template <int V, class P>
struct Pin {};
template <class R>
struct From {};
template <class R>
struct Fault {};
struct Undeclared {};

template <int V>
inline constexpr bool pin_is_named_v = V != 0;

namespace registry {
inline constexpr combinator put{.shape = ^^Put,
                                .kind = shape_kind::step,
                                .direction = polarity::output,
                                .dual = ^^Take,
                                .payload_variance = variance::covariant};
inline constexpr combinator take{.shape = ^^Take,
                                 .kind = shape_kind::step,
                                 .direction = polarity::input,
                                 .dual = ^^Put,
                                 .payload_variance = variance::contravariant};
inline constexpr combinator pick{
    .shape = ^^Pick, .kind = shape_kind::choice, .direction = polarity::output, .dual = ^^Wait};
inline constexpr combinator wait{.shape = ^^Wait,
                                 .kind = shape_kind::choice,
                                 .direction = polarity::input,
                                 .dual = ^^Pick,
                                 .annotation = ^^From};
inline constexpr combinator again{.shape = ^^Again, .kind = shape_kind::binder, .dual = ^^Again};
inline constexpr combinator back{.shape = ^^Back, .kind = shape_kind::back, .dual = ^^Back};
inline constexpr combinator done{.shape = ^^Done, .kind = shape_kind::terminal, .dual = ^^Done};
inline constexpr combinator halt{.shape = ^^Halt, .kind = shape_kind::terminal, .dual = ^^Halt, .absorbs_suffix = true};
inline constexpr combinator pin{
    .shape = ^^Pin, .kind = shape_kind::wrapper, .dual = ^^Pin, .value_admits = ^^pin_is_named_v};
inline constexpr payload_rule fault{.shape = ^^Fault, .is_sendable = false, .is_label = false};
}  // namespace registry

namespace no_axioms {}

inline constexpr std::meta::info reg = ^^registry;

[[nodiscard]] consteval bool well_formed(std::meta::info type) {
    return fold(reg, type, well_formed_algebra{reg, {}}, {});
}
[[nodiscard]] consteval std::meta::info dual(std::meta::info type) { return fold(reg, type, dual_algebra{reg}, 0); }
[[nodiscard]] consteval bool refines_plain(std::meta::info sub, std::meta::info super) {
    return refines(reg, ^^no_axioms, sub, super).holds;
}

static_assert(check_registry(reg).reason == incoherence::none);

using Ping = Put<int, Take<char, Done>>;
using Pong = Take<int, Put<char, Done>>;
static_assert(dual(^^Ping) == std::meta::dealias(^^Pong) && dual(^^Pong) == std::meta::dealias(^^Ping));
static_assert(dual(^^Wait<From<int>, Done, Back>) == ^^Pick<Done, Back>);
static_assert(dual(^^Pin<3, Ping>) == ^^Pin<3, Pong>);

static_assert(well_formed(^^Ping));
static_assert(well_formed(^^Again<Put<int, Back>>));
static_assert(!well_formed(^^Back));
static_assert(!well_formed(^^Again<Back>), "a back node with no step above it is not guarded");
static_assert(!well_formed(^^Again<Pin<1, Back>>), "a wrapper is not a guard");
static_assert(!well_formed(^^Again<Put<int, Again<Back>>>), "the inner back node binds the inner binder");
static_assert(!well_formed(^^Again<Done>), "a binder body is not a terminal");
static_assert(!well_formed(^^Pin<0, Done>), "the value filter refuses the value 0");
static_assert(!well_formed(^^Put<Fault<int>, Done>), "a payload that is not sendable is not sent");
static_assert(well_formed(^^Take<Fault<int>, Done>));
static_assert(!well_formed(^^Undeclared), "an unregistered combinator is refused");
static_assert(first_unregistered(reg, ^^Put<int, Pick<Done, Undeclared>>) == ^^Undeclared);

static_assert(fold(reg, ^^Put<int, Pick<Done, Halt>>, compose_algebra{reg, ^^Ping}, 0)
              == ^^Put<int, Pick<Ping, Halt>>);
static_assert(fold(reg, ^^Put<int, Pick<Done, Done>>, compose_at_choice_algebra{reg, 1, ^^Ping, {}}, 0)
              == ^^Put<int, Pick<Done, Ping>>);
static_assert(fold(reg, ^^Put<int, Done>, compose_at_choice_algebra{reg, 0, ^^Ping, {}}, 0) == std::meta::info{});

static_assert(fold(reg, ^^Pick<>, empty_choice_algebra{reg}, 0));
static_assert(fold(reg, ^^Wait<Take<Fault<int>, Done>>, empty_choice_algebra{reg}, 0),
              "a choice made only of branches that are no labels is empty");
static_assert(!fold(reg, ^^Wait<Take<int, Done>, Take<Fault<int>, Done>>, empty_choice_algebra{reg}, 0));

static_assert(refines_plain(^^Ping, ^^Ping));
static_assert(refines_plain(^^Pick<Done>, ^^Pick<Done, Done>) && !refines_plain(^^Pick<Done, Done>, ^^Pick<Done>));
static_assert(refines_plain(^^Wait<Done, Done>, ^^Wait<Done>) && !refines_plain(^^Wait<Done>, ^^Wait<Done, Done>));
static_assert(refines_plain(^^Again<Put<int, Back>>, ^^Put<int, Again<Put<int, Back>>>),
              "the relation is on the unfoldings, so one unfold refines its loop");
static_assert(!refines_plain(^^Wait<From<int>, Done>, ^^Wait<Done>), "the notes differ");
static_assert(!refines_plain(^^Pin<1, Done>, ^^Pin<2, Done>), "an invariant value is compared for equality");
static_assert(refines(reg, ^^no_axioms, ^^Wait<Done, Take<Fault<int>, Done>>, ^^Wait<Done>).reason
              == mismatch::non_label_branch);
static_assert(refines(reg, ^^no_axioms, ^^Put<int, Done>, ^^Put<long, Done>).reason == mismatch::payload);

}  // namespace detail::transition_self_test

}  // namespace foundation::algebra::transition
