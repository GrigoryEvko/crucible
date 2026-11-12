#pragma once

// The registry of the transition algebra: the vocabulary of a
// registration, the read of a registry, the seal of a registry, and the
// coherence of one registration.
//
// fixy/session/Fold.h folds the algebras over a protocol, and it reads a
// node through this registry.  fixy/session/Wire.h gives the wire word of
// a choice, and fixy/session/Refinement.h gives the payload preorder and
// the refinement preorder.
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
//     inline constexpr fixy::session::transition::combinator put{
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
//                     for (the section on branches and labels of
//                     fixy/session/Fold.h).  The dual step names the dual
//                     choice.  A step with no keyed choice cannot carry a
//                     payload that names a label.
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

#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>  // also std::allocator, which <memory> declares too, and <memory> is a heavy header

namespace fixy::session::transition {

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
[[nodiscard]] consteval static_run<T> to_static_run(std::span<const T> values) {
    const std::span<const T> stored = std::define_static_array(values);
    return static_run<T>{stored.data(), stored.size()};
}

namespace detail {

// Storage for `count` elements in a constant evaluation.  An element whose
// default constructor does nothing is alive at once, because an array new
// of such a type initializes nothing and costs almost nothing.  Each
// other element needs a constructor call before its first read.
// release_storage_ ends the storage, and not the elements.
template <class T>
[[nodiscard]] consteval T* acquire_storage_(std::size_t count) {
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        return new T[count];
    } else {
        return std::allocator<T>{}.allocate(count);
    }
}

template <class T>
constexpr void release_storage_(T* items, std::size_t count) {
    if (items == nullptr) return;
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        delete[] items;
    } else {
        std::allocator<T>{}.deallocate(items, count);
    }
}

}  // namespace detail

// A stack for a constant evaluation, whose storage never becomes smaller.
// `items` points to `capacity` elements of storage.  The first `top`
// elements are the stack, and the first `made` elements are alive.  A pop
// lowers `top` and keeps the storage.
//
// The costs of this GCC 16 build set the form.  In a constant evaluation,
// one push_back with one pop_back of a vector costs about 455,000
// instructions, a read through operator[] about 30,000, and a read through
// a pointer about 10,000.  A vector of N elements also costs N element
// initializations when it is made, about 90,000 instructions for each
// std::size_t and about 300,000 for each graph_node below.  So a loop over
// a stack changes no vector.  A stack starts the life of an element only
// once, when it first reaches the element, and only when the default
// constructor of the type does something.  The storage becomes two times
// larger when it is full, so the count of its changes is logarithmic in
// the highest top.
//
// A copy or a move would keep `items` on the storage of the first stack,
// so a stack has neither.  The stack ends its storage and not its
// elements, so an element must be trivially destructible.
template <class T>
struct stack {
    static_assert(std::is_trivially_destructible_v<T>, "a stack ends its storage and not its elements");

    T* items = nullptr;
    std::size_t top = 0;
    std::size_t made = 0;
    std::size_t capacity = 0;

    constexpr stack() = default;
    stack(const stack&) = delete("items points into the storage of this stack");
    stack& operator=(const stack&) = delete("items points into the storage of this stack");
    constexpr ~stack() { detail::release_storage_(items, capacity); }

    // The live elements, so that a stack of reflections is a range that
    // std::meta::substitute reads with no copy.
    [[nodiscard]] constexpr const T* begin() const noexcept { return items; }
    [[nodiscard]] constexpr const T* end() const noexcept { return items + top; }
};

namespace detail {

// Gives the stack storage for `count` more elements, and keeps the
// elements under the top.  The elements above the top end.
template <class T>
consteval void grow(stack<T>& pile, std::size_t count) {
    if (pile.top + count <= pile.capacity) return;
    const std::size_t grown_capacity = 2 * (pile.top + count) + 14;
    T* const grown = acquire_storage_<T>(grown_capacity);
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        for (std::size_t index = 0; index < pile.top; ++index)
            grown[index] = pile.items[index];
        pile.made = grown_capacity;
    } else {
        for (std::size_t index = 0; index < pile.top; ++index)
            ::new(static_cast<void*>(grown + index)) T(pile.items[index]);
        pile.made = pile.top;
    }
    release_storage_(pile.items, pile.capacity);
    pile.items = grown;
    pile.capacity = grown_capacity;
}

}  // namespace detail

// Gives the stack space for `count` more elements, each alive, so the
// caller can write them through `items`.  An element whose default
// constructor does nothing has no value until the caller writes it.  A
// pointer or a reference into the stack is not valid after this call.
template <class T>
consteval void make_room(stack<T>& pile, std::size_t count) {
    detail::grow(pile, count);
    for (; pile.made < pile.top + count; ++pile.made)
        ::new(static_cast<void*>(pile.items + pile.made)) T();
}

// `value` must not be an element of the stack, because the stack can move
// its elements first.
template <class T>
consteval void push(stack<T>& pile, const T& value) {
    detail::grow(pile, 1);
    if (pile.top < pile.made) {
        pile.items[pile.top] = value;
    } else {
        ::new(static_cast<void*>(pile.items + pile.top)) T(value);
        pile.made = pile.top + 1;
    }
    ++pile.top;
}

// The live elements of a stack, in static storage.
template <class T>
[[nodiscard]] consteval static_run<T> to_static_run(const stack<T>& pile) {
    return to_static_run(std::span<const T>{pile.items, pile.top});
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

}  // namespace fixy::session::transition
