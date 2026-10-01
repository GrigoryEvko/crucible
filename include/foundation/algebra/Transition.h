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
// next to the declaration.  The fold reads the registry when it first
// meets a shape, and that answer holds for the rest of the translation
// unit.  A query that meets a shape which is not registered stops the
// build.  A second registration of a shape stops the build at the next
// read of the registry that is not cached.
//
// A registration that a different header adds, in a namespace that it
// opens again, gives a shape an answer in the translation units that see
// that header and none in the others.  A payload with no rule is a plain
// payload, so a payload rule that comes too late gives a different answer
// and no error.  A layer closes both gaps with a seal.  A seal is a
// variable of type `seal` in the namespace.  It names one kind of
// registration and states how many registrations of that kind the
// namespace holds.  Each read of a sealed kind counts the registrations
// again, and it stops the build when the count differs from the seal, or
// when the namespace holds two seals of that kind.  Each translation unit
// that compiles then reads the same registrations of that kind.
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
//   marker    Shape<Next>                 a mark on the spine, with no
//                                         message, then Next
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
//   keyed_choice      For a step, the registered choice of the same
//                     direction that a keyed step of this shape stands
//                     for (the section on branches and labels).  The dual
//                     step names the dual choice.  A step with no keyed
//                     choice cannot carry a payload that names a label.
//   annotation        The template of the note of a choice, or null.
//                     The dual shape must name the same template, so
//                     the dual keeps the note.  Refinement compares
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
//   is_plain          False: no plain protocol holds this combinator.
//                     Well-formedness refuses it at every depth, and
//                     every other algebra reads it.  A layer checks a
//                     protocol that holds it with a relation of its own.
//   payload_is_protocol  For a step.  True: the payload is a protocol,
//                     which travels as it is.  Well-formedness asks it of
//                     that protocol outside every binder, and the
//                     empty-choice test looks into it.
//
// Coherence.  Refinement up to exits must be closed under duality: when
// T refines U, the dual of U refines the dual of T (Padovani and
// Zavattaro, TOPLAS 2026, page 3, where this closure is what makes the
// type system sound).  Exit preservation, below the refinement preorder,
// is not closed under duality, and the layer states it on its own.  A
// registration keeps that closure when its dual has the same
// kind, the opposite direction, the opposite payload variance, the
// opposite value variance, the same absorption, the same note template,
// the same plainness and the same payload kind.  A plain combinator whose
// dual is not plain would make the dual of a well-formed protocol
// ill-formed.  The note rule makes duality an involution on a choice with
// a note, because the dual keeps the note.  The keyed choice of a step
// is a choice of the same direction, and the dual step names its dual,
// so a keyed step and its dual stand for dual choices.  A self-dual wrapper
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
//   label_key    An alias template `template <class T> using`, the label
//                that a payload of this shape names.  Two label branches
//                of one choice whose heads name the same label are not
//                well-formed.  Null: the payload names no label.
//   input_note   An alias template `template <class T> using`, the note
//                of the input choice that a keyed input step with this
//                payload stands for.  It must name the note template of
//                that choice.  Null: that choice has no note.
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
// payload, wherever it stands.  Six rules keep the kinds apart:
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
//   5. No two label words of one choice are equal.  Two distinct key
//      types can print one name, for example two closure types, and
//      then share a word.  The peer would enter the wrong branch.
//   6. In a keyed choice, each label branch is its label step, with no
//      wrapper and no binder above it.  The endpoint enters a keyed
//      branch past its label step, so nothing may stand above that step.
//      The label step of a branch that starts with a binder is also the
//      entry of the loop, where it is a choice of its own.
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
// second.  Refinement visits each pair of graph nodes at most once, so it
// is O(|T|·|U|) on the two type graphs (Udomsrirungruang and Yoshida,
// POPL 2025, section 5).  A graph has one node for each distinct subtree.

#include <foundation/Platform.h>
#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace foundation::algebra::transition {

// ── Registration vocabulary ───────────────────────────────────────────

enum class shape_kind : std::uint8_t {
    step,
    choice,
    binder,
    back,
    terminal,
    wrapper,
    marker
};

enum class polarity : std::uint8_t {
    neutral,
    output,
    input
};

enum class variance : std::uint8_t {
    invariant,
    covariant,
    contravariant
};

struct combinator {
    std::meta::info shape{};
    shape_kind kind = shape_kind::terminal;
    polarity direction = polarity::neutral;
    std::meta::info dual{};
    variance payload_variance = variance::invariant;
    std::meta::info keyed_choice{};
    std::meta::info annotation{};
    variance value_variance = variance::invariant;
    std::meta::info value_order{};
    std::meta::info value_admits{};
    bool absorbs_suffix = false;
    bool is_plain = true;
    bool payload_is_protocol = false;
};

struct payload_rule {
    std::meta::info shape{};
    bool is_sendable = true;
    bool is_label = true;
    std::meta::info label_key{};
    std::meta::info input_note{};
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

// The closure of one kind of registration in one namespace.  `kind` is
// ^^combinator, ^^payload_rule or ^^subsort_axiom, and `count` is the
// number of variables of that kind in the namespace.  The count is a
// literal, so a registration that stands before the seal is refused too.
struct seal {
    std::meta::info kind{};
    std::size_t count = 0;
};

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

// A run of elements in static storage.  It is a structural type, so a
// constant of it can be read back through reflection, and a variable
// template can keep it as the one answer for its arguments.
template <class T>
struct static_run {
    const T* data = nullptr;
    std::size_t length = 0;

    [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept { return data[index]; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return length; }
    [[nodiscard]] constexpr bool empty() const noexcept { return length == 0; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return data; }
    [[nodiscard]] constexpr const T* end() const noexcept { return data + length; }
};

template <class T>
[[nodiscard]] consteval static_run<T> to_static_run(const std::vector<T>& values) {
    const std::span<const T> stored = std::define_static_array(values);
    return static_run<T>{stored.data(), stored.size()};
}

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

// Each function below has no constant definition.  A call to one stops
// the constant evaluation, and the diagnostic names it.
void a_combinator_shape_has_two_registrations() noexcept;
void a_payload_shape_has_two_rules() noexcept;
void a_registration_stands_outside_its_seal() noexcept;
void a_kind_of_registration_has_two_seals() noexcept;

}  // namespace detail

enum class seal_fault : std::uint8_t {
    none,
    count_differs,
    sealed_twice
};

struct seal_reading {
    bool is_sealed = false;
    seal_fault fault = seal_fault::none;
    std::size_t sealed = 0;
    std::size_t found = 0;
};

// Reads the seal of one kind in a namespace, and counts the registrations
// of that kind.  A namespace with no seal of the kind is open, and it
// has no fault.  Complexity: linear in the members of the namespace.
[[nodiscard]] consteval seal_reading read_seal(std::meta::info registry, std::meta::info kind) {
    seal_reading result{};
    std::size_t seals = 0;
    for (const std::meta::info member : std::meta::members_of(registry, std::meta::access_context::unchecked())) {
        if (detail::is_registration_of(member, kind)) {
            ++result.found;
        } else if (detail::is_registration_of(member, ^^seal)) {
            const seal entry = std::meta::extract<seal>(member);
            if (entry.kind != kind) continue;
            ++seals;
            result.is_sealed = true;
            result.sealed = entry.count;
        }
    }
    if (seals > 1) {
        result.fault = seal_fault::sealed_twice;
    } else if (result.is_sealed && result.found != result.sealed) {
        result.fault = seal_fault::count_differs;
    }
    return result;
}

[[nodiscard]] consteval std::string_view seal_fault_name(seal_fault fault) {
    switch (fault) {
        case seal_fault::none:
            return "none";
        case seal_fault::count_differs:
            return "the namespace holds a different number of registrations of the kind than its seal states, so a "
                   "registration stands outside the seal";
        case seal_fault::sealed_twice:
            return "the namespace holds two seals of the kind";
        default:
            break;
    }
    return "an unknown fault";
}

namespace detail {

// Stops the build when a read of a sealed kind finds a fault.
consteval void require_seal_holds(std::meta::info registry, std::meta::info kind) {
    const seal_reading reading = read_seal(registry, kind);
    if (reading.fault == seal_fault::sealed_twice) a_kind_of_registration_has_two_seals();
    if (reading.fault == seal_fault::count_differs) a_registration_stands_outside_its_seal();
}

}  // namespace detail

// Reads the registry now.  A sealed registry must hold the registrations
// that its seal counts.  A shape with two registrations is counted in the
// answer, and check_combinator names it.  Two registrations of a
// different shape stop the build here: a read that is cached could not
// see the second one.  Complexity: quadratic in the registrations of the
// registry, which are few.
[[nodiscard]] consteval combinator_lookup read_combinator(std::meta::info registry, std::meta::info shape) {
    detail::require_seal_holds(registry, ^^combinator);
    combinator_lookup result{};
    std::vector<std::meta::info> other_shapes{};
    for (const std::meta::info member : std::meta::members_of(registry, std::meta::access_context::unchecked())) {
        if (!detail::is_registration_of(member, ^^combinator)) continue;
        const combinator entry = std::meta::extract<combinator>(member);
        if (entry.shape != shape) {
            for (const std::meta::info seen : other_shapes) {
                if (seen == entry.shape) detail::a_combinator_shape_has_two_registrations();
            }
            other_shapes.push_back(entry.shape);
            continue;
        }
        ++result.count;
        if (!result.is_found) {
            result.is_found = true;
            result.entry = entry;
        }
    }
    return result;
}

// Reads the payload rule of one shape now.  A sealed registry must hold
// the registrations that its seal counts, and two rules for one shape
// stop the build.  Complexity: linear in the members of the registry.
[[nodiscard]] consteval payload_lookup read_payload_rule(std::meta::info registry, std::meta::info shape) {
    detail::require_seal_holds(registry, ^^payload_rule);
    payload_lookup result{};
    for (const std::meta::info member : std::meta::members_of(registry, std::meta::access_context::unchecked())) {
        if (!detail::is_registration_of(member, ^^payload_rule)) continue;
        const payload_rule entry = std::meta::extract<payload_rule>(member);
        if (entry.shape != shape) continue;
        if (result.is_found) detail::a_payload_shape_has_two_rules();
        result.is_found = true;
        result.entry = entry;
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
    annotation_differs,
    direction_on_neutral_kind,
    order_on_non_wrapper,
    variance_against_direction,
    keyed_choice_on_non_step,
    keyed_choice_not_a_choice,
    keyed_choice_not_dual,
    plainness_differs,
    protocol_payload_differs,
    protocol_payload_on_non_step,
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

namespace detail {

// The first rule this registration breaks, read now.  check_combinator
// below keeps the answer for each shape.
[[nodiscard]] consteval coherence_verdict check_combinator_now(std::meta::info registry, std::meta::info shape) {
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
    if (mirror.annotation != entry.annotation) return {incoherence::annotation_differs, shape};
    if (mirror.is_plain != entry.is_plain) return {incoherence::plainness_differs, shape};
    if (entry.payload_is_protocol && entry.kind != shape_kind::step) {
        return {incoherence::protocol_payload_on_non_step, shape};
    }
    if (mirror.payload_is_protocol != entry.payload_is_protocol) return {incoherence::protocol_payload_differs, shape};
    // A pair can flip its variance under duality and still have each side
    // backwards.  An output that is contravariant lets the subtype send a
    // wider payload than the peer of the supertype receives.
    const bool is_backwards = (entry.direction == polarity::output && entry.payload_variance == variance::contravariant)
                           || (entry.direction == polarity::input && entry.payload_variance == variance::covariant);
    if (entry.kind == shape_kind::step && is_backwards) return {incoherence::variance_against_direction, shape};
    // A keyed step stands for a choice of its own direction, and its dual
    // stands for the dual of that choice.
    if (entry.kind != shape_kind::step) {
        if (entry.keyed_choice != std::meta::info{}) return {incoherence::keyed_choice_on_non_step, shape};
        return {};
    }
    if (entry.keyed_choice == std::meta::info{}) {
        if (mirror.keyed_choice != std::meta::info{}) return {incoherence::keyed_choice_not_dual, shape};
        return {};
    }
    const combinator_lookup keyed = lookup_combinator(registry, entry.keyed_choice);
    if (!keyed.is_found || keyed.entry.kind != shape_kind::choice || keyed.entry.direction != entry.direction) {
        return {incoherence::keyed_choice_not_a_choice, shape};
    }
    if (mirror.keyed_choice != keyed.entry.dual) return {incoherence::keyed_choice_not_dual, shape};
    return {};
}

}  // namespace detail

// The coherence of one shape, read once per translation unit.
template <std::meta::info Registry, std::meta::info Shape>
inline constexpr coherence_verdict coherence_v = detail::check_combinator_now(Registry, Shape);

// The first rule this registration breaks, or none.  A shape with no
// registration is not incoherent: the fold refuses it on its own.
[[nodiscard]] consteval coherence_verdict check_combinator(std::meta::info registry, std::meta::info shape) {
    return std::meta::extract<coherence_verdict>(std::meta::substitute(
        ^^coherence_v, {std::meta::reflect_constant(registry), std::meta::reflect_constant(shape)}));
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
        case incoherence::annotation_differs:
            return "the dual names a different note template, so the dual drops the note and duality is not an "
                   "involution";
        case incoherence::direction_on_neutral_kind:
            return "a step or a choice has no direction, or a different kind has one";
        case incoherence::order_on_non_wrapper:
            return "a value order or a value filter on a kind with no value";
        case incoherence::variance_against_direction:
            return "an output step with a contravariant payload, or an input step with a covariant payload, lets a "
                   "subtype send a payload that the peer does not receive";
        case incoherence::keyed_choice_on_non_step:
            return "a keyed choice on a combinator that is not a step";
        case incoherence::keyed_choice_not_a_choice:
            return "the keyed choice of a step is not a registered choice of the same direction";
        case incoherence::keyed_choice_not_dual:
            return "the dual step names a keyed choice that is not the dual of this keyed choice, so a keyed step and "
                   "its dual do not stand for dual choices";
        case incoherence::plainness_differs:
            return "the dual disagrees on whether a plain protocol may hold it, so the dual of a well-formed protocol "
                   "would not be well-formed";
        case incoherence::protocol_payload_differs:
            return "the dual disagrees on whether the payload is a protocol";
        case incoherence::protocol_payload_on_non_step:
            return "a combinator that is not a step names its payload a protocol, and it has no payload";
        default:
            break;
    }
    return "an unknown reason";
}

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
    switch (result.entry.kind) {
        case shape_kind::terminal:
        case shape_kind::back:
            result.is_malformed = has_arguments;
            break;
        case shape_kind::step:
            result.is_malformed =
                arguments.size() != 2 || !std::meta::is_type(arguments[0]) || !std::meta::is_type(arguments[1]);
            if (!result.is_malformed) {
                result.payload = std::meta::dealias(arguments[0]);
                result.next = std::meta::dealias(arguments[1]);
            }
            break;
        case shape_kind::binder:
        case shape_kind::marker:
            result.is_malformed = arguments.size() != 1 || !std::meta::is_type(arguments[0]);
            if (!result.is_malformed) result.next = std::meta::dealias(arguments[0]);
            break;
        case shape_kind::wrapper:
            result.is_malformed =
                arguments.size() != 2 || std::meta::is_type(arguments[0]) || !std::meta::is_type(arguments[1]);
            if (!result.is_malformed) {
                result.value = arguments[0];
                result.next = std::meta::dealias(arguments[1]);
            }
            break;
        case shape_kind::choice: {
            std::size_t first = 0;
            if (!arguments.empty() && result.entry.annotation != std::meta::info{} && std::meta::is_type(arguments[0])
                && shape_of(arguments[0]) == result.entry.annotation) {
                result.annotation = std::meta::dealias(arguments[0]);
                first = 1;
            }
            std::vector<std::meta::info> branches;
            for (std::size_t index = first; index < arguments.size(); ++index) {
                if (!std::meta::is_type(arguments[index])) {
                    result.is_malformed = true;
                    break;
                }
                branches.push_back(std::meta::dealias(arguments[index]));
            }
            result.branches = to_static_run(branches);
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

struct node_members {
    bool is_node = false;
    std::vector<member_claim> claims{};
    std::vector<std::meta::info> bases{};
    std::vector<std::meta::info> children{};
};

using node_reader = node_members (*)(std::meta::info);

// True when the first member of `type` with the identifier of `claim` has
// the claimed value: an alias of the claimed type, or a static data member
// with the claimed constant.  An inherited member is not a member here.
// The walk sees private members, so the reflection of a member stays in
// this function.
[[nodiscard]] consteval bool claim_holds(std::meta::info type, const member_claim& claim) {
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::unchecked())) {
        if (!std::meta::has_identifier(member) || std::meta::identifier_of(member) != claim.name) continue;
        if (std::meta::is_type(claim.value)) {
            return std::meta::is_type_alias(member) && std::meta::dealias(member) == std::meta::dealias(claim.value);
        }
        return std::meta::is_variable(member) && std::meta::constant_of(member) == claim.value;
    }
    return false;
}

// True when each claimed member of `type` exists with the claimed value,
// and the direct bases of `type` are exactly `bases`, in order.  A type
// that is only declared in this translation unit has no member that a
// reader can read, so it agrees.  Complexity: linear in the members of
// `type` for each claim.
[[nodiscard]] consteval bool members_agree(std::meta::info type, const std::vector<member_claim>& claims,
                                           const std::vector<std::meta::info>& bases) {
    if (!std::meta::is_complete_type(type)) return true;
    for (const member_claim& claim : claims) {
        if (!claim_holds(type, claim)) return false;
    }
    const std::vector<std::meta::info> actual = std::meta::bases_of(type, std::meta::access_context::unchecked());
    if (actual.size() != bases.size()) return false;
    for (std::size_t index = 0; index < bases.size(); ++index) {
        if (std::meta::dealias(std::meta::type_of(actual[index])) != std::meta::dealias(bases[index])) return false;
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
    const node_members view = read(node_type);
    if (!view.is_node) return {};
    if (!members_agree(node_type, view.claims, view.bases)) return node_type;
    for (const std::meta::info child : view.children) {
        const std::meta::info below = first_disagreeing_node(read, child);
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
    const std::uint64_t id = std::meta::extract<std::uint64_t>(
        std::meta::substitute(^^::foundation::reflect::stable_type_id, {std::meta::dealias(key)}));
    return id | label_word_bit;
}

// The head of one branch of a choice, under its wrappers.  `payload` is
// the payload of a head step, or null.  `label_key` is the label that
// the payload names through its registration, or null, and
// `label_word` is the word of that key, or zero.  `is_at_root` is false
// when a wrapper or a binder stands above the head step.
struct branch_head {
    bool is_label = true;
    std::meta::info payload{};
    std::meta::info label_key{};
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
        result.label_word = label_word_of(result.label_key);
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
    std::vector<branch_head> heads;
    for (const std::meta::info branch : choice.branches)
        heads.push_back(detail::head_of_branch(registry, branch));
    bool has_label = false;
    bool non_label_seen = false;
    for (const branch_head& head : heads) {
        if (head.is_label) {
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
    for (const branch_head& head : heads) {
        if (!head.is_label) continue;
        ++labels;
        if (head.label_key != std::meta::info{}) ++keyed;
    }
    if (keyed != 0 && keyed != labels) return choice_fault::mixed_label_keys;
    for (const branch_head& head : heads) {
        if (keyed != 0 && head.is_label && !head.is_at_root) return choice_fault::keyed_label_below_root;
    }
    for (std::size_t first = 0; first < heads.size(); ++first) {
        for (std::size_t second = first + 1; second < heads.size(); ++second) {
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
            return "two label keys of the choice have one label word, so the peer cannot tell the two labels apart. "
                   "Two distinct types that print one name, for example two closure types, share a word";
        case choice_fault::keyed_label_below_root:
            return "a label branch of a keyed choice starts with a wrapper or a binder, not with its label step.  The "
                   "endpoint enters the branch past the label word of that step, so a wrapper or a loop entry above "
                   "the step has no place on the wire";
        default:
            break;
    }
    return "an unknown fault";
}

// ── The wire word of a branch ─────────────────────────────────────────

// True when each label branch of the choice names a label key.  The
// choice must be well-formed.  Complexity: linear in the branches.
[[nodiscard]] consteval bool is_keyed_choice(std::meta::info registry, const node& choice) {
    bool has_label = false;
    for (const std::meta::info branch : choice.branches) {
        const branch_head head = detail::head_of_branch(registry, branch);
        if (!head.is_label) continue;
        has_label = true;
        if (head.label_key == std::meta::info{}) return false;
    }
    return has_label;
}

// The word that picks one branch of a choice.  A branch that is no
// label has no word, and `is_wired` is false for it.
struct wire_word {
    bool is_wired = false;
    std::uint64_t value = 0;
};

// The word of branch `index` of the choice at the head of `choice`: the
// label word in a keyed choice, and the position in a positional one.
// An index past the last branch has no word.  Complexity: linear in the
// branches.
[[nodiscard]] consteval wire_word wire_word_of(std::meta::info registry, std::meta::info choice, std::size_t index) {
    const node view = decompose(registry, choice);
    if (!view.is_registered || view.entry.kind != shape_kind::choice || index >= view.branches.size()) return {};
    const branch_head head = detail::head_of_branch(registry, view.branches[index]);
    if (!head.is_label) return {};
    if (is_keyed_choice(registry, view)) return {true, head.label_word};
    return {true, static_cast<std::uint64_t>(index)};
}

// True when the choice at the head of `choice` is a keyed choice.
[[nodiscard]] consteval bool is_keyed_choice_type(std::meta::info registry, std::meta::info choice) {
    const node view = decompose(registry, choice);
    return view.is_registered && view.entry.kind == shape_kind::choice && is_keyed_choice(registry, view);
}

// True when the node is a step whose payload names a label key.  Outside
// a choice such a step is a choice with one branch.
[[nodiscard]] consteval bool is_keyed_step(std::meta::info registry, const node& step) {
    return detail::step_label_key(registry, step) != std::meta::info{};
}

[[nodiscard]] consteval bool is_keyed_step_type(std::meta::info registry, std::meta::info step) {
    return is_keyed_step(registry, decompose(registry, step));
}

// The word that a keyed step sends or expects: the label word of its key.
// A step that is not keyed has no word.
[[nodiscard]] consteval wire_word wire_word_of_step(std::meta::info registry, std::meta::info step) {
    const std::meta::info key = detail::step_label_key(registry, decompose(registry, step));
    if (key == std::meta::info{}) return {};
    return {true, label_word_of(key)};
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
    for (const std::meta::info branch : mapped)
        arguments.push_back(branch);
    return arguments;
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
        std::vector<std::meta::info> mapped;
        for (const std::meta::info branch : view.branches)
            mapped.push_back(child(branch, ctx));
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
        std::vector<std::meta::info> mapped;
        for (const std::meta::info branch : view.branches)
            mapped.push_back(child(branch, ctx));
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
        std::vector<std::meta::info> mapped;
        for (const std::meta::info branch : view.branches)
            mapped.push_back(child(branch, ctx));
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
        std::vector<std::meta::info> mapped(view.branches.begin(), view.branches.end());
        mapped[index] = fold(registry, mapped[index], compose_algebra{registry, suffix}, 0);
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

// `built` lists each node that the build made, so an occurrence of a
// subtree that the graph holds reads its node.
struct type_graph {
    std::vector<graph_node> nodes{};
    std::vector<std::size_t> children{};
    std::vector<std::size_t> sorted_labels{};
    std::meta::info unregistered{};
    std::vector<built_node> built{};
};

namespace detail {

consteval std::size_t add_to_graph(std::meta::info registry, type_graph& graph, std::meta::info type,
                                   std::vector<std::size_t>& binders, bool is_branch);

// The node that the graph holds for this identity, or npos.  Complexity:
// linear in the nodes of the graph.  A protocol has few distinct
// subtrees, and each compare is one compare of a reflection.
[[nodiscard]] consteval std::size_t find_built(const type_graph& graph, std::meta::info type, bool is_branch,
                                               std::size_t scope) {
    for (const built_node& made : graph.built) {
        if (made.type == type && made.is_branch == is_branch && made.scope == scope) return made.index;
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
consteval std::size_t add_keyed_step(std::meta::info registry, type_graph& graph, const node& step,
                                     std::vector<std::size_t>& binders) {
    const combinator choice = lookup_combinator(registry, step.entry.keyed_choice).entry;
    const std::size_t here = graph.nodes.size();
    graph.nodes.push_back(graph_node{});
    graph.nodes[here].type = step.type;
    graph.nodes[here].entry = choice;
    graph.nodes[here].annotation = keyed_step_note(registry, graph, step, choice);
    const branch_head head = head_of_branch(registry, step.type);
    graph.nodes[here].is_label = head.is_label;
    graph.nodes[here].head_payload = head.payload;
    graph.nodes[here].label_key = head.label_key;
    graph.nodes[here].label_word = head.label_word;
    const std::size_t branch = add_to_graph(registry, graph, step.type, binders, true);
    graph.nodes[here].first_child = graph.children.size();
    graph.nodes[here].child_count = 1;
    graph.children.push_back(branch);
    graph.nodes[here].is_keyed = true;
    graph.nodes[here].first_label = graph.sorted_labels.size();
    graph.nodes[here].label_count = 1;
    graph.sorted_labels.push_back(branch);
    return here;
}

// `is_branch` is true for a branch of a choice, where a keyed step is the
// branch itself and not a choice of its own.  An occurrence whose identity
// the graph holds reads that node, and the build does not enter it again.
consteval std::size_t add_to_graph(std::meta::info registry, type_graph& graph, std::meta::info type,
                                   std::vector<std::size_t>& binders, bool is_branch) {
    const node view = decompose(registry, type);
    const std::size_t scope = binders.empty() || !has_open_back(registry, view.type) ? npos : binders.back();
    const std::size_t shared = find_built(graph, view.type, is_branch, scope);
    if (shared != npos) return shared;
    graph.built.push_back(built_node{view.type, is_branch, scope, graph.nodes.size()});
    if (!is_branch && view.is_registered && view.entry.keyed_choice != std::meta::info{}
        && is_keyed_step(registry, view)) {
        return add_keyed_step(registry, graph, view, binders);
    }
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
    const branch_head head = head_of_branch(registry, view.type);
    graph.nodes[here].is_label = head.is_label;
    graph.nodes[here].head_payload = head.payload;
    graph.nodes[here].label_key = head.label_key;
    graph.nodes[here].label_word = head.label_word;
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
        case shape_kind::wrapper:
        case shape_kind::marker: {
            const std::size_t below = add_to_graph(registry, graph, view.next, binders, false);
            graph.nodes[here].next = below;
            break;
        }
        case shape_kind::binder: {
            binders.push_back(here);
            const std::size_t below = add_to_graph(registry, graph, view.next, binders, false);
            binders.pop_back();
            graph.nodes[here].next = below;
            break;
        }
        case shape_kind::choice: {
            std::vector<std::size_t> own;
            for (const std::meta::info branch : view.branches) {
                own.push_back(add_to_graph(registry, graph, branch, binders, true));
            }
            graph.nodes[here].first_child = graph.children.size();
            graph.nodes[here].child_count = own.size();
            for (const std::size_t index : own)
                graph.children.push_back(index);
            if (is_keyed_choice(registry, view)) {
                // The label branches in the order of their label words, by
                // an insertion sort, because a choice has few branches.
                // Complexity: quadratic in the label branches of the choice.
                std::vector<std::size_t> labels;
                for (const std::size_t index : own) {
                    if (graph.nodes[index].is_label) labels.push_back(index);
                }
                for (std::size_t outer = 1; outer < labels.size(); ++outer) {
                    for (std::size_t inner = outer; inner > 0; --inner) {
                        if (graph.nodes[labels[inner - 1]].label_word <= graph.nodes[labels[inner]].label_word) break;
                        const std::size_t moved = labels[inner - 1];
                        labels[inner - 1] = labels[inner];
                        labels[inner] = moved;
                    }
                }
                graph.nodes[here].is_keyed = true;
                graph.nodes[here].first_label = graph.sorted_labels.size();
                graph.nodes[here].label_count = labels.size();
                for (const std::size_t index : labels)
                    graph.sorted_labels.push_back(index);
            }
            break;
        }
        default:
            break;
    }
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
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t index = graph.nodes.size(); index-- > 0;) {
            graph_node& current = graph.nodes[index];
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
                    reaches = current.next != npos && graph.nodes[current.next].can_end;
                    break;
                case shape_kind::choice:
                    for (std::size_t k = 0; k < current.child_count && !reaches; ++k) {
                        reaches = graph.nodes[graph.children[current.first_child + k]].can_end;
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

[[nodiscard]] consteval type_graph build_graph(std::meta::info registry, std::meta::info type) {
    type_graph graph{};
    std::vector<std::size_t> binders;
    detail::add_to_graph(registry, graph, type, binders, false);
    detail::mark_can_end(graph);
    return graph;
}

// A graph in static storage.  The graph of one protocol is built once
// per translation unit, and each relation that reads it shares it.
struct graph_view {
    static_run<graph_node> nodes{};
    static_run<std::size_t> children{};
    static_run<std::size_t> sorted_labels{};
    std::meta::info unregistered{};
};

[[nodiscard]] consteval graph_view freeze(const type_graph& graph) {
    return graph_view{to_static_run(graph.nodes), to_static_run(graph.children), to_static_run(graph.sorted_labels),
                      graph.unregistered};
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

// The branches of one choice node, as graph indices: the label branches
// in their order, and the branches that are no label.
struct split_branches {
    std::vector<std::size_t> labels{};
    std::vector<std::size_t> non_labels{};
};

[[nodiscard]] consteval split_branches split_of(const graph_view& graph, const graph_node& choice) {
    split_branches result{};
    for (std::size_t k = 0; k < choice.child_count; ++k) {
        const std::size_t child = graph.children[choice.first_child + k];
        if (graph.nodes[child].is_label) {
            result.labels.push_back(child);
        } else {
            result.non_labels.push_back(child);
        }
    }
    return result;
}

// The branch among `candidates` whose head receives `payload`, or npos.
// Complexity: linear in the candidates.
[[nodiscard]] consteval std::size_t partner_of(const graph_view& graph, const std::vector<std::size_t>& candidates,
                                               std::meta::info payload) {
    for (const std::size_t candidate : candidates) {
        if (graph.nodes[candidate].head_payload == payload) return candidate;
    }
    return npos;
}

// The label pairs of two keyed choices, as graph indices, sub then super
// for each pair, or the reason the labels do not refine.
struct label_pairing {
    mismatch reason = mismatch::none;
    std::vector<std::size_t> pairs{};
};

// True when two label branches of one keyed choice have one label word,
// with one key or with two.  The run of the choice is in the order of the
// label words, so a repeated word stands next to its twin.
// Complexity: linear in the label branches of the choice.
[[nodiscard]] consteval bool repeats_a_label_word(const graph_view& graph, const graph_node& choice) {
    for (std::size_t rank = 1; rank < choice.label_count; ++rank) {
        const std::size_t earlier = graph.sorted_labels[choice.first_label + rank - 1];
        const std::size_t later = graph.sorted_labels[choice.first_label + rank];
        if (graph.nodes[earlier].label_word == graph.nodes[later].label_word) return true;
    }
    return false;
}

// Pairs the label branches of two keyed choices by label, in one merge of
// the two runs that the graphs sorted by label word.  An output choice of
// the subtype sends only labels that the supertype sends, and an input
// choice of the supertype receives only labels that the subtype receives.
// Two branches with one word pair only when their keys are equal too.  A
// choice that repeats a label word is not well-formed, and the merge
// refuses it also when the layer above did not check well-formedness.
// Complexity: linear in the label branches of the two choices.
[[nodiscard]] consteval label_pairing pair_by_label(const graph_view& sub_graph, const graph_node& sub,
                                                    const graph_view& super_graph, const graph_node& super,
                                                    bool is_output) {
    label_pairing result{};
    if (repeats_a_label_word(sub_graph, sub) || repeats_a_label_word(super_graph, super)) {
        result.reason = mismatch::ill_formed;
        return result;
    }
    std::size_t sub_rank = 0;
    std::size_t super_rank = 0;
    while (sub_rank < sub.label_count || super_rank < super.label_count) {
        const bool has_sub = sub_rank < sub.label_count;
        const bool has_super = super_rank < super.label_count;
        const std::size_t sub_index = has_sub ? sub_graph.sorted_labels[sub.first_label + sub_rank] : npos;
        const std::size_t super_index = has_super ? super_graph.sorted_labels[super.first_label + super_rank] : npos;
        const std::uint64_t sub_word = has_sub ? sub_graph.nodes[sub_index].label_word : 0;
        const std::uint64_t super_word = has_super ? super_graph.nodes[super_index].label_word : 0;
        if (has_sub && has_super && sub_word == super_word) {
            if (sub_graph.nodes[sub_index].label_key != super_graph.nodes[super_index].label_key) {
                result.reason = mismatch::label_word_clash;
                return result;
            }
            result.pairs.push_back(sub_index);
            result.pairs.push_back(super_index);
            ++sub_rank;
            ++super_rank;
        } else if (has_sub && (!has_super || sub_word < super_word)) {
            // A label of the subtype that the supertype does not have.
            if (is_output) {
                result.reason = mismatch::label_set;
                return result;
            }
            ++sub_rank;
        } else {
            // A label of the supertype that the subtype does not have.
            if (!is_output) {
                result.reason = mismatch::label_set;
                return result;
            }
            ++super_rank;
        }
    }
    return result;
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
    // The visited pairs in the order of the walk, and the edges of the
    // product between them, each as a pair key sub * width + super.
    std::vector<std::size_t> order;
    std::vector<std::size_t> edge_from;
    std::vector<std::size_t> edge_to;
    std::vector<std::size_t> pending{0, 0};
    while (!pending.empty()) {
        const std::size_t super_index = settle(right, pending.back());
        pending.pop_back();
        const std::size_t sub_index = settle(left, pending.back());
        pending.pop_back();
        if (sub_index == npos || super_index == npos) return {false, mismatch::unguarded, sub, super};
        const std::size_t key = sub_index * width + super_index;
        std::uint8_t& seen = visited[key];
        if (seen != 0) continue;
        seen = 1;
        order.push_back(key);
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
            case shape_kind::marker:
                pending.push_back(a.next);
                pending.push_back(b.next);
                break;
            case shape_kind::choice: {
                if (a.annotation != b.annotation) return {false, mismatch::annotation, a.type, b.type};
                const detail::split_branches own = detail::split_of(left, a);
                const detail::split_branches other = detail::split_of(right, b);
                if (!other.non_labels.empty() && other.labels.empty()) {
                    return {false, mismatch::pure_non_label_choice, a.type, b.type};
                }
                // A choice with no label branch is not well-formed.  The
                // layer refuses it first, and the relation refuses it too,
                // so a layer that skips the check stays sound.
                if (own.labels.empty() || other.labels.empty()) return {false, mismatch::ill_formed, a.type, b.type};
                const bool is_output = a.entry.direction == polarity::output;
                if (a.is_keyed != b.is_keyed) return {false, mismatch::label_discipline, a.type, b.type};
                if (a.is_keyed) {
                    const detail::label_pairing pairing = detail::pair_by_label(left, a, right, b, is_output);
                    if (pairing.reason != mismatch::none) return {false, pairing.reason, a.type, b.type};
                    for (const std::size_t index : pairing.pairs)
                        pending.push_back(index);
                } else {
                    const bool count_is_wrong =
                        is_output ? own.labels.size() > other.labels.size() : own.labels.size() < other.labels.size();
                    if (count_is_wrong) return {false, mismatch::branch_count, a.type, b.type};
                    const std::size_t shared =
                        own.labels.size() < other.labels.size() ? own.labels.size() : other.labels.size();
                    for (std::size_t k = 0; k < shared; ++k) {
                        pending.push_back(own.labels[k]);
                        pending.push_back(other.labels[k]);
                    }
                }
                for (const std::size_t mine : own.non_labels) {
                    if (detail::partner_of(right, other.non_labels, left.nodes[mine].head_payload) == npos) {
                        return {false, mismatch::non_label_branch, a.type, b.type};
                    }
                }
                for (const std::size_t theirs : other.non_labels) {
                    const std::size_t mine = detail::partner_of(left, own.non_labels, right.nodes[theirs].head_payload);
                    if (mine == npos) return {false, mismatch::missing_non_label_branch, a.type, b.type};
                    pending.push_back(mine);
                    pending.push_back(theirs);
                }
                break;
            }
            default:
                break;
        }
        // Each child pair is an edge of the product.  A child that does
        // not settle stops the walk when it is popped.
        for (std::size_t k = before; k + 1 < pending.size(); k += 2) {
            const std::size_t child_sub = settle(left, pending[k]);
            const std::size_t child_super = settle(right, pending[k + 1]);
            if (child_sub == npos || child_super == npos) continue;
            edge_from.push_back(key);
            edge_to.push_back(child_sub * width + child_super);
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
    // Exit preservation.  A pair of terminals can end, and a pair with an
    // edge to a pair that can end can end.  The edges are recorded in the
    // order of the walk, so a pass from the last edge back settles every
    // edge of the tree, and each further pass settles one more loop-back.
    std::vector<std::uint8_t> pair_can_end(visited.size(), 0);
    for (const std::size_t key : order) {
        if (left.nodes[key / width].entry.kind == shape_kind::terminal) pair_can_end[key] = 1;
    }
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t edge = edge_from.size(); edge-- > 0;) {
            if (pair_can_end[edge_from[edge]] != 0 || pair_can_end[edge_to[edge]] == 0) continue;
            pair_can_end[edge_from[edge]] = 1;
            is_changed = true;
        }
    }
    for (const std::size_t key : order) {
        const graph_node& a = left.nodes[key / width];
        const graph_node& b = right.nodes[key % width];
        if (b.can_end && pair_can_end[key] == 0) return {false, mismatch::loses_termination, a.type, b.type};
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

}  // namespace foundation::algebra::transition
