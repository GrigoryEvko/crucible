#pragma once

// The transfer of a value between threads, derived from the structure of its
// type, as Rust derives its auto traits Send and Sync.
//
// THE THREE PROPERTIES
//     Sendable<T>      a value of T can move to another thread, and that
//                      thread can use it there.
//     Shareable<T>     two threads can read one object of T through a
//                      reference to const at the same time.
//     Synchronized<T>  two threads can write one object of T through a
//                      reference that is not const at the same time (THE
//                      SHARED WRITE).  C++ needs this third property where
//                      Rust needs two, because a T& of C++ is both the &mut T
//                      and the &T with interior mutability of Rust.
//     SendableBeside<T>
//                      a value of T can move to a thread that runs while its
//                      sender runs on, as the body of run_beside of
//                      fixy/os/ThreadTasks.h does.  The sender can still
//                      write each object that the value refers to, so each
//                      such object is Synchronized (THE VALUE BESIDE ITS
//                      SENDER).
//     SendableOwned<T> a value of T holds no borrow, so it can move to a
//                      thread that uses it after its sender ends, as a value
//                      of a channel does (THE VALUE THAT OUTLIVES ITS
//                      SENDER).
//
// THE RULES
//     The walk asks one property of one type, and the first rule that applies
//     gives the answer.  A type that the walk meets again during the walk
//     counts as holding the property, so a type that reaches itself through a
//     Box ends the walk, as the auto traits of Rust do.
//
//     1. A reference to const T holds each property when T is Shareable.  A
//        reference to T that is not const holds each property when two
//        threads can write an object of T at the same time (THE SHARED
//        WRITE): the thread that gives the reference can still write the
//        object, and no type tells that only one of the two threads writes.
//        A class that states refers{}, such as a Ref, is a reference to its
//        first type argument, and this rule applies to it.  A class that
//        states refers_unique{}, such as a ViewMut, is the one writer of the
//        object of its first type argument.  It is Sendable when that type is
//        Sendable.  It is Shareable only when two threads can write that type,
//        because a reference to const of it can still write the object.  A
//        class that states refers_deep{}, such as a Link, refers to the object
//        of its first type argument as a reference does, but a const object of
//        it gives only const access to the object, and a const object of it
//        does not copy.  It is Shareable when that type is Shareable.  A copy
//        of an object that is not const refers to the same object, so for
//        each other property the class is a reference to the object.  A class
//        that states refers_deep{} and copies from a const object holds no
//        property, because the copy would write the object.
//     2. A scalar, an enumeration, nullptr_t, a pointer to a function and a
//        pointer to a member hold each property.  A pointer to a function
//        names code, which no thread writes.
//     3. A pointer to an object holds no property.  The walk cannot see who
//        else holds the object.
//     4. An array holds a property when its element holds it.
//     5. A class or a union that states a property holds it or not as it
//        states (THE ANNOTATIONS).  A class that states thread_bound{} is not
//        Sendable.
//     6. A class that states by_arguments{} holds a property when its first
//        type argument holds it, and the walk reads no member of it.  The
//        class owns an object of that argument, so an argument that is only
//        declared holds only what its statement gives (THE OPAQUE TYPES), as
//        the payload of a cell does.  The class holds no value of its other
//        type arguments, such as the authority of a BoxWord, so the walk asks
//        nothing of them.
//     7. A class that holds state that the walk cannot read holds no property.
//        A lambda with captures has this shape on a compiler that shows no
//        field of a capture.  On a compiler that shows the fields, the
//        captures of a lambda are its members, and rule 8 reads them.
//     8. Each other class or union holds a property when each base and each
//        non-static data member holds it, and, for a specialization, when each
//        type argument holds it.  The arguments count because a class can
//        keep a value of an argument in bytes, as an Option does.  Such a
//        class cannot keep a value of a class that is only declared, so an
//        argument that is only declared holds each property: a tag type is
//        the usual case.  A member declared mutable makes the class not
//        Shareable, unless two threads can write the member at the same time,
//        because a read through a reference to const can write it.
//
// THE SHARED WRITE
//     T is Synchronized when T is a class that states shareable{}, such as
//     the Atomic of the base, an array of such a type, or a class whose each
//     base and each member is Synchronized or is const and Shareable.  A class
//     that states thread_bound{} is not Synchronized.  No
//     write reaches a const member, so a read is all it must allow.  A member
//     that is a reference makes the class not Synchronized.  A scalar is not
//     Synchronized: two plain writes of one scalar race.  An atomic of the
//     standard library is not either: the base quarantines it, and its Atomic
//     takes its place.
//
// THE VALUE BESIDE ITS SENDER
//     SendableBeside is Sendable with a stricter rule 1.  A reference, a class
//     that states refers{} and a class that states refers_unique{} hold it
//     only when two threads can write their target (THE SHARED WRITE), also
//     for a target that is const.  Each other rule is the rule of Sendable,
//     and sendable{}, not_sendable{} and thread_bound{} state it as they state
//     Sendable.  So a View of const int is Sendable, but it is not
//     SendableBeside: the sender can write the ints while the other thread
//     reads them, and no type tells that it does not.
//
// THE VALUE THAT OUTLIVES ITS SENDER
//     SendableOwned is Sendable with a rule 1 that refuses each borrow.  A
//     reference, and a class that states refers{}, refers_unique{} or
//     refers_deep{}, name an object that can end before the value does, so
//     they do not hold it.  Each other rule is the rule of Sendable, and
//     sendable{}, not_sendable{} and thread_bound{} state it as they state
//     Sendable.  So an owner, such as a Box, holds it when the object that it
//     owns holds it.  A channel asks it of its values: a producer can push a
//     value and end before a consumer on another thread pops the value.  Rust
//     asks Send and 'static of such a value for the same reason.
//
// THE ANNOTATIONS
//     A class states a property that its fields hide with one annotation, the
//     named door that a search finds:
//
//         class [[= ::foundation::transfer::by_arguments{}]] Box { ... };
//
//     sendable{} and not_sendable{} state Sendable.  shareable{} and
//     not_shareable{} state Shareable, and shareable{} also states that two
//     threads can write an object of the class at the same time.
//     by_arguments{} states that the class owns the object of its first type
//     argument behind its pointers, as a Box does, and that its other type
//     arguments name no object that it holds.  refers{} states that the class
//     refers to the object of its first type argument, as a reference does.
//     refers_unique{} states that the class refers to that object as its one
//     writer, as a ViewMut does.  refers_deep{} states that the class refers
//     to that object, and that a const object of the class gives only const
//     access to it and does not copy, as a Link does.
//     thread_bound{} states that the class belongs to one owner thread: a
//     value does not move to another thread, and no other thread writes it.
//     The class then is not Sendable and two threads cannot write it, but it
//     is Shareable when its parts are, because a read through a reference to
//     const can be safe on each thread, as the read of a timing sink is.
//     payload_cell{} states that the class is a cell that hands values of its
//     first type argument from one thread to another, as the Atomic of the
//     base does.  Each step of the cell is atomic, but a value that one thread
//     stores, another thread loads.  So the class holds each property, the
//     shared write included, exactly when that argument is Sendable
//     (SendableBeside for SendableBeside).  A cell of a reference to an
//     object of one owner thread thus moves no such object.  Each statement is
//     a claim that the code of the class makes true, and the comment beside
//     it gives the reason.
//
// THE OPAQUE TYPES
//     A class that is only declared, such as a handle whose definition is out
//     of view, holds no property, because the walk cannot read it.  The same
//     holds for such a class as the argument of an owner, such as the Box of
//     a pimpl class.  The header that declares such a class states its facts
//     beside the declaration, in a registry, as fixy/session/Registry.h does
//     for the combinators of a protocol.  An annotation cannot do it, because
//     a declaration that is not a definition keeps no annotation that the
//     walk can read.
//
//         namespace my_layer {
//         struct Handle;
//         namespace transfer_facts {
//         // No thread writes a handle after its build.
//         inline constexpr ::foundation::transfer::opaque_facts handle{
//             .type = ^^Handle, .is_sendable = true, .is_shareable = true};
//         inline constexpr ::foundation::transfer::opaque_seal seal{.count = 1};
//         }
//         }
//
//     The registry is the namespace transfer_facts in the namespace of the
//     class.  Each variable of type opaque_facts in it is one statement, and
//     its one variable of type opaque_seal counts the statements.  The comment
//     beside a statement gives its reason, as for an annotation.  A registry
//     with no seal, with two seals, or with a count that differs from its seal
//     stops the build at its first read, and so does a class with two
//     statements.  So each translation unit reads the same statements.
//
//     The walk reads a statement only while the class is only declared.  The
//     file that defines the class must check that its definition holds each
//     property that the statement gives, beside the definition:
//
//         static_assert(::foundation::transfer::holds_its_statement(^^Handle));
//
// What the walk cannot see, stated rather than implied: a value behind type
// erasure, and a capture of a lambda on a compiler that shows no field of a
// capture (rule 7 refuses such a lambda).  The walk reads the bases, the
// members and the arguments of a class, and each capture field that the
// compiler shows, through
// member_components_of and argument_components_of of
// foundation/reflect/TypeComponents.h, and the annotations through
// carries_annotation of foundation/Linear.h.  Complexity: linear in the number
// of distinct types that the walk reaches, times the cost of the scan of the
// visited list.

#include <foundation/Linear.h>
#include <foundation/reflect/Members.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>

namespace foundation::transfer {

// The annotations (THE ANNOTATIONS).
struct sendable final {};
struct not_sendable final {};
struct shareable final {};
struct not_shareable final {};
struct by_arguments final {};
struct refers final {};
struct refers_unique final {};
struct refers_deep final {};
struct thread_bound final {};
struct payload_cell final {};

// A statement of the facts of a class that is only declared, and the seal of
// a registry of such statements (THE OPAQUE TYPES).  is_synchronized states
// that two threads can write an object of the class at the same time.  Both
// are structural types, so the walk reads a variable of each as a constant.
struct opaque_facts {
    std::meta::info type{};
    bool is_sendable = false;
    bool is_shareable = false;
    bool is_synchronized = false;
};

struct opaque_seal {
    std::size_t count = 0;
};

// The properties that first_refusal walks (THE THREE PROPERTIES, THE VALUE
// BESIDE ITS SENDER and THE VALUE THAT OUTLIVES ITS SENDER).  The shared
// write has a walk of its own, is_synchronized.
enum class Property : unsigned char {
    Send = 0,
    Share = 1,
    SendBeside = 2,
    SendOwned = 3,
};

// True when the type holds the property (THE RULES).  The definition follows
// first_refusal.
[[nodiscard]] consteval bool holds(std::meta::info type, Property property);

namespace detail {

// How the walk reaches the type of a goal.  A part is a base, a member, an
// element, a target or the root.  A type argument that a class owns behind its
// pointers (rule 6, and the payload of a cell) is a part too, because the
// class holds an object of it.  An incomplete part holds only what its
// statement gives (THE OPAQUE TYPES).  A tag is a type argument that rule 8
// reads.  An incomplete tag holds each property, because no value of it can
// be inside the class: a tag type is the usual case.
enum class Reach : unsigned char {
    Part = 0,
    Tag = 1,
};

// One question of the walk: one property of one type.
struct Goal {
    std::meta::info type{};
    Property property = Property::Send;
    Reach reach = Reach::Part;
};

// The goals of one walk, in a growable array.  The list is a class and not
// std::vector, as TypeNodeList of foundation/reflect/TypeComponents.h is: a
// Goal holds a reflection, so a list exists only in a constant evaluation,
// where it allocates its array and frees it when it ends.  Complexity:
// amortized constant for each push, linear for holds.
class GoalList {
public:
    consteval GoalList() noexcept = default;
    GoalList(GoalList const&) = delete("a list owns its array, so a copy would free the array two times");
    GoalList& operator=(GoalList const&) = delete("a list owns its array, so a copy would free the array two times");
    // A destructor cannot be consteval, and a list exists only in a constant
    // evaluation, so the branch is the only path.
    constexpr ~GoalList() {
        if consteval {
            delete[] goals_;
        }
    }

    consteval void push(Goal goal) {
        if (size_ == capacity_) grow_();
        goals_[size_] = goal;
        ++size_;
    }

    // The last goal, which leaves the list.  The list is not empty.
    [[nodiscard]] consteval Goal pop() {
        --size_;
        return goals_[size_];
    }

    [[nodiscard]] consteval bool is_empty() const noexcept { return size_ == 0; }

    // True when the list holds a goal of the same type and property.
    [[nodiscard]] consteval bool holds(Goal goal) const noexcept {
        for (std::size_t index = 0; index < size_; ++index) {
            if (goals_[index].type == goal.type && goals_[index].property == goal.property) return true;
        }
        return false;
    }

private:
    // Doubles the capacity, and moves each goal into the new array.
    consteval void grow_() {
        const std::size_t capacity = capacity_ == 0 ? 8 : 2 * capacity_;
        Goal* const goals = new Goal[capacity];
        for (std::size_t index = 0; index < size_; ++index)
            goals[index] = goals_[index];
        delete[] goals_;
        goals_ = goals;
        capacity_ = capacity;
    }

    Goal* goals_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

[[nodiscard]] consteval bool states(std::meta::info type, std::meta::info annotation) {
    return ::foundation::linear::carries_annotation(type, annotation);
}

// Each function below has no constant definition.  A call to one stops the
// constant evaluation, and the diagnostic names it (THE OPAQUE TYPES).
void a_registry_of_transfer_facts_has_no_seal() noexcept;
void a_registry_of_transfer_facts_has_two_seals() noexcept;
void a_statement_of_transfer_facts_stands_outside_its_seal() noexcept;
void a_class_has_two_statements_of_transfer_facts() noexcept;

[[nodiscard]] consteval bool is_variable_of(std::meta::info member, std::meta::info type) {
    return std::meta::is_variable(member) && std::meta::remove_cvref(std::meta::type_of(member)) == type;
}

// The registry of the class, the namespace transfer_facts in the namespace
// of the class, or a null reflection when there is none.  Complexity: linear
// in the members of the namespace of the class.
[[nodiscard]] consteval std::meta::info registry_of(std::meta::info type) {
    const std::meta::info scope = std::meta::parent_of(type);
    if (!std::meta::is_namespace(scope)) return std::meta::info{};
    const std::meta::info registry =
        ::foundation::reflect::member_named_in(scope, std::meta::access_context::unprivileged(), "transfer_facts");
    if (registry == std::meta::info{} || !std::meta::is_namespace(registry) || std::meta::is_namespace_alias(registry))
        return std::meta::info{};
    return registry;
}

// The statement of the class, read from its registry, or a statement with no
// type when the registry states nothing of it (THE OPAQUE TYPES).  A fault of
// the registry stops the build.  Complexity: linear in the members of the
// namespace of the class and of the registry.
[[nodiscard]] consteval opaque_facts statement_of(std::meta::info type) {
    const std::meta::info registry = registry_of(type);
    if (registry == std::meta::info{}) return opaque_facts{};
    const std::meta::info bare = std::meta::dealias(type);
    opaque_facts found{};
    std::size_t statements = 0;
    std::size_t seals = 0;
    std::size_t sealed = 0;
    static_cast<void>(::foundation::reflect::find_member(
        registry, std::meta::access_context::unprivileged(), [&](std::meta::info member) {
            if (is_variable_of(member, ^^opaque_seal)) {
                ++seals;
                sealed = std::meta::extract<opaque_seal>(member).count;
                return false;
            }
            if (!is_variable_of(member, ^^opaque_facts)) return false;
            ++statements;
            const opaque_facts statement = std::meta::extract<opaque_facts>(member);
            if (std::meta::dealias(statement.type) != bare) return false;
            if (found.type != std::meta::info{}) a_class_has_two_statements_of_transfer_facts();
            found = statement;
            return false;
        }));
    if (seals == 0) a_registry_of_transfer_facts_has_no_seal();
    if (seals > 1) a_registry_of_transfer_facts_has_two_seals();
    if (statements != sealed) a_statement_of_transfer_facts_stands_outside_its_seal();
    return found;
}

// True when the statement of a class that is only declared gives the
// property (THE OPAQUE TYPES).  Without a statement, the class holds none.
[[nodiscard]] consteval bool is_stated(std::meta::info type, Property property) {
    const opaque_facts statement = statement_of(type);
    if (statement.type == std::meta::info{}) return false;
    return property == Property::Share ? statement.is_shareable : statement.is_sendable;
}

// The annotation that states the property, and the one that refuses it.
// sendable{} and not_sendable{} state SendableBeside and SendableOwned too
// (THE VALUE BESIDE ITS SENDER, THE VALUE THAT OUTLIVES ITS SENDER).
[[nodiscard]] consteval std::meta::info stating_annotation(Property property) {
    return property == Property::Share ? ^^shareable : ^^sendable;
}

[[nodiscard]] consteval std::meta::info refusing_annotation(Property property) {
    return property == Property::Share ? ^^not_shareable : ^^not_sendable;
}

[[nodiscard]] consteval bool is_class_or_union(std::meta::info type) {
    return std::meta::is_class_type(type) || std::meta::is_union_type(type);
}

// The first type argument of a cell of a payload (THE ANNOTATIONS), or a null
// reflection when the class names no type argument.
[[nodiscard]] consteval std::meta::info payload_of_cell(std::meta::info cell) {
    namespace refl = ::foundation::reflect;
    if (!std::meta::has_template_arguments(cell)) return std::meta::info{};
    const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{cell, true});
    if (arguments.empty() || arguments.begin()->is_value_argument) return std::meta::info{};
    return arguments.begin()->type;
}

// The first type that two threads cannot write at the same time, or a null
// reflection when two threads can write an object of the root (THE SHARED
// WRITE).  A class answers for a member that refuses, so the answer names
// the class that holds a reference, or the class of a const member that is
// not Shareable.  Complexity: linear in the number of distinct parts.
[[nodiscard]] consteval std::meta::info first_shared_write_refusal(std::meta::info root) {
    namespace refl = ::foundation::reflect;
    GoalList pending;
    pending.push(Goal{refl::bare_type(root), Property::Share});
    GoalList visited;
    while (!pending.is_empty()) {
        const Goal goal = pending.pop();
        if (visited.holds(goal)) continue;
        visited.push(goal);
        const std::meta::info type = goal.type;
        if (std::meta::is_array_type(type)) {
            pending.push(Goal{refl::bare_type(std::meta::remove_all_extents(type)), Property::Share});
            continue;
        }
        if (!is_class_or_union(type)) return type;
        // A class that is only declared holds what its statement gives (THE
        // OPAQUE TYPES).
        if (!std::meta::is_complete_type(type)) {
            if (statement_of(type).is_synchronized) continue;
            return type;
        }
        if (states(type, ^^not_shareable) || states(type, ^^thread_bound)) return type;
        // A cell of a payload (THE ANNOTATIONS).
        if (states(type, ^^payload_cell)) {
            const std::meta::info payload = payload_of_cell(type);
            if (payload == std::meta::info{} || !::foundation::transfer::holds(payload, Property::Send)) return type;
            continue;
        }
        if (states(type, ^^shareable)) continue;
        bool has_part = false;
        for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{type, true})) {
            has_part = true;
            // A member that is a reference is not held by value.
            if (!part.is_held_by_value) return type;
            // No write reaches a const member, so a read is all it must allow.
            if (part.was_const) {
                if (!::foundation::transfer::holds(part.type, Property::Share)) return type;
                continue;
            }
            pending.push(Goal{part.type, Property::Share});
        }
        // An empty class holds no state that a write can change.  A class that
        // is not empty and shows no part holds state that the walk cannot read.
        if (!has_part && !std::meta::is_empty_type(type)) return type;
    }
    return std::meta::info{};
}

// True when two threads can write an object of the type at the same time
// (THE SHARED WRITE).
[[nodiscard]] consteval bool is_shared_writable(std::meta::info root) {
    return first_shared_write_refusal(root) == std::meta::info{};
}

// The goal that rule 1 asks of a reference to the type of the node, for the
// property.  A reference to const asks Shareable of the target.  A reference
// to a target that is not const holds when two threads can write the target,
// and refuses otherwise.  For SendableBeside, each reference holds only when
// two threads can write the target (THE VALUE BESIDE ITS SENDER).  For
// SendableOwned, each reference refuses (THE VALUE THAT OUTLIVES ITS SENDER).
struct ReferenceAnswer {
    bool is_refused = false;
    Goal next{};
    bool has_next = false;
};

[[nodiscard]] consteval ReferenceAnswer answer_reference(::foundation::reflect::TypeNode target, Property property) {
    if (property == Property::SendOwned) return ReferenceAnswer{.is_refused = true};
    if (property == Property::SendBeside) return ReferenceAnswer{.is_refused = !is_shared_writable(target.type)};
    if (target.was_const) {
        return ReferenceAnswer{.is_refused = false, .next = Goal{target.type, Property::Share}, .has_next = true};
    }
    return ReferenceAnswer{.is_refused = !is_shared_writable(target.type)};
}

// Puts the goal of each type argument of a specialization that rule 8 reads on
// the list: a reference argument through rule 1, and each other type argument
// as a tag goal of the same property.  The type of a value argument names no
// state of an object, so it asks nothing.  False when a reference argument
// refuses.
[[nodiscard]] consteval bool push_arguments(std::meta::info specialization, Property property, GoalList& pending) {
    namespace refl = ::foundation::reflect;
    for (const refl::TypeNode& argument : refl::argument_components_of(refl::TypeNode{specialization, true})) {
        if (argument.is_value_argument) continue;
        if (argument.was_reference) {
            const ReferenceAnswer answer = answer_reference(argument, property);
            if (answer.is_refused) return false;
            if (answer.has_next) pending.push(answer.next);
            continue;
        }
        pending.push(Goal{argument.type, property, Reach::Tag});
    }
    return true;
}

}  // namespace detail

// The first type that refuses the property, or a null reflection when the
// root holds it (THE RULES).  The root must be complete.  Complexity: linear
// in the number of distinct goals, times the scan of the visited list.
[[nodiscard]] consteval std::meta::info first_refusal(std::meta::info root, Property property) {
    namespace refl = ::foundation::reflect;
    const std::meta::info declared_root = std::meta::dealias(root);
    detail::GoalList pending;
    // Rule 1, a reference at the root.  Each part below the root is bare, and
    // its node tells whether it was a reference.
    if (std::meta::is_reference_type(declared_root)) {
        const detail::ReferenceAnswer answer =
            detail::answer_reference(refl::node_reached_indirectly(declared_root), property);
        if (answer.is_refused) return declared_root;
        if (answer.has_next) pending.push(answer.next);
    } else {
        pending.push(detail::Goal{declared_root, property});
    }
    detail::GoalList visited;
    while (!pending.is_empty()) {
        const detail::Goal goal = pending.pop();
        if (visited.holds(goal)) continue;
        visited.push(goal);

        const std::meta::info bare = refl::bare_type(goal.type);
        // Rule 2.
        if (std::meta::is_arithmetic_type(bare) || std::meta::is_enum_type(bare)
            || std::meta::is_null_pointer_type(bare) || std::meta::is_member_pointer_type(bare)
            || std::meta::is_function_type(bare) || std::meta::is_void_type(bare)) {
            continue;
        }
        if (std::meta::is_pointer_type(bare)) {
            if (std::meta::is_function_type(std::meta::remove_pointer(bare))) continue;
            // Rule 3.
            return bare;
        }
        // Rule 4.
        if (std::meta::is_array_type(bare)) {
            pending.push(detail::Goal{refl::bare_type(std::meta::remove_all_extents(bare)), goal.property});
            continue;
        }
        if (!detail::is_class_or_union(bare)) return bare;
        if (!std::meta::is_complete_type(bare)) {
            if (goal.reach == detail::Reach::Tag || detail::is_stated(bare, goal.property)) continue;
            return bare;
        }
        // Rule 5.  A class of one owner thread moves to no other thread.
        if (detail::states(bare, detail::refusing_annotation(goal.property))) return bare;
        if (goal.property != Property::Share && detail::states(bare, ^^thread_bound)) return bare;
        // A cell of a payload holds each property when its payload moves.
        if (detail::states(bare, ^^payload_cell)) {
            const std::meta::info payload = detail::payload_of_cell(bare);
            if (payload == std::meta::info{}) return bare;
            const Property moved = goal.property == Property::Share ? Property::Send : goal.property;
            pending.push(detail::Goal{payload, moved, detail::Reach::Part});
            continue;
        }
        if (detail::states(bare, detail::stating_annotation(goal.property))) continue;
        const bool is_specialization = std::meta::has_template_arguments(bare);
        // Rule 1, a class that refers to the object of its first argument.
        if (detail::states(bare, ^^refers)) {
            if (!is_specialization) return bare;
            const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{bare, true});
            if (arguments.empty() || arguments.begin()->is_value_argument) return bare;
            const detail::ReferenceAnswer answer = detail::answer_reference(*arguments.begin(), goal.property);
            if (answer.is_refused) return bare;
            if (answer.has_next) pending.push(answer.next);
            continue;
        }
        // Rule 1, a class that is the one writer of the object of its first
        // argument.  A value moves with its target, and a read through a
        // reference to const of it can still write the target.
        if (detail::states(bare, ^^refers_unique)) {
            if (!is_specialization) return bare;
            const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{bare, true});
            if (arguments.empty() || arguments.begin()->is_value_argument) return bare;
            const std::meta::info target = arguments.begin()->type;
            // The target can end before the value (THE VALUE THAT OUTLIVES
            // ITS SENDER).
            if (goal.property == Property::SendOwned) return bare;
            // Beside its sender, the target is also the sender's (THE VALUE
            // BESIDE ITS SENDER), so it falls to the shared write below.
            if (goal.property == Property::Send) {
                pending.push(detail::Goal{target, Property::Send});
                continue;
            }
            if (!detail::is_shared_writable(target)) return bare;
            continue;
        }
        // Rule 1, a class that refers to the object of its first argument, and
        // that gives only const access to it through a const object.  A copy
        // of a const object would give a writer, so such a class refuses.
        if (detail::states(bare, ^^refers_deep)) {
            if (!is_specialization || std::meta::is_copy_constructible_type(bare)
                || std::meta::is_copy_assignable_type(bare)) {
                return bare;
            }
            const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{bare, true});
            if (arguments.empty() || arguments.begin()->is_value_argument) return bare;
            if (goal.property == Property::Share) {
                pending.push(detail::Goal{arguments.begin()->type, Property::Share});
                continue;
            }
            const detail::ReferenceAnswer answer = detail::answer_reference(*arguments.begin(), goal.property);
            if (answer.is_refused) return bare;
            if (answer.has_next) pending.push(answer.next);
            continue;
        }
        // Rule 6.  The class holds an object of its first argument, and no
        // value of its other arguments.
        if (detail::states(bare, ^^by_arguments)) {
            if (!is_specialization) return bare;
            const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{bare, true});
            if (arguments.empty() || arguments.begin()->is_value_argument) return bare;
            const refl::TypeNode owned = *arguments.begin();
            if (!owned.was_reference) {
                pending.push(detail::Goal{owned.type, goal.property, detail::Reach::Part});
                continue;
            }
            const detail::ReferenceAnswer answer = detail::answer_reference(owned, goal.property);
            if (answer.is_refused) return bare;
            if (answer.has_next) pending.push(answer.next);
            continue;
        }
        // Rule 7.
        if (refl::holds_unreadable_state(refl::TypeNode{bare, true})) return bare;
        // Rule 8.  A member that is a reference is not held by value, and its
        // node is its target.
        for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{bare, true})) {
            if (!part.is_held_by_value) {
                const detail::ReferenceAnswer answer = detail::answer_reference(part, goal.property);
                if (answer.is_refused) return bare;
                if (answer.has_next) pending.push(answer.next);
                continue;
            }
            if (goal.property == Property::Share && part.is_mutable && !detail::is_shared_writable(part.type)) {
                return bare;
            }
            pending.push(detail::Goal{part.type, goal.property});
        }
        if (is_specialization && !detail::push_arguments(bare, goal.property, pending)) return bare;
    }
    return std::meta::info{};
}

[[nodiscard]] consteval bool holds(std::meta::info type, Property property) {
    return first_refusal(type, property) == std::meta::info{};
}

// True when two threads can write one object of the type through a reference
// that is not const at the same time (THE SHARED WRITE).
[[nodiscard]] consteval bool is_synchronized(std::meta::info type) { return detail::is_shared_writable(type); }

// True when the class has a statement of its facts and its definition holds
// each property that the statement gives (THE OPAQUE TYPES).  The file that
// defines a class with a statement asserts it beside the definition.  A
// property that the statement does not give asks nothing, so a statement can
// give less than the definition holds.  The walk answers SendableBeside and
// SendableOwned of a declared class with is_sendable, so a statement of
// is_sendable asks the three of the definition.  The class must be complete.
[[nodiscard]] consteval bool holds_its_statement(std::meta::info type) {
    const opaque_facts statement = detail::statement_of(type);
    if (statement.type == std::meta::info{} || !std::meta::is_complete_type(std::meta::dealias(type))) return false;
    if (statement.is_sendable && !holds(type, Property::Send)) return false;
    if (statement.is_sendable && !holds(type, Property::SendBeside)) return false;
    if (statement.is_sendable && !holds(type, Property::SendOwned)) return false;
    if (statement.is_shareable && !holds(type, Property::Share)) return false;
    if (statement.is_synchronized && !is_synchronized(type)) return false;
    return true;
}

// The first type that two threads cannot write at the same time, or a null
// reflection when the type is Synchronized (THE SHARED WRITE).  A message
// names the part that refuses with it.
[[nodiscard]] consteval std::meta::info first_synchronized_refusal(std::meta::info type) {
    return detail::first_shared_write_refusal(type);
}

// A value of T can move to another thread.
template <class T>
concept Sendable = holds(^^T, Property::Send);

// Two threads can read one object of T through a reference to const at the
// same time.
template <class T>
concept Shareable = holds(^^T, Property::Share);

// A value of T can move to a thread that runs while its sender runs on (THE
// VALUE BESIDE ITS SENDER).
template <class T>
concept SendableBeside = holds(^^T, Property::SendBeside);

// A value of T holds no borrow, so it can move to a thread that uses it after
// its sender ends (THE VALUE THAT OUTLIVES ITS SENDER).
template <class T>
concept SendableOwned = holds(^^T, Property::SendOwned);

// Two threads can write one object of T through a reference that is not const
// at the same time.
template <class T>
concept Synchronized = is_synchronized(^^T);

}  // namespace foundation::transfer
