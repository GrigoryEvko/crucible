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
//     T is Synchronized when T is a class that states synchronized{}, a cell
//     of a payload that is Sendable, such as the Atomic of the base, an array
//     of such a type, or a class whose each base and each member is
//     Synchronized or is const and Shareable.  A statement of shareable{}
//     gives no shared write: the walk reads the parts of such a class as it
//     reads the parts of each other class.  A class that states
//     thread_bound{} is not Synchronized.  No write reaches a const member, so
//     a read is all it must allow.  A member that is a reference makes the
//     class not Synchronized.  A scalar is not Synchronized: two plain writes
//     of one scalar race.  An atomic of the standard library is not either:
//     the base quarantines it, and its Atomic takes its place.
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
//     not_shareable{} state Shareable: two threads read an object of the
//     class through a reference to const at the same time.  shareable{} does
//     not state a write.  synchronized{} states that two threads can write an
//     object of the class at the same time, and thus that two threads can read
//     it.  The walk checks the statement each time that it reads it: two
//     threads can write each base, and each data member that is not private is
//     no reference, and two threads can write it at the same time, or it is
//     const and Shareable.  A part that fails stops the build, and the
//     diagnostic names the class and the part.  A private member is state
//     that the code of the class orders, as the comment beside the statement
//     tells, and the check reads the doors of the class that write it (THE
//     DOORS OF A SYNCHRONIZED CLASS).
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
// THE DOORS OF A SYNCHRONIZED CLASS
//     The check of a statement of synchronized{} also reads the member
//     functions of the class.  A class whose private parts are each
//     Synchronized, or const and Shareable, writes only cells that two
//     threads can write, so each door of it is safe.  When a private part is
//     plain, which is a reference or a part that two threads cannot write,
//     each door of the class is one of these, or the build stops:
//       - const.  A const door writes only mutable parts, and a mutable part
//         that two threads cannot write stops the build;
//       - a door that takes a proof of the class (THE PROOF OF A DOOR);
//       - a claim door.  Its result is a proof of the class, an Option or a
//         Result of one, or an Option or a Result of a handle class whose own
//         member is a proof of the class.  A claim door makes its token with
//         one atomic step, such as a compare-exchange of a pool, and its
//         comment states the invariant that protects its plain writes: the
//         writes after the step happen only on the thread that won it, until
//         that thread gives the token back.  The claim doors of the base are
//         try_withdraw_producer_root, producer() and producer(lane) of a
//         pooled PermissionedChannel, claim_drain of each PermissionedChannel,
//         and try_withdraw_reader_root and reader() of SwmrSession.
//     A door is a public or protected member function or member function
//     template that is not a constructor, not a destructor and not deleted.
//     A static one is a door when it takes the class through a reference
//     that is not const.  GCC 16 gives no parameters of a member function template,
//     so the check reads the specialization of each template for a probe
//     body, a class whose call takes each argument list.  A template with no
//     such specialization stops the build, so the check fails closed.  A
//     friend and a free function are no member, and the check does not read
//     them: GCC 16 gives no friend of a class to reflection.  members_of of a
//     class lists no friend, and members_of of a namespace lists a friend only
//     when the namespace declares it too, with no query that tells a friend
//     apart.  One class has its doors exempt (THE EXEMPTION).
//
// THE PROOF OF A DOOR
//     A parameter is a proof of the class when its bare type is a token and
//     the token is tied to the class.  A token is a class that carries the
//     annotation authority{} of foundation/Linear.h and does not copy, such as
//     a Permission, a SoleWriter, an ExclusiveLease and the guard of a share,
//     or a class that states thread_bound{}, such as the claim of a producer.
//     A reference to const of a token counts only when the token is not
//     Shareable, so no two threads read one token.  The token is tied to the
//     class when it is the class or a member class of it, or when one of its
//     type arguments is the class, a member class of it, or a type that a
//     member alias of the class names.  So a token of an unrelated class, or
//     of a tag that the class does not declare as its own, proves nothing
//     about the class.  A LoanKey carries no authority, so it never counts,
//     and neither does a QuiescenceProof, which each thread can make.  A
//     token counts only when no other thread can build one (THE BUILD OF A
//     TOKEN).  The check reads the tie first and the build second, so a door
//     whose one tied token is open stops the build with its own message.
//
// THE BUILD OF A TOKEN
//     A token proves that one thread holds it only when no thread can build a
//     second one from nothing.  So a token counts only when its class is
//     closed: no code outside its friends and its own members builds an
//     object of it.  The token then comes from a mint of its owner, such as a
//     root, a claim door or the split of a permission.  A class is closed
//     when it is not an aggregate, has no public constructor template, and
//     each of its public constructors other than a copy and a move
//     constructor takes a gating parameter.  A gating parameter has no
//     default argument, and its class is closed.  When it is an lvalue
//     reference, its class is also a token, or a key: a class whose public
//     constructors are only a copy and a move constructor.  So the key of a
//     Permission or of a SoleWriter gates the constructor that takes it, and
//     a reference to a channel, which each thread can hold, gates nothing.
//     An aggregate is closed when one part with no default member initializer
//     is closed.  GCC 16 shows no inherited constructor, so a public
//     constructor of a base that no parameter gates, other than a default
//     constructor, makes the class open.  A token that copies counts only
//     when it is not Shareable, so no other thread copies it out of a
//     reference to const.  A walk deeper than closed_walk_depth levels counts
//     the class as open.
//
// THE EXEMPTION
//     The check does not read the doors of crucible::BackgroundThread.  Nine
//     of its doors take no proof, and plant 06 of the audit of the threads,
//     where two bodies of run_joined call set_numerics, proves the race.  The
//     keyed doors of the owner-key member series replace those doors, and
//     that series removes the exemption and the test that pins it to one
//     class.
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
struct synchronized final {};

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

// Stops the build when a class that states synchronized{} shows a part that
// a holder of a reference that is not const can write with no order (THE
// SHARED WRITE).  Such a part is a base that two threads cannot write, or a
// data member that is not private and that is a reference, or that two
// threads cannot write at the same time and that is not const and Shareable.
// A private member is state that the code of the class orders, as the comment
// beside the statement tells.  The diagnostic names the class and the part.
// The definition follows is_shared_writable.  Complexity: linear in the bases
// and the members, and the walk of each base and each member that is not
// private.
consteval void check_synchronized_statement(std::meta::info type);

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
        // A statement of the shared write, which the check reads (THE
        // ANNOTATIONS).
        if (states(type, ^^synchronized)) {
            check_synchronized_statement(type);
            continue;
        }
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

// The refusal of a statement of synchronized{} (check_synchronized_statement).
// The walk reads a specialization of the variable below, which stops the
// build.  Its three messages give the rule, the class and the part that
// refuses: a member, or the type of a base.
template <std::meta::info Class, std::meta::info Part>
consteval bool refuse_synchronized_statement() {
    static_assert(Class == std::meta::info{},
                  "transfer: a class states synchronized{}, but a part of it that is not private is a reference, or "
                  "two threads cannot write the part at the same time and it is not const and Shareable.  A base "
                  "counts as such a part, also a private one.  Make the part private, or give it a type that two "
                  "threads can write, such as an Atomic (THE SHARED WRITE of foundation/Transfer.h).  The next two "
                  "messages name the class and the part");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Part));
    return false;
}

template <std::meta::info Class, std::meta::info Part>
inline constexpr bool synchronized_statement_refusal = refuse_synchronized_statement<Class, Part>();

// Reads the refusal of the class for the part, which stops the build.
consteval void refuse_synchronized_part(std::meta::info type, std::meta::info part) {
    const std::meta::info arguments[] = {std::meta::reflect_constant(type), std::meta::reflect_constant(part)};
    static_cast<void>(std::meta::extract<bool>(std::meta::substitute(^^synchronized_statement_refusal, arguments)));
}

// The refusal of a private member that is mutable and that two threads cannot
// write (THE DOORS OF A SYNCHRONIZED CLASS): a const door writes it.
template <std::meta::info Class, std::meta::info Part>
consteval bool refuse_mutable_part() {
    static_assert(Class == std::meta::info{},
                  "transfer: a class states synchronized{}, and a private member of it is mutable, but two threads "
                  "cannot write the member at the same time.  A const door of the class writes the member, so two "
                  "threads that call the door race.  Give the member a type that two threads can write, such as an "
                  "Atomic, or drop mutable (THE DOORS OF A SYNCHRONIZED CLASS of foundation/Transfer.h).  The next "
                  "two messages name the class and the member");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Part));
    return false;
}

template <std::meta::info Class, std::meta::info Part>
inline constexpr bool mutable_part_refusal = refuse_mutable_part<Class, Part>();

// The refusal of a door with no proof (THE DOORS OF A SYNCHRONIZED CLASS).
template <std::meta::info Class, std::meta::info Door, std::meta::info Part>
consteval bool refuse_unproven_door() {
    static_assert(Class == std::meta::info{},
                  "transfer: a class states synchronized{} and holds a plain private part, but a door of it is not "
                  "const, takes no proof of the class and gives no proof of the class, so two threads that call the "
                  "door can write the part at the same time.  Give the door a proof tied to the class, such as a "
                  "Permission of a tag of the class, make it a claim door that gives the token of the class, or "
                  "make it const over parts that two threads can write (THE DOORS OF A SYNCHRONIZED CLASS and THE "
                  "PROOF OF A DOOR of foundation/Transfer.h).  The next three messages name the class, the door and "
                  "the first plain part");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Door));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Part));
    return false;
}

template <std::meta::info Class, std::meta::info Door, std::meta::info Part>
inline constexpr bool unproven_door_refusal = refuse_unproven_door<Class, Door, Part>();

// The refusal of a member function template with no specialization for the
// probe body (THE DOORS OF A SYNCHRONIZED CLASS): the check cannot read its
// parameters, so it fails closed.
template <std::meta::info Class, std::meta::info Door>
consteval bool refuse_unreadable_door() {
    static_assert(Class == std::meta::info{},
                  "transfer: a class states synchronized{} and holds a plain private part, and a member function "
                  "template of it has no specialization for the probe body, a class whose call takes each argument "
                  "list.  GCC 16 gives no parameters of a template itself, so the check cannot read the proof of the "
                  "door, and it refuses the door.  Give the template one type parameter that a callable class can "
                  "fill, or make the door a function (THE DOORS OF A SYNCHRONIZED CLASS of foundation/Transfer.h).  "
                  "The next two messages name the class and the template");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Door));
    return false;
}

template <std::meta::info Class, std::meta::info Door>
inline constexpr bool unreadable_door_refusal = refuse_unreadable_door<Class, Door>();

// The refusal of a door whose one token another thread can build (THE BUILD
// OF A TOKEN).
template <std::meta::info Class, std::meta::info Door, std::meta::info Token>
consteval bool refuse_open_token() {
    static_assert(Class == std::meta::info{},
                  "transfer: a class states synchronized{} and holds a plain private part, and a door of it takes "
                  "or gives a token of the class that each thread can build, through a public constructor or an "
                  "aggregate initialization.  Two threads that each build one call the door at the same time.  Give "
                  "the token a private constructor, and let only a mint of its owner build it: a root, a claim "
                  "door or the split of a permission (THE BUILD OF A TOKEN of foundation/Transfer.h).  The next "
                  "three messages name the class, the door and the token");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Door));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Token));
    return false;
}

template <std::meta::info Class, std::meta::info Door, std::meta::info Token>
inline constexpr bool open_token_refusal = refuse_open_token<Class, Door, Token>();

// The probe body of a member function template (THE DOORS OF A SYNCHRONIZED
// CLASS): its call takes each argument list, gives nothing and does not
// throw.  It is no token, so a parameter of its type proves nothing.
struct door_probe_body final {
    template <class... Arguments>
    constexpr void operator()(Arguments&&... /*arguments*/) const noexcept {}
};

// One class whose doors the check does not read (THE EXEMPTION): the name of
// its namespace, which lies in the global namespace, and its own name.  The
// check file of foundation/Transfer.h pins the table to the one class.
struct synchronized_door_exemption {
    char const* space = "";
    char const* name = "";
};

inline constexpr synchronized_door_exemption synchronized_door_exemptions[] = {
    {.space = "crucible", .name = "BackgroundThread"},
};

// True when the class is an exemption (THE EXEMPTION).  The name query of
// foundation/reflect/Members.h compares each name and walks no scope.  A
// specialization is no exemption.  Complexity: linear in the exemptions,
// times the length of a name.
[[nodiscard]] consteval bool is_exempt_from_door_check(std::meta::info type) {
    namespace refl = ::foundation::reflect;
    if (!std::meta::has_identifier(type) || std::meta::has_template_arguments(type)) return false;
    const std::meta::info space = std::meta::parent_of(type);
    if (!std::meta::is_namespace(space) || std::meta::parent_of(space) != ^^::) return false;
    for (const synchronized_door_exemption& exemption : synchronized_door_exemptions) {
        if (refl::has_identifier_named(type, exemption.name) && refl::has_identifier_named(space, exemption.space)) {
            return true;
        }
    }
    return false;
}

// The depth of the walk of a closed class (THE BUILD OF A TOKEN).  A walk that
// needs more levels counts the class as open, so the check fails closed.
inline constexpr int closed_walk_depth = 4;

// True when the class has the shape of a token (THE PROOF OF A DOOR): it
// states thread_bound{}, or it carries authority{} and does not copy.
[[nodiscard]] consteval bool has_token_shape(std::meta::info bare) {
    if (!is_class_or_union(bare) || !std::meta::is_complete_type(bare)) return false;
    if (states(bare, ^^thread_bound)) return true;
    return states(bare, ^^::foundation::linear::authority) && !std::meta::is_copy_constructible_type(bare);
}

// True when a public constructor or constructor template of the class, other
// than a copy or a move constructor, is not deleted.  A class with no such
// constructor is a key: only its friends and its own members build it.
[[nodiscard]] consteval bool has_public_maker(std::meta::info bare) {
    const std::meta::info maker = ::foundation::reflect::find_member(
        bare, std::meta::access_context::unprivileged(), [](std::meta::info member) {
            if (std::meta::is_constructor_template(member)) return true;
            if (!std::meta::is_constructor(member) || std::meta::is_deleted(member)) return false;
            return !std::meta::is_copy_constructor(member) && !std::meta::is_move_constructor(member);
        });
    return maker != std::meta::info{};
}

[[nodiscard]] consteval bool is_closed_class(std::meta::info bare, int depth);

// True when a parameter of a public constructor needs a value that no other
// thread can make (THE BUILD OF A TOKEN).  The class of the parameter is
// closed, and the parameter has no default argument.  A value or an rvalue
// reference takes an object that the caller gives up.  An lvalue reference
// borrows an object that each thread can hold, so it counts only for a token
// or a key, which no other thread holds.
[[nodiscard]] consteval bool is_gating_parameter(std::meta::info parameter, int depth) {
    if (std::meta::has_default_argument(parameter)) return false;
    const std::meta::info type = std::meta::type_of(parameter);
    const std::meta::info bare = ::foundation::reflect::bare_type(type);
    if (!is_closed_class(bare, depth)) return false;
    if (!std::meta::is_lvalue_reference_type(type)) return true;
    return has_token_shape(bare) || !has_public_maker(bare);
}

// The constructors that open_constructor_of reads: each public one, or each
// public one that a using declaration of a derived class inherits, which is
// no default, copy or move constructor.
enum class MakerSet : unsigned char {
    own = 0,
    inherited = 1,
};

// True when a public constructor of the set takes no gating parameter, or a
// public constructor template exists (THE BUILD OF A TOKEN).  The check
// cannot read the parameters of a template, so a template opens the class.
[[nodiscard]] consteval bool has_open_constructor(std::meta::info bare, int depth, MakerSet set) {
    namespace refl = ::foundation::reflect;
    const std::meta::info open =
        refl::find_member(bare, std::meta::access_context::unprivileged(), [depth, set](std::meta::info member) {
            if (std::meta::is_constructor_template(member)) return true;
            if (!std::meta::is_constructor(member) || std::meta::is_deleted(member)) return false;
            if (std::meta::is_copy_constructor(member) || std::meta::is_move_constructor(member)) return false;
            if (set == MakerSet::inherited && std::meta::is_default_constructor(member)) return false;
            const std::meta::info gate = refl::find_parameter(
                member, [depth](std::meta::info parameter) { return is_gating_parameter(parameter, depth - 1); });
            return gate == std::meta::info{};
        });
    return open != std::meta::info{};
}

// True when no code outside the friends and the members of the class can
// build an object of it from nothing (THE BUILD OF A TOKEN).  GCC 16 shows no
// inherited constructor, so a public constructor of a base that a using
// declaration can inherit makes the class open when no parameter gates it.
// Complexity: linear in the public members of the class and of its bases,
// times the parameters of each constructor, to closed_walk_depth levels.
[[nodiscard]] consteval bool is_closed_class(std::meta::info bare, int depth) {
    namespace refl = ::foundation::reflect;
    if (depth <= 0 || !is_class_or_union(bare) || !std::meta::is_complete_type(bare)) return false;
    const refl::TypeNodeList parts = refl::member_components_of(refl::TypeNode{bare, true});
    // An aggregate initialization builds each part that it names, and each
    // other part from its default member initializer or from {}.  So one
    // closed part with no default member initializer closes the class.
    if (std::meta::is_aggregate_type(bare)) {
        for (const refl::TypeNode& part : parts) {
            const bool has_initializer =
                part.member != std::meta::info{} && std::meta::has_default_member_initializer(part.member);
            if (!has_initializer && is_closed_class(part.type, depth - 1)) return true;
        }
        return false;
    }
    if (has_open_constructor(bare, depth, MakerSet::own)) return false;
    for (const refl::TypeNode& part : parts) {
        if (part.member == std::meta::info{} && has_open_constructor(part.type, depth, MakerSet::inherited)) {
            return false;
        }
    }
    return true;
}

// True when the token can prove that one thread holds it (THE BUILD OF A
// TOKEN): the class is closed, and a token that copies is not Shareable, so
// no other thread copies the token out of a reference to const.
[[nodiscard]] consteval bool is_closed_token(std::meta::info bare) {
    if (!is_closed_class(bare, closed_walk_depth)) return false;
    return !std::meta::is_copy_constructible_type(bare) || !::foundation::transfer::holds(bare, Property::Share);
}

// True when the class is a token (THE PROOF OF A DOOR): it has the shape of a
// token, and no other thread can build one (THE BUILD OF A TOKEN).
[[nodiscard]] consteval bool is_door_token(std::meta::info bare) {
    return has_token_shape(bare) && is_closed_token(bare);
}

// True when the type is one of the own types of the owner (THE PROOF OF A
// DOOR): the owner, a member class of it, or a type that a public member
// alias of it names.  Complexity: linear in the members of the owner.
[[nodiscard]] consteval bool is_own_type_of(std::meta::info owner, std::meta::info candidate) {
    namespace refl = ::foundation::reflect;
    const std::meta::info bare = refl::bare_type(candidate);
    if (bare == owner) return true;
    if (is_class_or_union(bare) && std::meta::parent_of(bare) == owner) return true;
    const std::meta::info alias =
        refl::find_member(owner, std::meta::access_context::unprivileged(), [bare](std::meta::info member) {
            return std::meta::is_type_alias(member) && std::meta::dealias(member) == bare;
        });
    return alias != std::meta::info{};
}

// True when the token is tied to the owner (THE PROOF OF A DOOR): it is an own
// type of the owner, or one of its type arguments is one.
[[nodiscard]] consteval bool is_tied_to(std::meta::info token, std::meta::info owner) {
    namespace refl = ::foundation::reflect;
    if (is_own_type_of(owner, token)) return true;
    if (!std::meta::has_template_arguments(token)) return false;
    for (const refl::TypeNode& argument : refl::argument_components_of(refl::TypeNode{token, true})) {
        if (!argument.is_value_argument && is_own_type_of(owner, argument.type)) return true;
    }
    return false;
}

// True when the bare type is a token tied to the owner (THE PROOF OF A DOOR).
[[nodiscard]] consteval bool is_proof_type(std::meta::info bare, std::meta::info owner) {
    return is_door_token(bare) && is_tied_to(bare, owner);
}

// True when the bare type has the shape of a token and is tied to the owner,
// but another thread can build one (THE BUILD OF A TOKEN), so it proves no
// one holder.
[[nodiscard]] consteval bool is_open_token_of(std::meta::info bare, std::meta::info owner) {
    return has_token_shape(bare) && is_tied_to(bare, owner) && !is_closed_token(bare);
}

// True when the node of a parameter is a proof of the owner (THE PROOF OF A
// DOOR).  A reference to const of a token counts only when the token is not
// Shareable.
[[nodiscard]] consteval bool is_proof_parameter(::foundation::reflect::TypeNode parameter, std::meta::info owner) {
    if (!is_proof_type(parameter.type, owner)) return false;
    if (!parameter.was_reference || !parameter.was_const) return true;
    return !::foundation::transfer::holds(parameter.type, Property::Share);
}

// True when the bare result of a door is the token of a claim door (THE DOORS
// OF A SYNCHRONIZED CLASS): a proof of the owner, or a sum whose first type
// argument is a proof of the owner or a handle class whose own member is one.
// The read stops at the members of the handle, so the rule stays exact.
[[nodiscard]] consteval bool is_claim_result(std::meta::info result, std::meta::info owner) {
    namespace refl = ::foundation::reflect;
    if (is_proof_type(result, owner)) return true;
    if (!is_class_or_union(result) || !std::meta::has_template_arguments(result)
        || !states(result, ^^::foundation::linear::sum)) {
        return false;
    }
    const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{result, true});
    if (arguments.empty() || arguments.begin()->is_value_argument) return false;
    const std::meta::info held = arguments.begin()->type;
    if (is_proof_type(held, owner)) return true;
    if (!is_class_or_union(held) || !std::meta::is_complete_type(held)) return false;
    for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{held, true})) {
        if (part.member != std::meta::info{} && is_proof_type(part.type, owner)) return true;
    }
    return false;
}

// True when the member is a door (THE DOORS OF A SYNCHRONIZED CLASS).  A
// static member is read too, because a parameter can give it the object.
[[nodiscard]] consteval bool is_door(std::meta::info member) {
    const bool is_template = std::meta::is_function_template(member);
    if (!std::meta::is_function(member) && !is_template) return false;
    if (std::meta::is_private(member)) return false;
    if (is_template) return true;
    return !std::meta::is_constructor(member) && !std::meta::is_destructor(member) && !std::meta::is_deleted(member);
}

// True when the node of a parameter of a static function names the owner
// through a reference that is not const, so the function can write the
// object as a door does.
[[nodiscard]] consteval bool reaches_owner(::foundation::reflect::TypeNode parameter, std::meta::info owner) {
    return parameter.was_reference && !parameter.was_const && parameter.type == owner;
}

// The open token of the owner in the bare result of a door (THE BUILD OF A
// TOKEN), or a null reflection.  The read follows is_claim_result: the result,
// the first type argument of a sum, and the own members of that argument.
[[nodiscard]] consteval std::meta::info open_token_in_result(std::meta::info result, std::meta::info owner) {
    namespace refl = ::foundation::reflect;
    if (is_open_token_of(result, owner)) return result;
    if (!is_class_or_union(result) || !std::meta::has_template_arguments(result)
        || !states(result, ^^::foundation::linear::sum)) {
        return std::meta::info{};
    }
    const refl::TypeNodeList arguments = refl::argument_components_of(refl::TypeNode{result, true});
    if (arguments.empty() || arguments.begin()->is_value_argument) return std::meta::info{};
    const std::meta::info held = arguments.begin()->type;
    if (is_open_token_of(held, owner)) return held;
    if (!is_class_or_union(held) || !std::meta::is_complete_type(held)) return std::meta::info{};
    for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{held, true})) {
        if (part.member != std::meta::info{} && is_open_token_of(part.type, owner)) return part.type;
    }
    return std::meta::info{};
}

// The verdict of a door: it holds the rule, it holds no proof, its one proof
// is a token that another thread can build, or the check cannot read the
// parameters of its template.
enum class DoorVerdict : unsigned char {
    proven = 0,
    unproven = 1,
    unreadable = 2,
    open_token = 3,
};

// The verdict of a door and the open token that it names, or a null
// reflection.
struct DoorFinding {
    DoorVerdict verdict = DoorVerdict::proven;
    std::meta::info token{};
};

// The type of a function reads as its return node, and then a node for each
// parameter (argument_components_of of foundation/reflect/TypeComponents.h).
// The tie of a token comes first, and then its build: a door whose only
// token of the owner is open has the verdict open_token.
[[nodiscard]] consteval DoorFinding verdict_of_door(std::meta::info door, std::meta::info owner) {
    namespace refl = ::foundation::reflect;
    std::meta::info function = door;
    if (std::meta::is_function_template(door)) {
        const std::meta::info arguments[] = {^^door_probe_body};
        if (!std::meta::can_substitute(door, arguments)) return DoorFinding{.verdict = DoorVerdict::unreadable};
        function = std::meta::substitute(door, arguments);
        if (!std::meta::is_function(function)) return DoorFinding{.verdict = DoorVerdict::unreadable};
        if (std::meta::is_constructor(function) || std::meta::is_destructor(function)
            || std::meta::is_deleted(function)) {
            return DoorFinding{};
        }
    }
    const bool is_static = std::meta::is_static_member(function);
    if (!is_static && std::meta::is_const(function)) return DoorFinding{};
    const refl::TypeNodeList parts = refl::argument_components_of(refl::TypeNode{std::meta::type_of(function), true});
    bool is_result = true;
    bool does_reach_owner = !is_static;
    bool has_proof = false;
    bool gives_proof = false;
    std::meta::info open_token{};
    for (const refl::TypeNode& part : parts) {
        if (is_result) {
            gives_proof = is_claim_result(part.type, owner);
            open_token = open_token_in_result(part.type, owner);
            is_result = false;
            continue;
        }
        if (is_static && reaches_owner(part, owner)) does_reach_owner = true;
        if (is_proof_parameter(part, owner)) has_proof = true;
        if (open_token == std::meta::info{} && is_open_token_of(part.type, owner)) open_token = part.type;
    }
    if (!does_reach_owner || has_proof || gives_proof) return DoorFinding{};
    if (open_token != std::meta::info{}) return DoorFinding{.verdict = DoorVerdict::open_token, .token = open_token};
    return DoorFinding{.verdict = DoorVerdict::unproven};
}

// Stops the build at the first door of the class that does not hold the rule
// (THE DOORS OF A SYNCHRONIZED CLASS).  plain names the first plain private
// part.  The walk is unchecked, because a derived class can call a protected
// door, and is_door then drops each private member.  Complexity: linear in
// the members of the class, times the parameters of each door.
consteval void check_synchronized_doors(std::meta::info type, std::meta::info plain) {
    namespace refl = ::foundation::reflect;
    DoorFinding finding{};
    const std::meta::info door =
        refl::find_member(type, std::meta::access_context::unchecked(), [type, &finding](std::meta::info member) {
            if (!is_door(member)) return false;
            finding = verdict_of_door(member, type);
            return finding.verdict != DoorVerdict::proven;
        });
    if (door == std::meta::info{}) return;
    if (finding.verdict == DoorVerdict::unreadable) {
        const std::meta::info arguments[] = {std::meta::reflect_constant(type), std::meta::reflect_constant(door)};
        static_cast<void>(std::meta::extract<bool>(std::meta::substitute(^^unreadable_door_refusal, arguments)));
        return;
    }
    if (finding.verdict == DoorVerdict::open_token) {
        const std::meta::info arguments[] = {std::meta::reflect_constant(type), std::meta::reflect_constant(door),
                                             std::meta::reflect_constant(finding.token)};
        static_cast<void>(std::meta::extract<bool>(std::meta::substitute(^^open_token_refusal, arguments)));
        return;
    }
    const std::meta::info arguments[] = {std::meta::reflect_constant(type), std::meta::reflect_constant(door),
                                         std::meta::reflect_constant(plain)};
    static_cast<void>(std::meta::extract<bool>(std::meta::substitute(^^unproven_door_refusal, arguments)));
}

consteval void check_synchronized_statement(std::meta::info type) {
    namespace refl = ::foundation::reflect;
    const bool is_exempt = is_exempt_from_door_check(type);
    std::meta::info plain{};
    for (const refl::TypeNode& part : refl::member_components_of(refl::TypeNode{type, true})) {
        // A node of a base names no member.  Each base, private or not, is
        // Synchronized, so a base that two threads cannot write refuses.
        if (part.member == std::meta::info{}) {
            if (!is_shared_writable(part.type)) refuse_synchronized_part(type, part.type);
            continue;
        }
        // A member that is a reference is not held by value.  No write
        // reaches a const member, so a read is all it must allow.
        if (std::meta::is_private(part.member)) {
            // A const door writes a mutable member (THE DOORS OF A
            // SYNCHRONIZED CLASS).  The first plain member is enough for the
            // check of the doors, so the walk stops there.
            if (part.is_mutable) {
                if (!part.is_held_by_value || !is_shared_writable(part.type)) {
                    const std::meta::info arguments[] = {std::meta::reflect_constant(type),
                                                         std::meta::reflect_constant(part.member)};
                    static_cast<void>(
                        std::meta::extract<bool>(std::meta::substitute(^^mutable_part_refusal, arguments)));
                }
                continue;
            }
            if (is_exempt || plain != std::meta::info{}) continue;
            const bool holds_write = part.is_held_by_value
                                  && (part.was_const ? ::foundation::transfer::holds(part.type, Property::Share)
                                                     : is_shared_writable(part.type));
            if (!holds_write) plain = part.member;
            continue;
        }
        const bool holds_write = part.is_held_by_value
                              && (part.was_const ? ::foundation::transfer::holds(part.type, Property::Share)
                                                 : is_shared_writable(part.type));
        if (!holds_write) refuse_synchronized_part(type, part.member);
    }
    if (plain != std::meta::info{}) check_synchronized_doors(type, plain);
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
        // A statement of the shared write states the read of two threads too,
        // and the check reads it (THE ANNOTATIONS).
        if (goal.property == Property::Share && detail::states(bare, ^^synchronized)) {
            detail::check_synchronized_statement(bare);
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
