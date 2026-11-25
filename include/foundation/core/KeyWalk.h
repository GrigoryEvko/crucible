#pragma once

// The walks of the loan keys, over the kept parts of each class.
//
// A keyed door refuses a body that reaches a key of the brand of its owner, and
// a root door refuses a body that reaches an owner of the brand through a
// borrow (the gate and the borrow of a root door of foundation/core/Frame.h).
// The two gates read each class that a body reaches, and the bodies of one unit
// reach the same classes again and again.  This header keeps what the gates
// ask of one root:
//
//   key_reach_v<Root>            the marked classes that a value of Root
//                                reaches: each class with loan_key_mark of
//                                foundation/core/LoanKeyTrait.h
//   owner_reach_v<Root, Region>  the marked classes that a value of Root reaches
//                                through a borrow, with Root in the region
//   is_key_free_(type)           true for a root whose answer names no class and
//                                refuses nothing, with a read that costs less
//   is_owner_free_(type, region) than a read of the answer
//   is_keyless_v<Class>          true for a class that reaches no marked class
//                                and no class that a walk cannot read
//
// Kept answer
//   An answer names no key and no brand, so one variable of each root serves
//   each of them.  A walk that reaches a class that is incomplete in the unit,
//   or a class whose state reflection does not show, refuses, and a kept
//   refusal stays after the unit completes the class.  Each other walk reads
//   only complete classes, whose parts do not change, and it reads nothing of a
//   tag that a plain class only names (is_declared_tag_).  A value of the plain
//   class reaches no object of that tag, whatever the tag holds.  So each kept
//   answer that admits a body stays correct.  The kept variables are templates,
//   and the rule specialization-home refuses a specialization of one outside
//   this header.
//
// Order of the reads
//   A walk is a loop over a list of its own.  It asks of a class that it
//   reaches only is_keyless_v, and that answer asks only the answers of the
//   bases and the members of its class, so it ends with the depth of the class.
//   The answer of a closure is the union of the answers of its captures, and no
//   capture names the class of its closure.  So no kept answer asks itself, and
//   a class that reaches itself ends its walk with the visited list.

#include <foundation/core/LoanKeyTrait.h>
#include <foundation/core/TypeParts.h>
#include <foundation/reflect/Annotation.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>

namespace foundation::core::detail {

// The two regions of the walk of the borrow of a root door: what a value of
// the root holds, and what it reaches through a borrow.
enum class BorrowRegion : unsigned char {
    held,
    borrowed,
};

// One node of a walk: a bare type in a region.  kept is the reflection of the
// kept parts of a class, when the walk has it, and may_read tells that the walk
// can read the parts of the class.
struct KeyNode {
    std::meta::info type{};
    std::meta::info kept{};
    BorrowRegion region = BorrowRegion::held;
    bool may_read = false;
};

// The node of a type that a walk reaches other than by value.  The completeness
// query instantiates a specialization.
[[nodiscard]] consteval KeyNode indirect_key_node_(std::meta::info type, BorrowRegion region) {
    std::meta::info const bare = ::foundation::reflect::bare_type(type);
    return KeyNode{bare, std::meta::info{}, region, is_class_or_union_(bare) && std::meta::is_complete_type(bare)};
}

// The nodes of one walk in the order that the walk lists them, each pair of a
// type and a region one time.  A node that the walk cannot read does not stand
// for a node of the same type that it can read.  The list is a class and not
// std::vector, as TypeNodeList of foundation/reflect/TypeComponents.h is.
// Complexity: linear in the list for each push.
class KeyNodeList {
public:
    consteval KeyNodeList() noexcept = default;
    KeyNodeList(KeyNodeList const&) = delete("a walk owns its list, so a copy would free the list two times");
    KeyNodeList& operator=(KeyNodeList const&) = delete("a walk owns its list, so a copy would free the list two "
                                                        "times");
    // A destructor cannot be consteval, and a list exists only in a constant
    // evaluation, so the branch is the only path.
    constexpr ~KeyNodeList() {
        if consteval {
            delete[] nodes_;
        }
    }

    consteval void push(KeyNode const& node) {
        std::meta::info const type = node.type;
        if (is_leaf_(type)) return;
        KeyNode const* const nodes = nodes_;
        std::size_t const size = size_;
        for (std::size_t index = 0; index < size; ++index) {
            if (nodes[index].type == type && nodes[index].region == node.region
                && (nodes[index].may_read || !node.may_read)) {
                return;
            }
        }
        if (size_ == capacity_) grow_();
        nodes_[size_] = node;
        ++size_;
    }

    [[nodiscard]] consteval bool has_next() const { return next_ < size_; }
    [[nodiscard]] consteval KeyNode take() { return nodes_[next_++]; }

private:
    // Doubles the capacity, and moves each node into the new array.
    consteval void grow_() {
        std::size_t const capacity = capacity_ == 0 ? 32 : 2 * capacity_;
        KeyNode* const nodes = new KeyNode[capacity]{};
        for (std::size_t index = 0; index < size_; ++index)
            nodes[index] = nodes_[index];
        delete[] nodes_;
        nodes_ = nodes;
        capacity_ = capacity;
    }

    KeyNode* nodes_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t next_ = 0;
};

// The most marked classes that one answer names.  A root that reaches more is
// refused.
inline constexpr std::size_t key_room = 32;

// The marked classes that a root reaches.  is_refused tells that the root
// reaches a class that the walk cannot read, or more classes than key_room.
struct KeyReach {
    std::meta::info types[key_room]{};
    std::size_t size = 0;
    bool is_refused = false;

    [[nodiscard]] consteval bool holds(std::meta::info type) const {
        for (std::size_t index = 0; index < size; ++index) {
            if (types[index] == type) return true;
        }
        return false;
    }

    consteval void add(std::meta::info type) {
        if (holds(type)) return;
        if (size == key_room) {
            is_refused = true;
            return;
        }
        types[size] = type;
        ++size;
    }

    consteval void add_each(KeyReach const& other) {
        is_refused = is_refused || other.is_refused;
        for (std::size_t index = 0; index < other.size; ++index)
            add(other.types[index]);
    }

    [[nodiscard]] consteval bool is_empty() const { return size == 0 && !is_refused; }
};

// A plain tag: an empty class with no base, no member and no key mark.
template <class Class>
inline constexpr bool is_plain_tag_v =
    !::foundation::reflect::carries_annotation(^^Class, ^^loan_key_mark)
    && ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{^^Class, true}).empty();

// The class that a pointer to a plain tag converts to, and a pointer to no
// other class.
struct PlainTagTest final {
    template <class Class>
        requires(is_plain_tag_v<Class>)
    consteval PlainTagTest(Class*) noexcept {}
};

// True for a plain tag that is complete and no specialization, such as a brand.
// The read is the read of is_plain_class_ of foundation/core/TypeParts.h.
[[nodiscard]] consteval bool is_plain_tag_(std::meta::info type) {
    if (!std::meta::is_class_type(type) || std::meta::has_template_arguments(type)) return false;
    if (!std::meta::is_complete_type(type) || !std::meta::is_empty_type(type)) return false;
    return std::meta::is_convertible_type(std::meta::add_pointer(type), ^^PlainTagTest);
}

[[nodiscard]] consteval bool is_keyless_class_(std::meta::info type);

// The kept answer of is_keyless_class_: the class has no key mark and hides no
// part, each type argument of it is a plain tag, and each base and each member
// is a class with the same answer.  A pointer, a reference and a function type
// give no answer here, so such a class goes to a walk.
template <class Class>
consteval bool is_kept_keyless_() {
    TypeParts const& parts = type_parts_v<Class>;
    if (parts.is_unreadable || parts.is_key_marked || parts.hides_members) return false;
    PartRun const run = parts.run();
    for (std::size_t index = 0; index < parts.member_start; ++index) {
        TypePart const part = run[index];
        if (part.has(PartFact::was_reference) || !is_plain_tag_(part.type)) return false;
    }
    for (std::size_t index = parts.member_start; index < parts.size; ++index) {
        TypePart const part = run[index];
        if (part.has(PartFact::was_reference)) return false;
        std::meta::info held = part.type;
        if (std::meta::is_array_type(held)) {
            held = ::foundation::reflect::bare_type(std::meta::remove_all_extents(held));
        }
        if (!is_class_or_union_(held) || !is_keyless_class_(held)) return false;
    }
    return true;
}

template <class Class>
inline constexpr bool is_keyless_v = is_kept_keyless_<Class>();

// The class that a pointer to a keyless class converts to, and a pointer to no
// other class.
struct KeylessTest final {
    template <class Class>
        requires(is_keyless_v<Class>)
    consteval KeylessTest(Class*) noexcept {}
};

// True for a class that reaches no marked class, no incomplete class and no
// class whose state reflection does not show.  False tells nothing.  The class
// is complete.  Complexity: linear in the parts of the class, one time for each
// unit.
[[nodiscard]] consteval bool is_keyless_class_(std::meta::info type) {
    return std::meta::is_convertible_type(std::meta::add_pointer(type), ^^KeylessTest);
}

// Lists the parts of a type that is no class: the target of a pointer and each
// type that a function type or a pointer to member names in the region target,
// and the element of an array in the region of the array.  The element of an
// array behind a pointer can be incomplete, so each element is a node of its
// own.
consteval void list_parts_of_no_class_(KeyNodeList& list, KeyNode const& node, BorrowRegion target) {
    std::meta::info const type = node.type;
    if (std::meta::is_pointer_type(type)) {
        list.push(indirect_key_node_(std::meta::remove_pointer(type), target));
        return;
    }
    if (std::meta::is_array_type(type)) {
        list.push(indirect_key_node_(std::meta::remove_all_extents(type), node.region));
        return;
    }
    for (::foundation::reflect::TypeNode const& component : ::foundation::reflect::argument_components_of(
             ::foundation::reflect::TypeNode{type, false}, ::foundation::reflect::SpecializationRead::Instantiating)) {
        list.push(KeyNode{component.type, std::meta::info{}, target, component.may_read_members});
    }
}

// Lists the bases and the members that the parts hold: a member by reference in
// the region reference, and each other part in the region.
consteval void list_held_parts_(KeyNodeList& list, TypeParts const& parts, BorrowRegion region,
                                BorrowRegion reference) {
    PartRun const run = parts.run();
    for (std::size_t index = parts.member_start; index < parts.size; ++index) {
        TypePart const part = run[index];
        if (part.has(PartFact::was_reference)) {
            list.push(indirect_key_node_(part.type, reference));
        } else {
            list.push(KeyNode{part.type, part.kept, region, true});
        }
    }
}

// Lists the bases and the members of a class, also the ones that its parts hide.
consteval void list_members_of_class_(KeyNodeList& list, std::meta::info type, TypeParts const& parts,
                                      BorrowRegion region, BorrowRegion reference) {
    list_held_parts_(list, parts, region, reference);
    if (!parts.hides_members) return;
    list_held_parts_(list, std::meta::extract<TypeParts const&>(kept_held_parts_of(type)), region, reference);
}

// True for a template argument of a plain class that is an incomplete class,
// such as the tag of an Id that a unit only declares.  parts are the parts of
// the class, part is the argument, and argument is the node of it.  A plain class
// holds no object of a type argument, no pointer and no borrow
// (foundation/core/TypeParts.h), so a value of it reaches no object of the tag,
// also after the unit completes the tag.  An argument that is a reference type,
// and an incomplete argument of each other class, is no such tag.
[[nodiscard]] consteval bool is_declared_tag_(TypeParts const& parts, TypePart const& part, KeyNode const& argument) {
    return parts.is_plain && !part.has(PartFact::was_reference) && is_class_or_union_(argument.type)
        && !argument.may_read;
}

// The marked classes that the walk of the gate reaches from the node.  Each
// node of the walk is held: the walk follows each base, each member, each
// target of a reference or a pointer, and each type that a template argument, a
// function type or a pointer to member names, apart from a declared tag of a
// plain class.  Complexity: linear in the distinct types that the node reaches,
// times the scan of the list.
[[nodiscard]] consteval KeyReach walk_keys_(KeyNode const& start) {
    constexpr BorrowRegion held = BorrowRegion::held;
    KeyReach reach{};
    KeyNodeList list;
    list.push(start);
    while (list.has_next()) {
        KeyNode const node = list.take();
        std::meta::info const type = node.type;
        if (!is_class_or_union_(type)) {
            list_parts_of_no_class_(list, node, held);
            continue;
        }
        if (!node.may_read) {
            reach.is_refused = true;
            return reach;
        }
        if (is_keyless_class_(type)) continue;
        TypeParts const& parts =
            std::meta::extract<TypeParts const&>(node.kept == std::meta::info{} ? kept_parts_of(type) : node.kept);
        if (parts.is_unreadable) {
            reach.is_refused = true;
            return reach;
        }
        if (parts.is_key_marked) reach.add(type);
        PartRun const run = parts.run();
        for (std::size_t index = 0; index < parts.member_start; ++index) {
            KeyNode const argument = indirect_key_node_(run[index].type, held);
            if (!is_declared_tag_(parts, run[index], argument)) list.push(argument);
        }
        list_members_of_class_(list, type, parts, held, held);
    }
    return reach;
}

// True for a class with the shape of a closure: no name and no template
// arguments.  The class of a closure is unique to its lambda, and no capture
// names it, so an answer of it is the union of the answers of its captures.  The
// captures of a closure show as its members, and the read of the members of the
// class reads them also when the class carries a mark.
[[nodiscard]] consteval bool has_closure_shape_(std::meta::info type) {
    return std::meta::is_class_type(type) && !std::meta::has_identifier(type)
        && !std::meta::has_template_arguments(type);
}

// The parts of a class with the shape of a closure: each base and each capture.
[[nodiscard]] consteval ::foundation::reflect::TypeNodeList captures_of_(std::meta::info closure) {
    return ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{closure, true},
                                                       ::foundation::reflect::SpecializationRead::Instantiating);
}

// The answer of a root: the union of the answers of the parts of a closure, or
// the walk of each other type.  Complexity: the walk of each part, one time for
// each unit, and linear in the parts.
template <class Root>
[[nodiscard]] consteval KeyReach list_key_reach_();

template <class Root>
inline constexpr KeyReach key_reach_v = list_key_reach_<Root>();

// The reflection of the kept answer of the walk of the gate for a root.
[[nodiscard]] consteval std::meta::info kept_key_reach_of(std::meta::info root) {
    std::meta::info const arguments[] = {root};
    return std::meta::substitute(^^key_reach_v, arguments);
}

template <class Root>
[[nodiscard]] consteval KeyReach list_key_reach_() {
    std::meta::info const root = std::meta::dealias(^^Root);
    if constexpr (!has_closure_shape_(^^Root)) {
        return walk_keys_(KeyNode{root, std::meta::info{}, BorrowRegion::held, true});
    } else {
        KeyReach reach{};
        ::foundation::reflect::TypeNodeList const captures = captures_of_(root);
        reach.is_refused = captures.empty() && !std::meta::is_empty_type(root);
        for (::foundation::reflect::TypeNode const& capture : captures) {
            if (is_leaf_(capture.type)) continue;
            if (is_class_or_union_(capture.type) && !capture.may_read_members) {
                reach.is_refused = true;
            } else {
                reach.add_each(std::meta::extract<KeyReach const&>(kept_key_reach_of(capture.type)));
            }
        }
        return reach;
    }
}

// True when the walk of the gate from a root reaches no marked class and refuses
// nothing.  A closure asks the same of each of its parts, so a closure of free
// parts makes no answer of its own.
template <class Root>
[[nodiscard]] consteval bool is_kept_key_free_();

template <class Root>
inline constexpr bool is_key_free_v = is_kept_key_free_<Root>();

// The class that a pointer to a free root converts to, and a pointer to no other
// root.
struct KeyFreeTest final {
    template <class Root>
        requires(is_key_free_v<Root>)
    consteval KeyFreeTest(Root*) noexcept {}
};

// The kept answer of is_key_free_v for a bare type.  A class that is incomplete
// in the unit is not free.
[[nodiscard]] consteval bool is_key_free_(std::meta::info type) {
    if (is_leaf_(type)) return true;
    if (is_class_or_union_(type)) {
        if (!std::meta::is_complete_type(type)) return false;
        if (!has_closure_shape_(type) && is_keyless_class_(type)) return true;
    }
    return std::meta::is_convertible_type(std::meta::add_pointer(type), ^^KeyFreeTest);
}

template <class Root>
[[nodiscard]] consteval bool is_kept_key_free_() {
    if constexpr (!has_closure_shape_(^^Root)) {
        return key_reach_v<Root>.is_empty();
    } else {
        ::foundation::reflect::TypeNodeList const captures = captures_of_(std::meta::dealias(^^Root));
        if (captures.empty()) return std::meta::is_empty_type(^^Root);
        for (::foundation::reflect::TypeNode const& capture : captures) {
            if (!is_key_free_(capture.type)) return false;
        }
        return true;
    }
}

// The class that a pointer to a class with the key mark converts to, and a
// pointer to no other class.
struct MarkedClassTest final {
    template <class Class>
        requires(type_parts_v<Class>.is_key_marked)
    consteval MarkedClassTest(Class*) noexcept {}
};

// True for a complete class with the key mark that has a name or template
// arguments.  An answer of a walk names only marked classes, and the answer of a
// class with the shape of a closure does not name that class.  So a gate that
// asks for another class as its key refuses.
[[nodiscard]] consteval bool is_marked_key_(std::meta::info type) {
    if (!std::meta::is_class_type(type) || has_closure_shape_(type) || !std::meta::is_complete_type(type)) {
        return false;
    }
    return std::meta::is_convertible_type(std::meta::add_pointer(type), ^^MarkedClassTest);
}

// The marked specializations that the walk of the borrow of a root door reaches
// through a borrow, from the node.  Each node has a region, and the walk keeps
// each pair of a type and a region one time:
//   * a base and a member by value have the region of their class.  A member
//     that is a reference, the target of a pointer, each part of a function
//     type and of a member pointer, and the first argument of a class that
//     states refers{}, refers_unique{} or refers_deep{} are borrowed.  The
//     element of an array has the region of the array
//   * each other template argument names a type and reaches no object, so it
//     is held, apart from an argument that is a reference type.  The members
//     give each borrow.  The handle of a PermissionedChannel names its payload
//     in consumer_pops<Form, T, Handle> and holds none, and with the region of
//     its class that handle would refuse a payload that it takes by value
//   * a class that states by_arguments{}, such as a Box, owns the object of
//     its first argument: that argument has the region of the class, and the
//     walk reads no member
//   * a class that states synchronized{} or payload_cell{}, reached through a
//     borrow, hands its values in and out by value (foundation/Transfer.h):
//     the walk reads its arguments as held and no member.  The walk thus
//     trusts each statement of such a class for its private state
// Complexity: linear in the distinct pairs of a type and a region, times the
// scan of the list.
[[nodiscard]] consteval KeyReach walk_owners_(KeyNode const& start) {
    constexpr BorrowRegion held = BorrowRegion::held;
    constexpr BorrowRegion borrowed = BorrowRegion::borrowed;
    KeyReach reach{};
    KeyNodeList list;
    list.push(start);
    while (list.has_next()) {
        KeyNode const node = list.take();
        std::meta::info const type = node.type;
        BorrowRegion const region = node.region;
        if (!is_class_or_union_(type)) {
            list_parts_of_no_class_(list, node, borrowed);
            continue;
        }
        if (!node.may_read) {
            reach.is_refused = true;
            return reach;
        }
        if (is_keyless_class_(type)) continue;
        TypeParts const& parts =
            std::meta::extract<TypeParts const&>(node.kept == std::meta::info{} ? kept_parts_of(type) : node.kept);
        if (parts.is_unreadable) {
            reach.is_refused = true;
            return reach;
        }
        if (parts.is_key_marked && region == borrowed && std::meta::has_template_arguments(type)) reach.add(type);
        PartRun const run = parts.run();
        for (std::size_t index = 0; index < parts.member_start; ++index) {
            TypePart const part = run[index];
            BorrowRegion target = held;
            if (part.has(PartFact::was_reference) || (parts.is_borrow && part.has(PartFact::is_first))) {
                target = borrowed;
            } else if (parts.states_by_arguments && part.has(PartFact::is_first)) {
                target = region;
            }
            KeyNode const argument = indirect_key_node_(part.type, target);
            if (!is_declared_tag_(parts, part, argument)) list.push(argument);
        }
        if (parts.states_by_arguments || (parts.states_cell && region == borrowed)) continue;
        list_members_of_class_(list, type, parts, region, borrowed);
    }
    return reach;
}

// The region of a part of a closure in the region.
[[nodiscard]] consteval BorrowRegion region_of_capture_(::foundation::reflect::TypeNode const& capture,
                                                        BorrowRegion region) {
    return capture.was_reference ? BorrowRegion::borrowed : region;
}

// The answer of a root in the region, in the form of list_key_reach_.
template <class Root, BorrowRegion Region>
[[nodiscard]] consteval KeyReach list_owner_reach_();

template <class Root, BorrowRegion Region>
inline constexpr KeyReach owner_reach_v = list_owner_reach_<Root, Region>();

// The reflection of the kept answer of the walk of the borrow of a root door
// for a root in the region.
[[nodiscard]] consteval std::meta::info kept_owner_reach_of(std::meta::info root, BorrowRegion region) {
    std::meta::info const arguments[] = {root, std::meta::reflect_constant(region)};
    return std::meta::substitute(^^owner_reach_v, arguments);
}

template <class Root, BorrowRegion Region>
[[nodiscard]] consteval KeyReach list_owner_reach_() {
    std::meta::info const root = std::meta::dealias(^^Root);
    if constexpr (!has_closure_shape_(^^Root)) {
        return walk_owners_(KeyNode{root, std::meta::info{}, Region, true});
    } else {
        KeyReach reach{};
        ::foundation::reflect::TypeNodeList const captures = captures_of_(root);
        reach.is_refused = captures.empty() && !std::meta::is_empty_type(root);
        for (::foundation::reflect::TypeNode const& capture : captures) {
            if (is_leaf_(capture.type)) continue;
            if (is_class_or_union_(capture.type) && !capture.may_read_members) {
                reach.is_refused = true;
            } else {
                std::meta::info const kept = kept_owner_reach_of(capture.type, region_of_capture_(capture, Region));
                reach.add_each(std::meta::extract<KeyReach const&>(kept));
            }
        }
        return reach;
    }
}

// True when the walk of the borrow of a root door, from a root in the region,
// reaches no marked class through a borrow and refuses nothing, in the form of
// is_key_free_v.
template <class Root, BorrowRegion Region>
[[nodiscard]] consteval bool is_kept_owner_free_();

template <class Root, BorrowRegion Region>
inline constexpr bool is_owner_free_v = is_kept_owner_free_<Root, Region>();

// The class that a pointer to a free root in the region converts to, and a
// pointer to no other root.
template <BorrowRegion Region>
struct OwnerFreeTest final {
    template <class Root>
        requires(is_owner_free_v<Root, Region>)
    consteval OwnerFreeTest(Root*) noexcept {}
};

// The kept answer of is_owner_free_v for a bare type in the region.  A class that
// is incomplete in the unit is not free.
[[nodiscard]] consteval bool is_owner_free_(std::meta::info type, BorrowRegion region) {
    if (is_leaf_(type)) return true;
    if (is_class_or_union_(type)) {
        if (!std::meta::is_complete_type(type)) return false;
        if (!has_closure_shape_(type) && is_keyless_class_(type)) return true;
    }
    std::meta::info const test = region == BorrowRegion::borrowed ? ^^OwnerFreeTest<BorrowRegion::borrowed>
                                                                  : ^^OwnerFreeTest<BorrowRegion::held>;
    return std::meta::is_convertible_type(std::meta::add_pointer(type), test);
}

template <class Root, BorrowRegion Region>
[[nodiscard]] consteval bool is_kept_owner_free_() {
    if constexpr (!has_closure_shape_(^^Root)) {
        return owner_reach_v<Root, Region>.is_empty();
    } else {
        ::foundation::reflect::TypeNodeList const captures = captures_of_(std::meta::dealias(^^Root));
        if (captures.empty()) return std::meta::is_empty_type(^^Root);
        for (::foundation::reflect::TypeNode const& capture : captures) {
            if (!is_owner_free_(capture.type, region_of_capture_(capture, Region))) return false;
        }
        return true;
    }
}

}  // namespace foundation::core::detail
