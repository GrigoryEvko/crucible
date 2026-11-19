#pragma once

// The facts of a callable class, and the check of a door against them.
//
// A door that takes a body, such as a fork, a spawn or a frame door, states
// a policy: where the body runs, how many times the door calls it, and if it
// calls one object from many threads.  check_closure compares the facts of
// the body with the policy of the door.  A refusal names the field, its type,
// the first part that refuses and the fix.  A door runs the check in a
// static_assert of its body, and the verdict is the message.
//
// THE FACTS
//     closure_facts reads each data member of the class of the body, through
//     member_components_of of foundation/reflect/TypeComponents.h, the one
//     read of the members of a class.  The members of a lambda are its
//     capture fields, on a compiler that shows them (the sixth patch of GCC).
//     The kind of a field comes from its type:
//       OwnValue     T: the body owns a T, as [x], [y = e] and [*this] give
//       BorrowConst  T const& or T const*: the body reads a T of its caller
//       BorrowMut    T& or T*: the body can write a T of its caller
//     A pointer to a function is OwnValue.  The field of [this] is a pointer,
//     so [this] is a borrow, and the field of [*this] is the class.  A class
//     with state that the walk cannot read, such as a lambda with captures on
//     a compiler that shows no capture field, is unreadable.  A class with
//     more than max_capture_facts fields is unreadable too.
//
// THE POLICY OF A DOOR (DoorPolicy)
//     crossing     SameThread: the body runs on the calling thread.
//                  SoleThread: one body runs on one other thread while the
//                  caller waits for it.
//                  ConcurrentThreads: sibling bodies or copies of one body run
//                  at the same time on other threads.
//                  Escapes: the body runs past the end of the frame of the
//                  call.
//                  BesideCaller: one body runs on another thread while the
//                  caller runs on at the same time, and the door joins it
//                  before it returns.
//     calls        the QTT grade of the calls of one body object:
//                  One (exactly one call), AtMostOne (one call or none),
//                  Omega (many calls) or Zero (no call).
//     calls_one_object_concurrently
//                  the door calls one body object from many threads at the
//                  same time.
//
// THE RULES (check_closure)
//     1. A body that owns a linear value (is_linear_type of
//        foundation/Linear.h) has the grade One, and each other body has the
//        grade Omega.  The door admits the body when the grade of the door is
//        below or equal to the grade of the body (QttSemiring::leq).  So a
//        linear body goes only to a door that calls it exactly one time.
//     2. A door on the calling thread asks nothing more, unless it calls one
//        object from many threads: then each field is Shareable.
//     3. A door on another thread refuses a body that it cannot read, and a
//        field that holds an execution context.  The door gives each body the
//        context of its own thread.
//     4. A door that escapes refuses each borrow: a borrow field, and an owned
//        value that holds a borrow (holds_borrow_ of foundation/core/Choice.h).
//     5. An OwnValue field is Sendable.  It is Shareable too when the door
//        calls one object from many threads.
//     6. A BorrowConst field is Shareable.
//     7. A BorrowMut field is Sendable at a SoleThread door, because no other
//        thread uses the object while the caller waits.  At each other door
//        it is Synchronized, because a second body can write the same object.
//     8. At a BesideCaller door, each borrow field is Synchronized, also a
//        BorrowConst field.  The caller runs at the same time as the body,
//        and it can write each object of its frame, also an object that the
//        body reads through a reference to const.  The check cannot see that
//        alias, so a read alone is not enough.  For the same reason, an
//        OwnValue field is SendableBeside there, not only Sendable: a value
//        that refers to an object of the frame, such as a View, refers only
//        to a Synchronized object.
//     The properties are those of foundation/Transfer.h.
//
// THE COST
//     The facts read the members of the class one time.  The check asks each
//     field only the one property that its kind and the crossing need, and it
//     reads the linearity of the body only for a door that does not call it
//     exactly one time.  A refusal builds its message one time, in the
//     verdict, and the static_assert of the door reads the verdict.  A pass
//     builds no message.

#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/StableIdentity.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>

namespace foundation::closure {

using ::foundation::algebra::lattices::QttGrade;

// The kind of a capture field (THE FACTS).
enum class CaptureKind : unsigned char {
    OwnValue = 0,
    BorrowConst = 1,
    BorrowMut = 2,
};

// Where a door runs its body (THE POLICY OF A DOOR).
enum class Crossing : unsigned char {
    SameThread = 0,
    SoleThread = 1,
    ConcurrentThreads = 2,
    Escapes = 3,
    BesideCaller = 4,
};

// The policy of a door (THE POLICY OF A DOOR).
struct DoorPolicy {
    Crossing crossing = Crossing::SameThread;
    QttGrade calls = QttGrade::One;
    bool calls_one_object_concurrently = false;
    // False for a door that does not ask the transfer facts of a field, as
    // the raw probe of fixy/os/ThreadTasks.h does.  Rules 1, 3 and 4 still
    // hold at such a door.
    bool asks_transfer_facts = true;
};

// The reason of a refusal (THE RULES).
enum class Refusal : unsigned char {
    None = 0,
    Unreadable = 1,
    LinearGrade = 2,
    CapturedContext = 3,
    Borrow = 4,
    NotSendable = 5,
    NotShareable = 6,
    NotSynchronized = 7,
    NotSendableBeside = 8,
};

// The most fields that closure_facts reads.  A class with more is unreadable.
inline constexpr std::size_t max_capture_facts = 32;

// The facts of one field.  The target is the type of the field with the
// reference or the pointer removed, with no cv qualifier.
struct CaptureFact {
    std::meta::info field{};
    std::meta::info target{};
    CaptureKind kind = CaptureKind::OwnValue;
    bool is_context = false;
};

// The facts of one callable class (THE FACTS).
class ClosureFacts {
public:
    [[nodiscard]] consteval std::meta::info type() const noexcept { return type_; }
    [[nodiscard]] consteval bool is_readable() const noexcept { return is_readable_; }
    [[nodiscard]] consteval std::size_t size() const noexcept { return size_; }
    [[nodiscard]] consteval const CaptureFact* begin() const noexcept { return fields_; }
    [[nodiscard]] consteval const CaptureFact* end() const noexcept { return fields_ + size_; }

private:
    friend consteval auto closure_facts(std::meta::info callable) -> ClosureFacts;

    std::meta::info type_{};
    bool is_readable_ = false;
    CaptureFact fields_[max_capture_facts]{};
    std::size_t size_ = 0;
};

namespace detail {

// The fact of one member node of the class.  A member that is a reference
// gives its referent, and a member that is a pointer to an object gives its
// pointee.
[[nodiscard]] consteval CaptureFact fact_of_member(::foundation::reflect::TypeNode node) {
    namespace refl = ::foundation::reflect;
    CaptureFact fact{};
    fact.field = node.member;
    fact.target = node.type;
    if (node.was_reference) {
        fact.kind = node.was_const ? CaptureKind::BorrowConst : CaptureKind::BorrowMut;
    } else if (std::meta::is_pointer_type(node.type)
               && !std::meta::is_function_type(std::meta::remove_pointer(node.type))) {
        const std::meta::info pointee = std::meta::remove_pointer(node.type);
        fact.target = refl::bare_type(pointee);
        fact.kind = std::meta::is_const_type(pointee) ? CaptureKind::BorrowConst : CaptureKind::BorrowMut;
    }
    fact.is_context = ::foundation::effects::is_exec_ctx(fact.target);
    return fact;
}

}  // namespace detail

// The facts of a callable class (THE FACTS).  It is not a template, so no
// unit can specialize an answer.  Complexity: linear in the members.
[[nodiscard]] consteval auto closure_facts(std::meta::info callable) -> ClosureFacts {
    namespace refl = ::foundation::reflect;
    ClosureFacts facts;
    const std::meta::info type = refl::bare_type(callable);
    facts.type_ = type;
    if (!std::meta::is_class_type(type)) {
        facts.is_readable_ =
            std::meta::is_function_type(type)
            || (std::meta::is_pointer_type(type) && std::meta::is_function_type(std::meta::remove_pointer(type)));
        return facts;
    }
    if (!std::meta::is_complete_type(type)) return facts;
    const refl::TypeNode root{type, true};
    if (refl::holds_unreadable_state(root)) return facts;
    for (const refl::TypeNode& node : refl::member_components_of(root)) {
        // A base of a callable class holds state of its own, and a lambda has
        // no base, so a class with a base is read through its members only
        // when the base is empty.
        if (node.member == std::meta::info{}) {
            if (!std::meta::is_empty_type(node.type)) return facts;
            continue;
        }
        if (facts.size_ == max_capture_facts) return facts;
        facts.fields_[facts.size_] = detail::fact_of_member(node);
        ++facts.size_;
    }
    facts.is_readable_ = true;
    return facts;
}

class ClosureVerdict;

[[nodiscard]] consteval auto check_closure(std::meta::info callable, DoorPolicy policy,
                                           ::foundation::core::TextView door) -> ClosureVerdict;

// The verdict of a door on a body: the reason of a refusal, the index of the
// field, and the message of the refusal.  data() and size() give the text,
// which static_assert prints as its message.  An admitted body has an empty
// text.
class ClosureVerdict {
public:
    [[nodiscard]] constexpr bool is_admitted() const noexcept { return reason_ == Refusal::None; }
    [[nodiscard]] constexpr Refusal reason() const noexcept { return reason_; }
    [[nodiscard]] constexpr std::size_t field_index() const noexcept { return field_; }
    [[nodiscard]] constexpr char const* data() const noexcept { return text_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

private:
    friend consteval auto check_closure(std::meta::info callable, DoorPolicy policy, ::foundation::core::TextView door)
        -> ClosureVerdict;

    // The last byte of the buffer stays zero, so a zero follows the text.
    static constexpr std::size_t capacity_ = 1024;

    consteval void append_(::foundation::core::TextView text) noexcept {
        for (char const letter : text)
            append_letter_(letter);
    }

    consteval void append_letter_(char letter) noexcept {
        if (size_ + 1 < capacity_) {
            text_[size_] = letter;
            ++size_;
        }
    }

    consteval void append_chars_(::foundation::reflect::detail::NameChars chars) noexcept {
        for (std::size_t index = 0; index < chars.size; ++index)
            append_letter_(chars.first[index]);
    }

    // The identifier of the field, or <unnamed>.
    consteval void append_identifier_(std::meta::info field) noexcept {
        if (!std::meta::has_identifier(field)) {
            append_("<unnamed>");
            return;
        }
        append_chars_(
            ::foundation::reflect::detail::static_name_chars_<^^ClosureVerdict>(std::meta::identifier_of(field)));
    }

    // The name of a field as the source spells it.  GCC names the field of
    // a capture "__" and the name of the entity: "__count" is `count`.
    // "__this" is `this` for a pointer and `*this` for the class.  A capture
    // with no name of its own, [_ = e], gives "___", and an element of a pack
    // capture gives "__cells#0", the element 0 of the pack `cells`.
    consteval void append_field_name_(CaptureFact const& fact) noexcept {
        if (!std::meta::has_identifier(fact.field)) {
            append_("<unnamed>");
            return;
        }
        const ::foundation::reflect::detail::NameChars name =
            ::foundation::reflect::detail::static_name_chars_<^^ClosureVerdict>(std::meta::identifier_of(fact.field));
        const bool is_capture = name.size > 2 && name.first[0] == '_' && name.first[1] == '_';
        const std::size_t start = is_capture ? 2 : 0;
        const bool is_this = is_capture && name.size == 6 && name.first[2] == 't' && name.first[3] == 'h'
                          && name.first[4] == 'i' && name.first[5] == 's';
        if (is_this) {
            if (std::meta::is_pointer_type(std::meta::type_of(fact.field))) {
                append_("this");
            } else {
                append_("*this");
            }
            return;
        }
        if (is_capture && name.first[2] == '_') {
            append_("_");
            return;
        }
        std::size_t mark = start;
        while (mark < name.size && name.first[mark] != '#')
            ++mark;
        for (std::size_t index = start; index < mark; ++index)
            append_letter_(name.first[index]);
        if (mark == name.size) return;
        append_(" (element ");
        for (std::size_t index = mark + 1; index < name.size; ++index)
            append_letter_(name.first[index]);
        append_(" of the pack)");
    }

    consteval void append_type_(std::meta::info type) noexcept {
        append_chars_(
            ::foundation::reflect::detail::static_name_chars_<^^ClosureVerdict>(std::meta::display_string_of(type)));
    }

    // The head of the refusal of one field: the door, the field and its type.
    consteval void refuse_field_(Refusal reason, std::size_t index, ::foundation::core::TextView door,
                                 CaptureFact const& fact) noexcept {
        reason_ = reason;
        field_ = index;
        append_(door);
        append_(": the body captures `");
        append_field_name_(fact);
        append_("` (field ");
        append_identifier_(fact.field);
        append_(", type ");
        append_type_(std::meta::type_of(fact.field));
        append_(").  ");
    }

    // The cause and the fix of the refusal of one field (THE RULES).  part is
    // the first part of the target that refuses, or a null reflection.
    consteval void explain_(CaptureFact const& fact, std::meta::info part, Crossing crossing) noexcept {
        switch (reason_) {
            case Refusal::CapturedContext:
                append_("A context names the thread of its owner.  Take the context that the door gives to the body "
                        "as its parameter, and capture no context");
                return;
            case Refusal::Borrow:
                append_("The door runs the body past the end of the frame of the call, so a borrow dangles.  Move "
                        "the value into the body, or use a door that joins in the frame");
                return;
            case Refusal::NotSendable:
                if (fact.kind == CaptureKind::OwnValue) {
                    append_("The value moves to another thread, and its type is not Sendable.  Capture a value that "
                            "holds no part of one thread");
                } else {
                    append_("The body uses it on another thread through a borrow that is not const, and its type is "
                            "not Sendable.  Capture the parts that the body uses, not the whole object");
                }
                break;
            case Refusal::NotShareable:
                append_("Two threads read it at the same time, and its type is not Shareable.  Capture a copy, or a "
                        "type with no unsynchronized part");
                break;
            case Refusal::NotSendableBeside:
                append_("The value moves to a thread that runs beside the caller, and its type is not "
                        "SendableBeside.  The caller can write each object that the value refers to while the body "
                        "reads it.  Capture a value that owns its parts, or one that refers only to an Atomic or a "
                        "channel");
                break;
            case Refusal::NotSynchronized:
                if (crossing == Crossing::BesideCaller) {
                    append_("The caller runs at the same time as the body, and it can write the object that the "
                            "body uses through this borrow, also a borrow to const.  Capture it by value, or use an "
                            "Atomic or a channel");
                } else if (std::meta::is_pointer_type(std::meta::remove_cvref(std::meta::type_of(fact.field)))) {
                    append_("The bodies or copies of this door run at the same time on other threads, so a write "
                            "through this pointer to an object that is not const races.  Capture a pointer to const "
                            "if the body only reads through it or uses it as an address");
                } else {
                    append_("The bodies or copies of this door run at the same time on other threads, so a write "
                            "through this reference that is not const races.  Capture it by value, capture a const "
                            "reference if the body only reads it, or use an Atomic or a channel");
                }
                break;
            default:
                return;
        }
        if (part != std::meta::info{}) {
            append_(".  The first part that refuses it: ");
            append_type_(part);
        }
    }

    char text_[capacity_]{};
    std::size_t size_ = 0;
    Refusal reason_ = Refusal::None;
    std::size_t field_ = 0;
};

namespace detail {

// The first part of the target that refuses the property of a refusal of a
// transfer property, or a null reflection when the target holds it.
[[nodiscard]] consteval std::meta::info refusing_part(std::meta::info target, Refusal refusal) {
    namespace transfer = ::foundation::transfer;
    switch (refusal) {
        case Refusal::NotSendable:
            return transfer::first_refusal(target, transfer::Property::Send);
        case Refusal::NotShareable:
            return transfer::first_refusal(target, transfer::Property::Share);
        case Refusal::NotSynchronized:
            return transfer::first_synchronized_refusal(target);
        case Refusal::NotSendableBeside:
            return transfer::first_refusal(target, transfer::Property::SendBeside);
        default:
            return std::meta::info{};
    }
}

// The property that the rules ask of one field at a door on another thread,
// as the refusal that it gives (rules 5 to 8).
[[nodiscard]] consteval Refusal asked_property(CaptureFact const& fact, DoorPolicy policy) {
    const bool is_beside = policy.crossing == Crossing::BesideCaller;
    if (fact.kind == CaptureKind::OwnValue) return is_beside ? Refusal::NotSendableBeside : Refusal::NotSendable;
    // Rule 8.
    if (is_beside) return Refusal::NotSynchronized;
    if (fact.kind == CaptureKind::BorrowConst) return Refusal::NotShareable;
    const bool is_unique_borrow = policy.crossing == Crossing::SoleThread && !policy.calls_one_object_concurrently;
    return is_unique_borrow ? Refusal::NotSendable : Refusal::NotSynchronized;
}

// The refusal of one field and the part that refuses (THE RULES).
struct FieldRefusal {
    Refusal reason = Refusal::None;
    std::meta::info part{};
};

[[nodiscard]] consteval FieldRefusal refusal_of_field(CaptureFact const& fact, DoorPolicy policy) {
    // Rule 2.
    if (policy.crossing == Crossing::SameThread) {
        if (!policy.calls_one_object_concurrently) return FieldRefusal{};
        const std::meta::info part = refusing_part(fact.target, Refusal::NotShareable);
        return part == std::meta::info{} ? FieldRefusal{} : FieldRefusal{Refusal::NotShareable, part};
    }
    // Rules 3 and 4.
    if (fact.is_context) return FieldRefusal{Refusal::CapturedContext};
    if (policy.crossing == Crossing::Escapes
        && (fact.kind != CaptureKind::OwnValue || ::foundation::core::detail::holds_borrow_(fact.target))) {
        return FieldRefusal{Refusal::Borrow};
    }
    if (!policy.asks_transfer_facts) return FieldRefusal{};
    // Rules 5 to 8.
    const Refusal asked = asked_property(fact, policy);
    const std::meta::info part = refusing_part(fact.target, asked);
    if (part != std::meta::info{}) return FieldRefusal{asked, part};
    if (fact.kind == CaptureKind::OwnValue && policy.calls_one_object_concurrently) {
        const std::meta::info shared = refusing_part(fact.target, Refusal::NotShareable);
        if (shared != std::meta::info{}) return FieldRefusal{Refusal::NotShareable, shared};
    }
    return FieldRefusal{};
}

}  // namespace detail

// The verdict of a door with a policy on a body (THE RULES).  door is the name
// of the door, at the start of the message.  It is not a template, so no unit
// can specialize an answer.  Complexity: linear in the fields, times the walk
// of one property of each target.
[[nodiscard]] consteval auto check_closure(std::meta::info callable, DoorPolicy policy,
                                           ::foundation::core::TextView door) -> ClosureVerdict {
    namespace lattices = ::foundation::algebra::lattices;
    ClosureVerdict verdict;
    const ClosureFacts facts = closure_facts(callable);
    // Rule 3, a body that the door cannot read.
    if (!facts.is_readable() && policy.crossing != Crossing::SameThread) {
        verdict.reason_ = Refusal::Unreadable;
        verdict.append_(door);
        verdict.append_(": the body has state that reflection does not show.  A lambda with captures needs the sixth "
                        "patch of GCC, which shows each capture as a field.  Until then, the door refuses it");
        return verdict;
    }
    // Rule 1.  A linear body has the grade One, and only the grade One of a
    // door is below or equal to it.
    if (!lattices::QttSemiring::leq(policy.calls, QttGrade::One)
        && ::foundation::linear::is_linear_type(facts.type())) {
        verdict.reason_ = Refusal::LinearGrade;
        verdict.append_(door);
        verdict.append_(
            ": the body owns a linear value, so a door calls it exactly one time and the call consumes it.  ");
        if (policy.calls == QttGrade::AtMostOne) {
            verdict.append_("This door can skip the call, and the linear value then ends with no consume.  ");
        } else {
            verdict.append_("This door does not call the body exactly one time.  ");
        }
        verdict.append_("Give the linear value to a door that calls its body exactly one time, or capture a borrow of "
                        "it");
        return verdict;
    }
    std::size_t index = 0;
    for (const CaptureFact& fact : facts) {
        const detail::FieldRefusal refusal = detail::refusal_of_field(fact, policy);
        if (refusal.reason != Refusal::None) {
            verdict.refuse_field_(refusal.reason, index, door, fact);
            verdict.explain_(fact, refusal.part, policy.crossing);
            return verdict;
        }
        ++index;
    }
    return verdict;
}

}  // namespace foundation::closure
