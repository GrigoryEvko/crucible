#pragma once

// The linear marker of the base, and the vocabulary of its doors.
//
// A resource is a linear value: each path uses it exactly one time.  A
// class tells that its values are linear with one P3394 annotation:
//
//     class [[= ::foundation::linear::linear{}]] Token { ... };
//
// Reflection reads the annotation through std::meta::annotations_of.  The
// quarantine plugin can read the same annotation in the tree of GCC.
// There, each annotation is an attribute, and its value has the type of
// the annotation.  So the trait of this header and the rules of the plugin
// that phase A of misc/12_10_2026_linear.md adds read one fact.  No macro
// writes the annotation, so a build with no plugin reads it too.
//
// A class template whose instances are linear only for some arguments
// writes linear_when<CONDITION> in place of linear{}.  An instance whose
// condition is false carries the inert value not_linear{}.
//
// A class is linear when it carries the annotation, or when it can move
// and it holds a linear base, a linear member or an array of linear
// elements by value.  A union counts as a class.  A class that cannot move
// is not linear by its members, because no move can consume it: its
// destructor is the door at the end of its scope.  A pointer, a reference
// and a template argument hold no value, so they make no class linear.
//
// A marked class has the shape of a linear class, and the trait refuses a
// marked class without it.  The class is not trivially copyable, so
// std::bit_cast refuses it and a move of an Option copies no bytes of it.
// It is not copy constructible, and its move constructor and destructor
// do not throw.  A user-provided move assignment, or the member
// foundation::lifetime::byte_seal, gives the first fact.  The move
// constructor and the destructor can stay trivial, so the ABI passes the
// class in registers.
//
// Each exit from linearity is a named door.  The annotations of this
// header name the doors: drop_door and detach_door on a function, and
// scope_end_door on a class whose destructor releases its resource.  The
// annotation once_door marks a function that owns a closure and calls it one
// time, such as call_once, the one door of a closure that moves a linear
// capture out.  The door exchange is the one move out of a linear value that
// a function borrows by a reference that is not const.  The door
// end_unrun_body is the one end of a body that no door called and that holds
// a linear value by value, with the reason of the caller.  The
// annotation authority marks a class whose value conveys an authority,
// which the gate of foundation/effects/Computation.h reads.  The annotation
// binding_hook marks the hook get<I> of a structured binding, such as the
// hook of foundation/core/Record.h.  Only the initializer of a name of a
// binding calls such a function, and the rule binding-hook of the plugin
// refuses each other call.  The annotation borrow_match marks the match of a
// sum that gives each arm a reference into the payload, such as the match of
// a const Option.  The rule borrow-match of the plugin refuses an arm that
// assigns, moves or changes the sum that it reads.  The annotation sum marks
// a sum class, and the annotations empties, fills, tests_full and tests_empty
// mark the members that change or test the empty state of a sum.  The rule
// must-consume reads them, and it refuses a linear sum that ends with a
// payload that no consumer took.
//
// The trait reads a closure as a class: the sixth GCC patch shows the field
// of each capture, so a closure that holds a linear value by copy is linear.
// What the trait does not see, stated rather than implied: a value behind
// type erasure.

#include <foundation/ByteSeal.h>
#include <foundation/reflect/Annotation.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstdint>
#include <meta>
#include <type_traits>

namespace foundation::linear {

// The annotation of a linear class.
struct linear final {};

// The value of linear_when<false>: an annotation that marks nothing.
struct not_linear final {};

// The annotation of a linear class whose end with no consume is a defect,
// such as a live fixy::Linear<T>, whose tracker reports the open obligation.
// Such a class marks no end of its own (marks_own_end), so no door that ends
// a value ends it, such as drop or end_unrun_body.  It ends only at its own
// consumer.
struct owes_consume final {};

// The value of owes_consume_when<false>: an annotation that marks nothing.
struct owes_no_consume final {};

// The annotation of a class whose value conveys an authority, such as a
// permission token.
struct authority final {};

// The annotation of a function that ends a linear value and reports
// nothing.
struct drop_door final {};

// The annotation of a function that ends a linear value with a reason.
struct detach_door final {};

// The annotation of a class whose destructor releases its resource.  The
// end of the scope of a value of the class is then its named door.
struct scope_end_door final {};

// The annotation of a function that owns a closure and calls it one time,
// such as call_once.  The rule must-consume reads a call of such a function
// as one call of the closure, and the end of the closure after it.
struct once_door final {};

// The annotation of a function of the base that reaches a payload in storage
// that it owns, or in its erased frame.  The rule must-consume reads a call of
// such a function on the way to a move, and a move in its body, as no move out
// of a borrow.  The rule trusts the annotation only in a base file.
struct moves_own_storage final {};

// The annotation of a door of the base whose body gives its object and a
// borrow of the same object to one call, such as a frame door that holds an
// owner and lends its run.  The door keeps the two names apart by its own
// construction.  The kind two-names of the rule view-escape reads no call in
// the body of such a door.  The rule trusts the annotation only in a base file.
struct two_names_door final {};

// The annotation of the hook get<I> of a structured binding.  A direct call
// on an rvalue moves one part out and drops the other parts.
struct binding_hook final {};

// The annotation of a match that gives each arm a reference into the
// payload of the sum.  A write of the sum in an arm ends that payload.  The
// loop door begin() of an Option carries it too, and the body of the loop
// is then such an arm.
struct borrow_match final {};

// The annotation of a sum class: a class that holds one of some
// alternatives, such as an Option, a Result or a OneOf.  A linear instance
// gives its linear alternative out only through a consumer.  So the rule
// must-consume refuses each linear instance that ends with no consume, and
// each temporary of one that the code discards, in each build.
struct sum final {};

// The annotations of the empty state of a sum, such as the state "no value"
// of an Option.  The rule must-consume reads them, so an empty Option of a
// linear payload can end with no consume.  empties marks a constructor that
// builds an empty sum, and a member after which the sum is empty, such as
// take.  fills marks a member after which the sum holds a payload, such as
// fill and replace, and a static member that builds a full sum, such as
// some.  tests_full marks a const member that gives true when the sum holds
// a payload, and tests_empty a const member that gives true when the sum is
// empty.  needs_empty marks a member that ends the process on a full sum,
// such as fill and expect_none, and needs_full a member that ends the
// process on an empty sum, such as expect.  The rule refuses such a call on
// a sum whose content it knows, so the run-time check stays only for a sum
// whose content the rule does not know.
struct empties final {};
struct fills final {};
struct tests_full final {};
struct tests_empty final {};
struct needs_empty final {};
struct needs_full final {};

// The annotation value of a class template whose instances are linear
// only for some arguments.
template <bool IsLinear>
inline constexpr auto linear_when = not_linear{};

template <>
inline constexpr auto linear_when<true> = linear{};

// The annotation value of a class template whose instances owe a consume at
// their end only for some arguments, such as the grade One of fixy::Qtt.
template <bool OwesConsume>
inline constexpr auto owes_consume_when = owes_no_consume{};

template <>
inline constexpr auto owes_consume_when<true> = owes_consume{};

// True when the class or union carries the annotation linear.
[[nodiscard]] consteval bool is_marked_linear(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    if (!std::meta::is_class_type(bare) && !std::meta::is_union_type(bare)) return false;
    return ::foundation::reflect::carries_annotation(bare, ^^linear);
}

// True when the marked class has the shape of a linear class.
[[nodiscard]] consteval bool has_linear_shape(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    return !std::meta::is_trivially_copyable_type(bare) && !std::meta::is_copy_constructible_type(bare)
        && std::meta::is_nothrow_move_constructible_type(bare) && std::meta::is_nothrow_destructible_type(bare);
}

// True when the type is linear, by its annotation or by its parts.  A
// marked class without the shape of a linear class stops the build here,
// so no reader of the trait gets an answer for it.  Complexity: linear in
// the number of bases and members that the type holds by value.
[[nodiscard]] consteval bool is_linear_type(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    if (std::meta::is_array_type(bare)) return is_linear_type(std::meta::remove_all_extents(bare));
    if (!std::meta::is_class_type(bare) && !std::meta::is_union_type(bare)) return false;
    if (::foundation::reflect::carries_annotation(bare, ^^linear)) {
        if (!has_linear_shape(bare)) {
            throw std::meta::exception(u8"linear: a class carries the annotation linear and does not have the shape "
                                       u8"of a linear class.  Make it move-only with a move constructor and a "
                                       u8"destructor that do not throw, and give it a user-provided move "
                                       u8"assignment so that it is not trivially copyable.",
                                       bare);
        }
        return true;
    }
    if (!std::meta::is_move_constructible_type(bare)) return false;
    for (const ::foundation::reflect::TypeNode& part :
         ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{bare, true})) {
        if (part.is_held_by_value && is_linear_type(part.type)) return true;
    }
    return false;
}

// True when T is linear.  The trait reads the type with its cv-qualifiers
// and its reference removed, and the type must be complete.  Only a
// translation unit that reads the trait evaluates it.  A gate reads the
// function is_linear_type, because no unit can specialize a function that
// is not a template.
template <class T>
inline constexpr bool is_linear_v = is_linear_type(^^T);

// A linear value that a door can take: an object of a linear type, not
// const, passed as an rvalue.
template <class T>
concept LinearValue = !std::is_reference_v<T> && !std::is_const_v<T> && is_linear_type(^^T);

// The body of an element door or a frame door holds no linear value by
// value.  The door calls the body, and a call is no consume, so a linear
// capture would end with the body after the call, where no rule reads it.
// The caller keeps the linear value as a local, and the body borrows it with
// a capture by reference or takes it as a parameter.  Each such door has a
// deleted twin that refuses a body that holds one.  A class with a trivial
// copy constructor holds no linear value, because the copy of a linear value
// is deleted, so the test reads that trait first and walks the members only
// for each other class.
template <class Body>
concept HoldsNoLinearValue =
    std::is_trivially_copy_constructible_v<std::remove_cvref_t<Body>> || !is_linear_type(^^std::remove_cvref_t<Body>);

// The reason of the deleted twin of each element door and frame door, for a
// body that holds a linear value (HoldsNoLinearValue).
#define CRUCIBLE_LINEAR_BODY_REFUSAL                                                                                \
    "the body holds a linear value by value, and the door calls it with no consume: keep the linear value a local " \
    "of the caller, and let the body capture it by reference or take it as a parameter"

// True when the class owes a consume at its end (owes_consume).
[[nodiscard]] consteval bool owes_consume_at_end(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    return std::meta::is_class_type(bare) && ::foundation::reflect::carries_annotation(bare, ^^owes_consume);
}

// True when the class marks its own end: it carries the annotation linear, it
// is no sum, it owes no consume at its end, and no type argument of its
// template is linear.  A class that is linear only by the values that it
// holds, such as a class with a linear member, a closure with a linear
// capture, an Option or a Record of a token, marks no end of its own.  Its end
// drops each linear value that it holds, and no rule reads the end of such a
// value in the body of a door.  Complexity: linear in the template arguments
// of the class.
[[nodiscard]] consteval bool marks_own_end(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    if (!is_marked_linear(bare) || ::foundation::reflect::carries_annotation(bare, ^^sum)
        || ::foundation::reflect::carries_annotation(bare, ^^owes_consume)) {
        return false;
    }
    for (const ::foundation::reflect::TypeNode& argument :
         ::foundation::reflect::argument_components_of(::foundation::reflect::TypeNode{bare, false})) {
        const bool is_held_type =
            !argument.is_value_argument
            && (!std::meta::is_class_type(argument.type) || std::meta::is_complete_type(argument.type));
        if (is_held_type && is_linear_type(argument.type)) return false;
    }
    return true;
}

[[nodiscard]] consteval bool parts_state_own_end(std::meta::info type);

// True when the class states its own end: it marks its own end, and each
// linear value that it holds by value states its own end too
// (parts_state_own_end).  A door that ends such a class ends each value that
// it holds with no run-time check.  Complexity: linear in the bases, the
// members and the template arguments that the class reaches by value.
[[nodiscard]] consteval bool states_own_end(std::meta::info type) {
    return marks_own_end(type) && parts_state_own_end(type);
}

// True when each linear value that the class holds by value states its own
// end: a base, a data member, a capture, or the element of an array that it
// holds.  The end of the class ends each such value in its own frame.  A sum
// of a linear payload, such as a data member Option<Token>, states no end of
// its own, because its end with a payload drops a value that a consumer must
// take, and the end of a class that holds one is no consume.  So such a class
// goes to its consumers, and no door ends it.  Complexity: linear in the bases
// and the members that the class reaches by value.
[[nodiscard]] consteval bool parts_state_own_end(std::meta::info type) {
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    for (const ::foundation::reflect::TypeNode& part :
         ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{bare, true})) {
        if (!part.is_held_by_value) continue;
        const std::meta::info held = std::meta::remove_all_extents(part.type);
        if (is_linear_type(held) && !states_own_end(held)) return false;
    }
    return true;
}

// A linear value whose class states its own end, so the drop door can end
// it.  Each other linear value goes to the consumer of each value that it
// holds.
template <class T>
concept DroppableLinear = LinearValue<T> && states_own_end(^^T);

// The drop door that reports nothing.  It moves the value into its own
// frame, so the destructor of the value runs before the door returns.  A
// search for this annotation lists each deliberate discard.  A class whose
// destructor checks that its value was consumed, such as a tracked
// fixy::Linear<T>, ends through its own drop door, because this door
// destroys a live value.
template <DroppableLinear T>
[[= drop_door{}]] constexpr void drop(T&& value) noexcept {
    T dropped(static_cast<T&&>(value));
    static_cast<void>(dropped);
}

// A class that holds a linear value and marks no end of its own would end
// that value in the frame of the door, where no rule reads the end.  The
// deleted twin gives the reason.
template <LinearValue T>
    requires(!marks_own_end(^^T) && !owes_consume_at_end(^^T))
constexpr void drop(T&&) = delete("the drop door ends only a class that states its own end.  This class is linear "
                                  "by the values that it holds: give each value to its consumer, or end each one at "
                                  "its own door");

// A class that owes a consume at its end, such as a live fixy::Linear<T>,
// would end with its obligation open in the frame of the door.  The deleted
// twin gives the reason.
template <LinearValue T>
    requires(owes_consume_at_end(^^T))
constexpr void drop(T&&) = delete("the drop door ends no class that owes a consume at its end, such as a live "
                                  "fixy::Linear: give the value to its consumer, or end it at its own drop door");

// A class that marks its own end and holds a linear value that states no end
// of its own, such as a data member Option<Token>, would end that value in
// the frame of the door with no consume.  The deleted twin gives the reason.
template <LinearValue T>
    requires(marks_own_end(^^T) && !parts_state_own_end(^^T))
constexpr void drop(T&&) = delete("the drop door ends a class only when each linear value that it holds states its "
                                  "own end.  This class holds a value that states no end of its own, such as an "
                                  "Option of a linear payload: take that value out and give it to its consumer, or "
                                  "give the class a consumer that does");

// A named value stays with the caller, so a drop of it would leave a
// second name for the value.  The deleted twin gives the reason.
template <class T>
    requires(is_linear_type(^^T))
constexpr void drop(T&) = delete("the drop door ends a linear value that the caller gives up.  Pass it with std::move");

// The exchange door, the form of mem::replace of Rust.  A function that takes
// a linear value by a reference that is not const borrows it, so a move out
// of the reference would leave the caller a spent value, which looks the same
// as a live one.  The rule must-consume refuses such a move (the kind
// borrow-move-out).  This door gives the old value back and leaves the
// replacement in the slot, so the caller keeps a live value.  The two moves
// do not throw, as the shape of a linear class needs.
template <LinearValue T>
    requires(std::is_nothrow_move_assignable_v<T>)
[[nodiscard]] constexpr T exchange(T& slot, T replacement) noexcept {
    T old(static_cast<T&&>(slot));
    slot = static_cast<T&&>(replacement);
    return old;
}

// A closure that the once door can call: a class, not a reference and not
// const, that runs as an rvalue with the arguments.
template <class Closure, class... Arguments>
concept OnceClosure = std::is_class_v<Closure> && !std::is_reference_v<Closure> && !std::is_const_v<Closure>
                   && std::is_invocable_v<Closure&&, Arguments&&...>;

// The once door.  The closure moves into the frame of the door, and the door
// calls it one time with the forwarded arguments, such as a context and a
// key by reference, so no second call of the same closure can follow.  A
// closure whose call operator moves a linear capture out goes through this
// door: each other call of it can run the body again and give a second value
// of one capture (the rule must-consume).  The result is a value, so it holds
// no reference into the closure, whose end follows the call.
template <class Closure, class... Arguments>
    requires OnceClosure<Closure, Arguments...>
[[= once_door{}]] constexpr auto
    call_once(Closure&& closure,
              Arguments&&... arguments) noexcept(std::is_nothrow_invocable_v<Closure&&, Arguments&&...>) {
    Closure owned(static_cast<Closure&&>(closure));
    return static_cast<Closure&&>(owned)(static_cast<Arguments&&>(arguments)...);
}

// A named closure stays with the caller, so a call of it here would leave a
// second name that can call it again.  The deleted twin gives the reason.
template <class Closure, class... Arguments>
    requires(std::is_class_v<Closure>)
constexpr void call_once(Closure&, Arguments&&...) = delete("the once door takes the closure that the caller gives "
                                                            "up.  Pass it with std::move");

// Why a caller ends a body that no door called (end_unrun_body).
enum class UnrunReason : std::uint8_t {
    // A door could not start the thread of the body, and the caller gives
    // the body up.
    start_refused,
    // A test of a refused start ends the body that came back.
    refusal_test,
    // A frame door could not lend the end that the body takes, such as the
    // producer end of a merge channel while its root is out of the pool, and
    // the caller gives up the body that came back, or a door ends a body that
    // holds no linear value.
    lend_refused,
};

// A body that no door called and that holds a linear value by value: a
// class, such as a closure with a linear capture, that marks no end of its
// own (marks_own_end), and each linear value that it holds states its own end
// (parts_state_own_end).  So the end of the body ends each such value with no
// run-time check.
template <class Body>
concept UnrunBody = LinearValue<Body> && std::is_class_v<Body> && !marks_own_end(^^Body) && !owes_consume_at_end(^^Body)
                 && parts_state_own_end(^^Body);

// The one door that ends a body that no door called and that holds a linear
// value by value, such as a body that a refused start of a thread gives back.
// The body moves into the frame of the door, so each linear value that it
// holds ends there with no consume.  The reason tells why the caller gives
// the body up.  A search for this door lists each such end.
template <UnrunBody Body>
[[= detach_door{}]] constexpr void end_unrun_body(Body&& body, UnrunReason /*reason*/) noexcept {
    Body ended(static_cast<Body&&>(body));
    static_cast<void>(ended);
}

// A named body stays with the caller, so an end of it here would leave a
// second name for its values.  The deleted twin gives the reason.
template <class Body>
    requires(is_linear_type(^^Body))
constexpr void end_unrun_body(Body&, UnrunReason) = delete("the door ends a body that the caller gives up.  Pass it "
                                                           "with std::move");

// A body that holds a linear value that states no end of its own, such as a
// capture Option<Token> or a capture of a live fixy::Linear<T>, would end that
// value in the frame of the door with no consume.  The deleted twin gives the
// reason.
template <class Body>
    requires(LinearValue<Body> && std::is_class_v<Body> && !marks_own_end(^^Body) && !owes_consume_at_end(^^Body)
             && !parts_state_own_end(^^Body))
constexpr void end_unrun_body(Body&&, UnrunReason) = delete(
    "the door ends a body only when each linear value that the body holds states its own end.  This body holds a "
    "value that states no end of its own, such as an Option of a linear payload or a live fixy::Linear: give the "
    "body to call_once, and let it consume that value");

// A value that owes a consume at its end, such as a live fixy::Linear<T>, is
// no body that a door can end.  The deleted twin gives the reason.
template <class Body>
    requires(LinearValue<Body> && owes_consume_at_end(^^Body))
constexpr void end_unrun_body(Body&&, UnrunReason) =
    delete("the door ends no value that owes a consume at its end, such as a live fixy::Linear: give the value to its "
           "consumer");

}  // namespace foundation::linear
