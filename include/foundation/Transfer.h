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
//        first type argument, and this rule applies to it.
//     2. A scalar, an enumeration, nullptr_t, a pointer to a function and a
//        pointer to a member hold each property.  A pointer to a function
//        names code, which no thread writes.
//     3. A pointer to an object holds no property.  The walk cannot see who
//        else holds the object.
//     4. An array holds a property when its element holds it.
//     5. A class or a union that states a property holds it or not as it
//        states (THE ANNOTATIONS).
//     6. A class that states by_arguments{} holds a property when each of its
//        type arguments holds it, and the walk reads no member of it.
//     7. A class that holds state that the walk cannot read holds no property.
//        A lambda with captures has this shape on a compiler that shows no
//        field of a capture.  On a compiler that shows the fields, the
//        captures of a lambda are its members, and rule 8 reads them.
//     8. Each other class or union holds a property when each base and each
//        non-static data member holds it, and, for a specialization, when each
//        type argument holds it.  The arguments count because a class can
//        keep a value of an argument in bytes, as an Option does.  A member
//        declared mutable makes the class not Shareable, unless two threads
//        can write the member at the same time, because a read through a
//        reference to const can write it.
//
// THE SHARED WRITE
//     T is Synchronized when T is a class that states shareable{}, such as
//     the Atomic of the base, an array of such a type, or a class whose each
//     base and each member is Synchronized or is const and Shareable.  No
//     write reaches a const member, so a read is all it must allow.  A member
//     that is a reference makes the class not Synchronized.  A scalar is not
//     Synchronized: two plain writes of one scalar race.  An atomic of the
//     standard library is not either: the base quarantines it, and its Atomic
//     takes its place.
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
//     by_arguments{} states that the class owns the objects of its type
//     arguments behind its pointers.  refers{} states that the class refers to
//     the object of its first type argument, as a reference does.  Each
//     statement is a claim that the code of the class makes true, and the
//     comment beside it gives the reason.
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

// The two properties that first_refusal walks (THE THREE PROPERTIES).  The
// shared write has a walk of its own, is_synchronized.
enum class Property : unsigned char {
    Send = 0,
    Share = 1,
};

// True when the type holds the property (THE RULES).  The definition follows
// first_refusal.
[[nodiscard]] consteval bool holds(std::meta::info type, Property property);

namespace detail {

// One question of the walk: one property of one type.  A type argument that
// is an incomplete class holds each property, because no value of it can be
// inside the class: a tag type is the usual case.
struct Goal {
    std::meta::info type{};
    Property property = Property::Send;
    bool is_argument = false;
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

// The annotation that states the property, and the one that refuses it.
[[nodiscard]] consteval std::meta::info stating_annotation(Property property) {
    return property == Property::Send ? ^^sendable : ^^shareable;
}

[[nodiscard]] consteval std::meta::info refusing_annotation(Property property) {
    return property == Property::Send ? ^^not_sendable : ^^not_shareable;
}

[[nodiscard]] consteval bool is_class_or_union(std::meta::info type) {
    return std::meta::is_class_type(type) || std::meta::is_union_type(type);
}

// True when two threads can write an object of the type at the same time
// (THE SHARED WRITE).  Complexity: linear in the number of distinct parts.
[[nodiscard]] consteval bool is_shared_writable(std::meta::info root) {
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
        if (!is_class_or_union(type) || !std::meta::is_complete_type(type)) return false;
        if (states(type, ^^not_shareable)) return false;
        if (states(type, ^^shareable)) continue;
        bool has_part = false;
        for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{type, true})) {
            has_part = true;
            // A member that is a reference is not held by value.
            if (!part.is_held_by_value) return false;
            // No write reaches a const member, so a read is all it must allow.
            if (part.was_const) {
                if (!::foundation::transfer::holds(part.type, Property::Share)) return false;
                continue;
            }
            pending.push(Goal{part.type, Property::Share});
        }
        // An empty class holds no state that a write can change.  A class that
        // is not empty and shows no part holds state that the walk cannot read.
        if (!has_part && !std::meta::is_empty_type(type)) return false;
    }
    return true;
}

// The goal that rule 1 asks of a reference to the type of the node.  A
// reference to const asks Shareable of the target.  A reference to a target
// that is not const holds when two threads can write the target, and refuses
// otherwise.
struct ReferenceAnswer {
    bool is_refused = false;
    Goal next{};
    bool has_next = false;
};

[[nodiscard]] consteval ReferenceAnswer answer_reference(::foundation::reflect::TypeNode target) {
    if (target.was_const) {
        return ReferenceAnswer{.is_refused = false, .next = Goal{target.type, Property::Share}, .has_next = true};
    }
    return ReferenceAnswer{.is_refused = !is_shared_writable(target.type)};
}

// Puts the goal of each type argument of a specialization on the list: a
// reference argument through rule 1, and each other type argument as a goal
// of the same property.  The type of a value argument names no state of an
// object, so it asks nothing.  False when a reference argument refuses.
[[nodiscard]] consteval bool push_arguments(std::meta::info specialization, Property property, GoalList& pending) {
    namespace refl = ::foundation::reflect;
    for (const refl::TypeNode& argument : refl::argument_components_of(refl::TypeNode{specialization, true})) {
        if (argument.is_value_argument) continue;
        if (argument.was_reference) {
            const ReferenceAnswer answer = answer_reference(argument);
            if (answer.is_refused) return false;
            if (answer.has_next) pending.push(answer.next);
            continue;
        }
        pending.push(Goal{argument.type, property, true});
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
        const detail::ReferenceAnswer answer = detail::answer_reference(refl::node_reached_indirectly(declared_root));
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
            if (goal.is_argument) continue;
            return bare;
        }
        // Rule 5.
        if (detail::states(bare, detail::refusing_annotation(goal.property))) return bare;
        if (detail::states(bare, detail::stating_annotation(goal.property))) continue;
        const bool is_specialization = std::meta::has_template_arguments(bare);
        // Rule 1, a class that refers to the object of its first argument.
        if (detail::states(bare, ^^refers)) {
            if (!is_specialization) return bare;
            const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{bare, true});
            if (arguments.empty() || arguments.begin()->is_value_argument) return bare;
            const detail::ReferenceAnswer answer = detail::answer_reference(*arguments.begin());
            if (answer.is_refused) return bare;
            if (answer.has_next) pending.push(answer.next);
            continue;
        }
        // Rule 6.
        if (detail::states(bare, ^^by_arguments)) {
            if (is_specialization && !detail::push_arguments(bare, goal.property, pending)) return bare;
            continue;
        }
        // Rule 7.
        if (refl::holds_unreadable_state(refl::TypeNode{bare, true})) return bare;
        // Rule 8.  A member that is a reference is not held by value, and its
        // node is its target.
        for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{bare, true})) {
            if (!part.is_held_by_value) {
                const detail::ReferenceAnswer answer = detail::answer_reference(part);
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

// A value of T can move to another thread.
template <class T>
concept Sendable = holds(^^T, Property::Send);

// Two threads can read one object of T through a reference to const at the
// same time.
template <class T>
concept Shareable = holds(^^T, Property::Share);

// Two threads can write one object of T through a reference that is not const
// at the same time.
template <class T>
concept Synchronized = is_synchronized(^^T);

}  // namespace foundation::transfer
