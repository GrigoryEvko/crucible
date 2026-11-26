#pragma once

// The store check of a frame body: no capture of the body takes a borrow out
// of the frame.
//
// A frame door lends a borrow to a body for the time of one call
// (foundation/core/Frame.h), and the result of the body holds no borrow
// (FrameResult of foundation/core/Choice.h).  A body can still write a
// borrow of the frame into an object that it captures, and the caller then
// reads that object after the frame ends.  StoresNoBorrowOutside<Body>
// refuses such a body by the types of its captures, whatever the door that
// made the borrow and whatever its brand.
//
// Walk
//   The walk reads each capture of the body (the members of its class, which
//   the sixth patch of GCC shows) and each type that a capture reaches.  Each
//   node of the walk is in one of three regions:
//     owned      the body owns the object: a capture by value, and each part
//                of it.  A write of it stays in the body
//     read_only  the body reads the object through a const reference, a
//                pointer to const or a borrow of a const element.  C++ const
//                does not go through a reference member, the target of a
//                pointer or a borrow, or a mutable member, so the walk reads
//                each part of such an object as C++ does: each one stays
//                writable when its type is not const
//     writable   the body writes the object, and the caller can read it after
//                the call: the target of a capture by a reference that is not
//                const, of a pointer to an object that is not const, or of a
//                borrow of an element that is not const, also inside a
//                read_only object
//   A borrow slot in the writable region refuses the body.  A borrow slot is
//   a pointer to an object, a class that states refers{}, refers_unique{} or
//   refers_deep{} (foundation/Transfer.h), such as a View or a Ref, and a
//   class whose state the walk cannot read.  So a capture of a View<T const>
//   by reference, or of an Option of an erased Ref, refuses the body, and so
//   does a capture of an object whose method can store a View.
//
//   The regions move as follows:
//     * the root is the class of the body, owned, or writable when the door
//       takes a named body that is no closure.  The caller can read a member
//       of a named class, and no member of a closure
//     * a member by value and a base keep the region of their class.  A
//       mutable member is writable.  A member by reference, the target of a
//       pointer and the target of a borrow are writable unless they are
//       const, and read_only then.  This holds in each region, also behind a
//       const pointer and in a read_only object.  A pointer is a borrow slot
//       only in the writable region
//     * a borrow names its target: borrow_target of its parts
//       (foundation/core/TypeParts.h), the type that its statement names or
//       else its first type argument, such as the T of a View<T> or a
//       Ref<T, Brand>.  A class of the base that states refers_deep{}, such as a Link,
//       gives only const access to its target when it is const, so its target
//       is read_only in a read_only object
//     * a class whose state the walk cannot read is a borrow slot in each
//       region, because a const object of it can still hold a reference
//       member.  A class that is incomplete in the unit stops the build in
//       each region, with an error that names the class and the cause.  A
//       unit that captures such an object includes the header that completes
//       its class
//     * a borrow of a frame, a class whose type arguments name a scope brand
//       (foundation/Brand.h), is no borrow slot.  Only that frame makes a
//       value of its brand, so a body of an inner frame writes none into it.
//       Its type arguments name its targets.  A ViewMut and a RefMut of an
//       outer frame are such borrows, and a Ref of a scope brand too
//     * an owner holds its storage through pointers that no caller sets: a
//       class on OwnerCore (foundation/core/Owner.h), a class that states
//       by_arguments{}, synchronized{} or payload_cell{}, or owns_its_storage{}.
//       The walk reads its type arguments in its own region and no member.  A
//       core whose state releases nothing, such as the run of a region, and a
//       share of a pool that states shares{} own no storage, so the walk reads
//       their members
//     * a handle of the base that states keeps_own_borrows{}
//       (foundation/core/TypeParts.h), such as an Epoch, is no borrow slot,
//       and the walk reads no part of it.  Each door of it that stores an
//       object runs ArenaKeeps, so the object holds borrows only of the brand
//       of the handle
//     * a class that states synchronized_by_proof{}
//       (foundation/core/TypeParts.h), in the base or outside it, is no
//       borrow slot after its check passes (the check of
//       synchronized_by_proof below), and the walk reads no part of it
//     * a pointer member of a class of the base that states refers_static{}
//       (foundation/Transfer.h), such as the pointer of a StaticText, names
//       static storage, so it is no borrow slot.  The walk reads each other
//       part of the class
//     * each other class gives its members.  A class template can hold a
//       value of a type argument in raw bytes, as a FixedList or an Option
//       does, so a class with an array of bytes or a union in a member gives
//       its type arguments too, in its own region.  A type argument that is
//       an incomplete class, such as a tag that a unit only declares, holds no
//       value
//     * an array gives its element in the region of the array
//   A function pointer, a pointer to member, a scalar and an enumeration are
//   no borrow slot.
//
// A capture by value of a plain value or of a borrow of a const element, a
// capture by const reference, and a capture by reference of an int, of an
// owner of plain values, of an arena, of an epoch or of a ViewMut of an outer
// frame pass.
// The cost is a refusal of a capture by reference of a large object that
// holds a borrow, such as a capture of this in a class with a member Ref.
// Such a body captures the parts that it writes.
//
// The walk is a function that is no template, so no unit can specialize what
// it admits.  It reads each pair of a type and a region one time.  It follows
// each pointer, borrow and borrow of a frame in its own visited list, so a
// class that reaches itself ends the walk, and the memo keeps the verdict of
// each captured type for the translation unit.  It reads a class through
// type_parts_v of foundation/core/TypeParts.h, which reads each class one
// time for the translation unit, with an operation budget for each class.
// Complexity: linear in the distinct pairs that the body reaches, times the
// scan of the visited list, at compile time only.

#include <foundation/core/Frame.h>
#include <foundation/core/TypeParts.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// The mark owns_its_storage of foundation/core/Owner.h names an owner whose
// type arguments the store check reads, and no member (the walk).

namespace detail {

// The three regions of the walk.
enum class StoreRegion : unsigned char {
    owned,
    read_only,
    writable,
};

// How a door takes its body: a named body that the caller can read after the
// call, or a body that the caller cannot read.
enum class BodyForm : unsigned char {
    unnamed,
    named,
};

// The region of the target of a reference, a pointer or a borrow: writable
// unless it is const.
[[nodiscard]] consteval StoreRegion target_region_(TypePart const& target) {
    return target.has(PartFact::was_const) ? StoreRegion::read_only : StoreRegion::writable;
}

// The refusal of a capture that reaches the class Type, whose definition the
// unit does not see.  The walk instantiates it only for such a class, and the
// two assertions then stop the build: the first gives the cause and the cure,
// and the second names the class.  So the walk fails closed on such a class in
// each region, and the error names the class.  The condition depends on Type
// and is false for each class.
template <std::meta::info Type>
consteval bool refuse_incomplete_class_() {
    static_assert(Type == std::meta::info{},
                  "the store check of a frame door: a capture of the body reaches a class, and the definition of that "
                  "class is not visible in this unit.  The check reads each part of a capture, so it refuses a class "
                  "whose parts it cannot read.  Include the header that defines the class.  The next message names "
                  "the class");
    static_assert(Type == std::meta::info{}, std::meta::display_string_of(Type));
    return true;
}

template <std::meta::info Type>
inline constexpr bool refuses_incomplete_class_v = refuse_incomplete_class_<Type>();

// Stops the build with the name of the class type, whose definition the unit
// does not see (refuse_incomplete_class_).
[[nodiscard]] consteval bool refuse_incomplete_capture_(std::meta::info type) {
    std::meta::info const arguments[] = {std::meta::reflect_constant(type)};
    return std::meta::extract<bool>(std::meta::substitute(^^refuses_incomplete_class_v, arguments));
}

// One node of the walk: a bare type in a region.  kept is the reflection of
// the kept parts of a class, when the walk has it (TypePart), and may_read
// tells that a class is complete.
struct StoreNode {
    std::meta::info type{};
    std::meta::info kept{};
    StoreRegion region = StoreRegion::owned;
    bool may_read = false;
};

// The node of a type that the walk reaches other than by value: a class is
// complete or the walk cannot read it.
[[nodiscard]] consteval StoreNode indirect_node_(std::meta::info type, StoreRegion region) {
    std::meta::info const bare = ::foundation::reflect::bare_type(type);
    return StoreNode{bare, std::meta::info{}, region, is_class_or_union_(bare) && std::meta::is_complete_type(bare)};
}

// True for the node of a template argument that names no object that a value
// can hold: an incomplete class, such as a tag that a unit only declares.  A
// class holds no value of an incomplete class, and a pointer member that names
// one is a member that the walk reads.
[[nodiscard]] consteval bool holds_no_value_of_(StoreNode const& argument) {
    return is_class_or_union_(argument.type) && !argument.may_read;
}

// The visited list of the walk, and the next node of each region.  The list
// is one array, so the walk allocates one time for a small graph.
class StoreWalk {
public:
    consteval StoreWalk() noexcept = default;
    StoreWalk(StoreWalk const&) = delete("a walk owns its list, so a copy would free the list two times");
    StoreWalk& operator=(StoreWalk const&) = delete("a walk owns its list, so a copy would free the list two times");
    // A destructor cannot be consteval, and a walk exists only in a constant
    // evaluation, so the branch is the only path.
    constexpr ~StoreWalk() {
        if consteval {
            delete[] nodes_;
        }
    }

    // Adds the node to the list, unless the list holds its type in its region
    // or the type can hold no borrow slot (is_leaf_).  Complexity: linear in
    // the list.
    consteval void push(StoreNode const& node) {
        std::meta::info const type = node.type;
        if (is_leaf_(type)) return;
        StoreNode const* const nodes = nodes_;
        for (std::size_t index = 0; index < size_; ++index) {
            if (nodes[index].type == type && nodes[index].region == node.region
                && (nodes[index].may_read || !node.may_read)) {
                return;
            }
        }
        if (size_ == capacity_) grow_();
        nodes_[size_] = node;
        ++size_;
    }

    // True when a node waits in some region.
    [[nodiscard]] consteval bool has_next() {
        return waits_(StoreRegion::writable) || waits_(StoreRegion::owned) || waits_(StoreRegion::read_only);
    }

    // The next node: the first one of the writable region, so a refusal comes
    // early, then of the owned region, and then of the read_only region.
    [[nodiscard]] consteval StoreNode take() {
        StoreRegion region = StoreRegion::read_only;
        if (waits_(StoreRegion::writable)) {
            region = StoreRegion::writable;
        } else if (waits_(StoreRegion::owned)) {
            region = StoreRegion::owned;
        }
        return nodes_[next_[static_cast<std::size_t>(region)]++];
    }

private:
    // True when a node of the region waits, and next_ of the region is then
    // its place.
    [[nodiscard]] consteval bool waits_(StoreRegion region) {
        std::size_t& next = next_[static_cast<std::size_t>(region)];
        while (next < size_ && nodes_[next].region != region) {
            ++next;
        }
        return next < size_;
    }

    // Doubles the capacity, and moves each node into the new array.
    // Complexity: linear in the number of nodes.
    consteval void grow_() {
        std::size_t const capacity = capacity_ == 0 ? 32 : 2 * capacity_;
        StoreNode* const nodes = new StoreNode[capacity]{};
        for (std::size_t index = 0; index < size_; ++index)
            nodes[index] = nodes_[index];
        delete[] nodes_;
        nodes_ = nodes;
        capacity_ = capacity;
    }

    StoreNode* nodes_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t next_[3]{};
};

// Which borrow a slot must not keep: each borrow, or only a borrow of the
// bytes of an epoch (the gate of an epoch body).  An epoch gives borrows only
// of its own brand EpochOf<Tag>, which is no arena tag, and it builds no
// object of a class that states an arena brand.  erase_brand of
// foundation/core/Ref.h refuses each arena brand.  So the gate of an epoch
// reads two kinds of slot as no slot, with the readers of ArenaKeeps of
// foundation/core/BumpArena.h: a borrow of the base whose owner is an arena
// tag (owner_brand and is_arena_tag_ of foundation/core/TypeParts.h), and a
// stated address (is_stated_address_).  The walk still goes into the target
// of such a borrow.
enum class StoreFacts : unsigned char {
    each_borrow,
    epoch_borrow,
};

// Pushes the bases and the members of the class in the region: a member by
// reference in the region of its target, a mutable member of an object that
// the body does not own in the writable region, and each other part in the
// region of its class.  A pointer member of a class of the base that states
// refers_static{} names static storage, so it is no part of the walk.
consteval void push_members_(StoreWalk& walk, TypeParts const& parts, StoreRegion region) {
    PartRun const run = parts.run();
    for (std::size_t index = parts.member_start; index < parts.size; ++index) {
        TypePart const part = run[index];
        if (part.has(PartFact::is_plain)) continue;
        if (parts.refers_static && std::meta::is_pointer_type(part.type)) continue;
        if (part.has(PartFact::was_reference)) {
            walk.push(indirect_node_(part.type, target_region_(part)));
        } else {
            bool const is_written = part.has(PartFact::is_mutable) && region != StoreRegion::owned;
            walk.push(StoreNode{part.type, part.kept, is_written ? StoreRegion::writable : region, true});
        }
    }
}

// How a walk reads the borrow slots that it reaches in the writable region:
// the gate stops at the first one, and the check of a marked class lists the
// target of each one (the check of synchronized_by_proof).
enum class SlotRead : unsigned char {
    first,
    each,
};

// The plan of one walk.  exempt names a class that the walk counts as no
// part, or nothing: the check of a marked class counts the class itself as no
// part when a slot or a result reaches it again.  The gate trusts a class that
// states synchronized_by_proof{} after the check of the mark.  The walks of
// that check trust no such mark, so no check waits on a check.
struct SlotPlan {
    SlotRead read = SlotRead::first;
    std::meta::info exempt{};
    bool trusts_proof_mark = true;
    StoreFacts facts = StoreFacts::each_borrow;
};

// Runs the check of the mark synchronized_by_proof{} of the class one time
// for each translation unit (the check of synchronized_by_proof).
[[nodiscard]] consteval bool proves_synchronized_by_proof_(std::meta::info type);

// True when the walk of the nodes on the list reaches a borrow slot in the
// writable region.  A class type is complete or the walk refuses it.  The walk
// pushes the target of each pointer, borrow and borrow of a frame into its own
// list, so a type that reaches itself, such as a node with a pointer to its
// parent, ends with the visited list and asks no memo in the middle of its own
// walk.  A plan that reads each slot puts the target of each one on the list
// of targets and goes on into the target: the type that a pointer names, the
// target of a borrow (borrow_target), and a null type, which is erased, for a
// borrow with no target and for a class whose state the walk cannot read.
[[nodiscard]] consteval bool drain_slots_(StoreWalk& walk, SlotPlan const& plan,
                                          ::foundation::reflect::TypeNodeList& targets) {
    constexpr StoreRegion read_only = StoreRegion::read_only;
    constexpr StoreRegion writable = StoreRegion::writable;
    bool has_slot = false;
    while (walk.has_next()) {
        StoreNode const node = walk.take();
        std::meta::info const type = node.type;
        StoreRegion const region = node.region;
        if (plan.exempt != std::meta::info{} && type == plan.exempt) continue;
        if (std::meta::is_pointer_type(type)) {
            std::meta::info const pointee = std::meta::remove_pointer(type);
            if (std::meta::is_function_type(pointee)) continue;
            if (region == writable) {
                if (plan.read == SlotRead::first) return true;
                has_slot = true;
                targets.push_back(::foundation::reflect::TypeNode{::foundation::reflect::bare_type(pointee)});
            }
            walk.push(indirect_node_(pointee, std::meta::is_const_type(pointee) ? read_only : writable));
            continue;
        }
        if (std::meta::is_array_type(type)) {
            walk.push(indirect_node_(std::meta::remove_all_extents(type), region));
            continue;
        }
        if (!is_class_or_union_(type)) continue;
        if (!node.may_read) return refuse_incomplete_capture_(type);
        if (node.kept == std::meta::info{} && is_plain_class_(type)) continue;
        TypeParts const& parts =
            std::meta::extract<TypeParts const&>(node.kept == std::meta::info{} ? kept_parts_of(type) : node.kept);
        // A refused check stops the build, so the walk reads no part of the
        // class either way, and the build gives one error.
        if (plan.trusts_proof_mark && parts.states_synchronized_by_proof) {
            static_cast<void>(proves_synchronized_by_proof_(type));
            continue;
        }
        if (parts.is_unreadable) {
            if (plan.read == SlotRead::first) return true;
            has_slot = true;
            targets.push_back(::foundation::reflect::TypeNode{});
            continue;
        }
        PartRun const run = parts.run();
        if (parts.is_frame_borrow) {
            for (std::size_t index = 0; index < parts.argument_count; ++index) {
                TypePart const argument = run[index];
                StoreNode const target = indirect_node_(argument.type, target_region_(argument));
                if (!argument.has(PartFact::is_scope_brand) && !holds_no_value_of_(target)) walk.push(target);
            }
            continue;
        }
        bool const is_epoch_gate = plan.facts == StoreFacts::epoch_borrow;
        if (parts.is_borrow) {
            bool const holds_no_epoch_borrow = is_epoch_gate && is_arena_tag_(parts.owner_brand);
            if (region == writable && !holds_no_epoch_borrow) {
                if (plan.read == SlotRead::first) return true;
                has_slot = true;
                targets.push_back(::foundation::reflect::TypeNode{parts.borrow_target});
            }
            bool const gives_const = region == read_only && parts.gives_const_access;
            bool const is_read = gives_const || parts.is_borrow_target_const;
            if (parts.borrow_target != std::meta::info{}) {
                walk.push(indirect_node_(parts.borrow_target, is_read ? read_only : writable));
            }
            continue;
        }
        if (parts.keeps_own_borrows || (is_epoch_gate && is_stated_address_(parts))) continue;
        if (!parts.is_storage_owner) push_members_(walk, parts, region);
        if (!parts.is_storage_owner && !parts.holds_bytes) continue;
        for (std::size_t index = 0; index < parts.argument_count; ++index) {
            StoreNode const argument = indirect_node_(run[index].type, region);
            if (!holds_no_value_of_(argument)) walk.push(argument);
        }
    }
    return has_slot;
}

// True when the walk from the start type in the start region reaches a
// borrow slot in the writable region (the gate).  The start type is bare.
[[nodiscard]] consteval bool reaches_slot_from_(std::meta::info start_type, StoreRegion start, StoreFacts facts) {
    StoreWalk walk;
    walk.push(indirect_node_(start_type, start));
    ::foundation::reflect::TypeNodeList unused;
    return drain_slots_(walk, SlotPlan{.facts = facts}, unused);
}

// Check of synchronized_by_proof
//   A class that states synchronized_by_proof{} of foundation/core/TypeParts.h
//   keeps one invariant: no borrow slot of an object of the class holds a
//   borrow of storage that a caller gave.  So a body that captures such an
//   object by reference cannot keep a borrow of its frame in it, and the gate
//   reads the class as no borrow slot, also outside the base.  Four facts keep
//   the invariant, and the check reads each one time for each class:
//     (a) the class states synchronized{} of foundation/Transfer.h, and the
//         check of that statement runs: two threads write an object of the
//         class only through a proof
//     (b) the store surface: the walk lists the target of each borrow slot
//         that the members reach in the writable region, and no parameter of
//         a door gives a target that meets one of them.  So no door can put a
//         borrow of the storage of its caller into a slot.  Two targets meet
//         when they are one type, when one is a base of the other (a door can
//         cast a base down), or when one is erased
//     (c) each data member of the class and of each base is private, so only
//         a door of the class writes an object of it.  Reflection gives no
//         friend, and the plugin rule proof-routes refuses each friend
//         function of a class that states synchronized{} and each friend
//         class that can reach it
//     (d) no result of a door reaches a borrow slot in the writable region,
//         so no caller writes a slot through a result.  The class counts as
//         no slot in a result, so a door can give the object itself back
//   The doors are the member functions and the member function templates that
//   are not private, also the const ones, because a const door writes a
//   mutable member and each target behind a pointer: of the class, of each
//   base, and of each nested class at each depth.  The constructors that are
//   not private are doors too, and so are the implicit copy and move, which
//   take a second object of the class.  A parameter that proves the owner
//   (is_proof_parameter of foundation/Transfer.h) gives no target.
//
//   A parameter gives storage of its caller: the object that a reference or a
//   pointer names, with each part of it and each object behind its borrows,
//   and of a parameter by value each object behind its borrows.  An object of
//   the class that a parameter gives is a second object, so the walk reads its
//   parts too, and the copy of a class whose slots name its own parts refuses.
//
//   An erased target meets each target, so the check fails closed: void, a
//   character type, std::byte, a class that is incomplete or whose state the
//   walk cannot read, and a probe type of foundation/Transfer.h, which stands
//   for each type that a door template can take.  So a class with a borrow
//   slot and a door template that takes a body cannot take the mark.
//
//   A fact that fails stops the build with a static assertion that names the
//   class and the door or the part.  Complexity: linear in the members and the
//   doors of the class, times the parameters of each door and the parts that
//   each parameter reaches, at compile time only.

// The room of the walk of the storage that one parameter gives.  A walk that
// needs more adds an erased target, so the check fails closed.
inline constexpr std::size_t given_walk_room = 512;

// True for a probe type of the door check of foundation/Transfer.h, which
// stands for each type that a door template can take.
[[nodiscard]] consteval bool is_probe_type_(std::meta::info bare) {
    namespace tr = ::foundation::transfer;
    return bare == ^^tr::detail::door_probe_body || bare == ^^tr::detail::door_probe_brand;
}

// True for a class whose parts the check cannot read: an incomplete class,
// and a class whose state reflection does not show.
[[nodiscard]] consteval bool hides_its_parts_(std::meta::info bare) {
    if (!is_class_or_union_(bare)) return false;
    if (!std::meta::is_complete_type(bare)) return true;
    return ::foundation::reflect::holds_unreadable_state(::foundation::reflect::TypeNode{bare, true});
}

// True for a target that meets each target: a null type, void, a character
// type, std::byte, a probe type, and a class whose parts the check cannot
// read.
[[nodiscard]] consteval bool is_erased_target_(std::meta::info type) {
    if (type == std::meta::info{}) return true;
    std::meta::info const bare = ::foundation::reflect::bare_type(type);
    if (std::meta::is_void_type(bare) || bare == ^^char || bare == ^^signed char || bare == ^^unsigned char
        || bare == ^^std::byte) {
        return true;
    }
    return is_probe_type_(bare) || hides_its_parts_(bare);
}

// True when the two targets meet: one is erased, they are one type, or one
// class is a base of the other.
[[nodiscard]] consteval bool targets_meet_(std::meta::info given, std::meta::info kept) {
    if (is_erased_target_(given) || is_erased_target_(kept)) return true;
    std::meta::info const left = ::foundation::reflect::bare_type(given);
    std::meta::info const right = ::foundation::reflect::bare_type(kept);
    if (left == right) return true;
    if (!is_class_or_union_(left) || !is_class_or_union_(right)) return false;
    return std::meta::is_base_of_type(left, right) || std::meta::is_base_of_type(right, left);
}

// True when a target of the first list meets a target of the second.
// Complexity: the product of the two sizes.
[[nodiscard]] consteval bool lists_meet_(::foundation::reflect::TypeNodeList const& given,
                                         ::foundation::reflect::TypeNodeList const& kept) {
    for (::foundation::reflect::TypeNode const& left : given) {
        for (::foundation::reflect::TypeNode const& right : kept) {
            if (targets_meet_(left.type, right.type)) return true;
        }
    }
    return false;
}

// True for a class of the base that states refers_static{} of
// foundation/Transfer.h, whose pointers name static storage and no storage of
// a caller.  TypeParts read the mark, and they ran its check.
[[nodiscard]] consteval bool names_static_storage_(std::meta::info bare) {
    return std::meta::extract<TypeParts const&>(kept_held_parts_of(bare)).refers_static;
}

// Pushes the node of the type into the walk of the storage that a parameter
// gives.  The writable region holds storage of the caller, and the owned
// region holds the copy that the door takes by value.  A type in the writable
// region is a target, so it goes on the list too, unless the list holds it:
// also a scalar, which the walk itself does not keep.  Complexity: linear in
// the list.
consteval void push_given_(StoreWalk& walk, ::foundation::reflect::TypeNodeList& given, std::meta::info type,
                           StoreRegion region) {
    std::meta::info const bare = ::foundation::reflect::bare_type(type);
    if (region == StoreRegion::writable) {
        bool is_listed = false;
        for (::foundation::reflect::TypeNode const& listed : given) {
            is_listed = is_listed || listed.type == bare;
        }
        if (!is_listed) given.push_back(::foundation::reflect::TypeNode{bare});
    }
    walk.push(
        StoreNode{bare, std::meta::info{}, region, is_class_or_union_(bare) && std::meta::is_complete_type(bare)});
}

// The targets of the storage of its caller that a parameter of the declared
// type gives (the check of synchronized_by_proof).  A reference gives its
// object, a pointer gives the object that it names, and each part of such an
// object and each object behind a borrow of it are storage of the caller too.
// A value gives each object behind a borrow in it.  A borrow names its target
// (borrow_target_of of foundation/Transfer.h), each type argument, and its
// pointers too.  A class of
// the base that states refers_static{} names static storage with its pointers.
// A probe type and a class whose parts the walk cannot read give an erased
// target in each region.  Complexity: linear in the parts that the parameter
// reaches, to given_walk_room parts.
[[nodiscard]] consteval ::foundation::reflect::TypeNodeList given_targets_(std::meta::info declared) {
    namespace refl = ::foundation::reflect;
    namespace tr = ::foundation::transfer;
    constexpr StoreRegion writable = StoreRegion::writable;
    refl::TypeNodeList given;
    StoreWalk walk;
    std::meta::info const type = std::meta::dealias(declared);
    if (std::meta::is_reference_type(type)) {
        push_given_(walk, given, std::meta::remove_reference(type), writable);
    } else {
        push_given_(walk, given, type, StoreRegion::owned);
    }
    std::size_t read = 0;
    while (walk.has_next()) {
        StoreNode const node = walk.take();
        std::meta::info const bare = node.type;
        if (++read > given_walk_room || is_probe_type_(bare) || hides_its_parts_(bare)) {
            given.push_back(refl::TypeNode{});
            if (read > given_walk_room) break;
            continue;
        }
        if (std::meta::is_pointer_type(bare)) {
            std::meta::info const pointee = std::meta::remove_pointer(bare);
            if (!std::meta::is_function_type(pointee)) push_given_(walk, given, pointee, writable);
            continue;
        }
        if (std::meta::is_array_type(bare)) {
            push_given_(walk, given, std::meta::remove_all_extents(bare), node.region);
            continue;
        }
        if (!is_class_or_union_(bare)) continue;
        bool const is_borrow = tr::detail::states(bare, ^^tr::refers) || tr::detail::states(bare, ^^tr::refers_unique)
                            || tr::detail::states(bare, ^^tr::refers_deep);
        bool const is_static = names_static_storage_(bare);
        for (refl::TypeNode const& argument : refl::argument_components_of(refl::TypeNode{bare, true})) {
            if (argument.is_value_argument) continue;
            push_given_(walk, given, argument.type, is_borrow ? writable : node.region);
        }
        if (is_borrow) {
            tr::detail::BorrowTarget const target = tr::detail::borrow_target_of(bare);
            if (target.is_found) push_given_(walk, given, target.node.type, writable);
        }
        for (refl::TypeNode const& part : refl::member_components_of(refl::TypeNode{bare, true})) {
            if (is_static && std::meta::is_pointer_type(part.type)) continue;
            push_given_(walk, given, part.type, part.was_reference ? writable : node.region);
        }
    }
    return given;
}

// The target of each borrow slot that the members of the owner reach in the
// writable region.  The walk reads the held parts of the owner, because a
// storage owner of the base hides its members from its kept parts, and it
// counts the owner as no part when a slot reaches it again.
[[nodiscard]] consteval ::foundation::reflect::TypeNodeList slot_targets_of_(std::meta::info owner) {
    StoreWalk walk;
    push_members_(walk, std::meta::extract<TypeParts const&>(kept_held_parts_of(owner)), StoreRegion::writable);
    ::foundation::reflect::TypeNodeList targets;
    static_cast<void>(drain_slots_(walk, SlotPlan{SlotRead::each, owner, false}, targets));
    return targets;
}

// True when the result of a door reaches a borrow slot in the writable region:
// a result by value from the owned region, and a result by reference from
// the region of its target.  The owner counts as no slot.
[[nodiscard]] consteval bool result_reaches_slot_(std::meta::info result, std::meta::info owner) {
    std::meta::info const declared = std::meta::dealias(result);
    std::meta::info const bare = ::foundation::reflect::bare_type(declared);
    if (std::meta::is_void_type(bare)) return false;
    StoreRegion region = StoreRegion::owned;
    if (std::meta::is_reference_type(declared)) {
        region = std::meta::is_const_type(std::meta::remove_reference(declared)) ? StoreRegion::read_only
                                                                                 : StoreRegion::writable;
    }
    StoreWalk walk;
    walk.push(indirect_node_(bare, region));
    ::foundation::reflect::TypeNodeList unused;
    return drain_slots_(walk, SlotPlan{SlotRead::first, owner, false}, unused);
}

// The refusals of the check of synchronized_by_proof.  The check reads a
// specialization of one of the variables below, which stops the build.  The
// first message gives the fact, and the next ones name the class and the door
// or the part.
template <std::meta::info Class>
consteval bool refuse_unsynchronized_proof_mark_() {
    static_assert(Class == std::meta::info{},
                  "the store check: a class states synchronized_by_proof{} of foundation/core/TypeParts.h, and it "
                  "states no synchronized{} of foundation/Transfer.h.  The mark asks the check of synchronized{}, so "
                  "two threads write an object of the class only through a proof.  The next message names the class");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    return false;
}

template <std::meta::info Class>
inline constexpr bool unsynchronized_proof_mark_refusal_ = refuse_unsynchronized_proof_mark_<Class>();

template <std::meta::info Class, std::meta::info Member>
consteval bool refuse_open_member_of_proof_mark_() {
    static_assert(Class == std::meta::info{},
                  "the store check: a class states synchronized_by_proof{} of foundation/core/TypeParts.h, and a "
                  "data member of the class or of a base is not private.  The mark asks that only a door of the class "
                  "writes an object of it.  The next two messages name the class and the member");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Member));
    return false;
}

template <std::meta::info Class, std::meta::info Member>
inline constexpr bool open_member_of_proof_mark_refusal_ = refuse_open_member_of_proof_mark_<Class, Member>();

template <std::meta::info Class, std::meta::info Door, std::meta::info Parameter>
consteval bool refuse_given_borrow_of_proof_mark_() {
    static_assert(Class == std::meta::info{},
                  "the store check: a class states synchronized_by_proof{} of foundation/core/TypeParts.h, and a door "
                  "of the class takes a parameter that gives storage of its caller whose type meets the target of a "
                  "borrow slot of a member, so the door can keep a borrow of that storage in the object.  Two targets "
                  "meet when they are one type, when one is a base of the other, or when one is erased: void, a "
                  "character type, std::byte, an incomplete or unreadable class, or a probe type of a door template, "
                  "such as a body.  The next three messages name the class, the door and the parameter");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Door));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Parameter));
    return false;
}

template <std::meta::info Class, std::meta::info Door, std::meta::info Parameter>
inline constexpr bool given_borrow_of_proof_mark_refusal_ =
    refuse_given_borrow_of_proof_mark_<Class, Door, Parameter>();

template <std::meta::info Class, std::meta::info Door>
consteval bool refuse_slot_in_result_of_proof_mark_() {
    static_assert(Class == std::meta::info{},
                  "the store check: a class states synchronized_by_proof{} of foundation/core/TypeParts.h, and a door "
                  "of the class gives a result that reaches a borrow slot in the writable region, so a caller can "
                  "write a borrow of its frame through the result.  The next two messages name the class and the "
                  "door");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Door));
    return false;
}

template <std::meta::info Class, std::meta::info Door>
inline constexpr bool slot_in_result_of_proof_mark_refusal_ = refuse_slot_in_result_of_proof_mark_<Class, Door>();

template <std::meta::info Class, std::meta::info Door>
consteval bool refuse_unread_door_of_proof_mark_() {
    static_assert(Class == std::meta::info{},
                  "the store check: a class states synchronized_by_proof{} of foundation/core/TypeParts.h, and the "
                  "check cannot read one of its doors: a door template or a nested class template that no probe of "
                  "foundation/Transfer.h fills, a nested class that is incomplete, or a nested class deeper than the "
                  "walk reads.  The next two messages name the class and the door");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Door));
    return false;
}

template <std::meta::info Class, std::meta::info Door>
inline constexpr bool unread_door_of_proof_mark_refusal_ = refuse_unread_door_of_proof_mark_<Class, Door>();

template <std::meta::info Class>
consteval bool refuse_copy_of_proof_mark_() {
    static_assert(Class == std::meta::info{},
                  "the store check: a class states synchronized_by_proof{} of foundation/core/TypeParts.h, and the "
                  "class copies or moves.  A copy reads a second object of the class, whose parts meet the target of a "
                  "borrow slot of a member, so the copy can keep a borrow of storage of its caller.  The next message "
                  "names the class");
    static_assert(Class == std::meta::info{}, std::meta::display_string_of(Class));
    return false;
}

template <std::meta::info Class>
inline constexpr bool copy_of_proof_mark_refusal_ = refuse_copy_of_proof_mark_<Class>();

// Each function below reads a refusal variable of the check for the class,
// and for a door or a part of it, which stops the build.  Each gives false.
[[nodiscard]] consteval bool refuse_class_of_proof_mark_(std::meta::info refusal, std::meta::info class_type) {
    std::meta::info const arguments[] = {std::meta::reflect_constant(class_type)};
    static_cast<void>(std::meta::extract<bool>(std::meta::substitute(refusal, arguments)));
    return false;
}

[[nodiscard]] consteval bool refuse_part_of_proof_mark_(std::meta::info refusal, std::meta::info class_type,
                                                        std::meta::info part) {
    std::meta::info const arguments[] = {std::meta::reflect_constant(class_type), std::meta::reflect_constant(part)};
    static_cast<void>(std::meta::extract<bool>(std::meta::substitute(refusal, arguments)));
    return false;
}

[[nodiscard]] consteval bool refuse_parameter_of_proof_mark_(std::meta::info class_type, std::meta::info door,
                                                             std::meta::info parameter) {
    std::meta::info const arguments[] = {std::meta::reflect_constant(class_type), std::meta::reflect_constant(door),
                                         std::meta::reflect_constant(parameter)};
    static_cast<void>(
        std::meta::extract<bool>(std::meta::substitute(^^given_borrow_of_proof_mark_refusal_, arguments)));
    return false;
}

// True when each data member of the class and of each base is private (fact
// c).  Else it refuses the first open member.  Complexity: linear in the
// members of the class and of its bases.
[[nodiscard]] consteval bool keeps_parts_private_(std::meta::info type, std::meta::info owner) {
    namespace refl = ::foundation::reflect;
    for (refl::TypeNode const& part : refl::member_components_of(refl::TypeNode{type, true})) {
        if (part.member == std::meta::info{}) {
            if (!keeps_parts_private_(part.type, owner)) return false;
            continue;
        }
        if (!std::meta::is_private(part.member)) {
            return refuse_part_of_proof_mark_(^^open_member_of_proof_mark_refusal_, owner, part.member);
        }
    }
    return true;
}

// True when the member is a door of the check: a member function or a member
// function template that is not private, also a constructor, that is no
// destructor and not deleted.
[[nodiscard]] consteval bool is_door_of_proof_mark_(std::meta::info member) {
    bool const is_template = std::meta::is_function_template(member) || std::meta::is_constructor_template(member);
    if (!std::meta::is_function(member) && !is_template) return false;
    if (std::meta::is_private(member)) return false;
    if (is_template) return true;
    return !std::meta::is_destructor(member) && !std::meta::is_deleted(member);
}

// True when the door holds facts b and d (the check of synchronized_by_proof):
// no parameter that proves no owner gives a target that meets a slot, and no
// result reaches a slot.  A door template reads as its specialization for the
// probes of foundation/Transfer.h, and a template that no probe fills
// refuses.  Else it refuses the door.
[[nodiscard]] consteval bool holds_door_of_proof_mark_(std::meta::info door, std::meta::info owner,
                                                       ::foundation::reflect::TypeNodeList const& slots) {
    namespace refl = ::foundation::reflect;
    namespace tr = ::foundation::transfer;
    std::meta::info function = door;
    if (std::meta::is_function_template(door) || std::meta::is_constructor_template(door)) {
        function = tr::detail::specialize_door(door, ^^tr::door_probe_contexts);
        if (function == std::meta::info{}) {
            return refuse_part_of_proof_mark_(^^unread_door_of_proof_mark_refusal_, owner, door);
        }
    }
    if (std::meta::is_deleted(function) || std::meta::is_destructor(function)) return true;
    std::meta::info const given = refl::find_parameter(function, [owner, &slots](std::meta::info parameter) {
        if (tr::detail::is_proof_parameter(refl::node_reached_indirectly(std::meta::type_of(parameter)), owner)) {
            return false;
        }
        return lists_meet_(given_targets_(std::meta::type_of(parameter)), slots);
    });
    if (given != std::meta::info{}) return refuse_parameter_of_proof_mark_(owner, door, given);
    if (std::meta::is_constructor(function)) return true;
    if (result_reaches_slot_(std::meta::return_type_of(function), owner)) {
        return refuse_part_of_proof_mark_(^^slot_in_result_of_proof_mark_refusal_, owner, door);
    }
    return true;
}

// True when each door of the scope, of each of its bases and of each nested
// class at each depth holds facts b and d.  A nested class template reads as
// its instance for a probe of foundation/Transfer.h.  A nested class that the
// check cannot read, or that is deeper than depth levels, refuses.
// Complexity: linear in the members of each scope, times the parameters of
// each door.
[[nodiscard]] consteval bool holds_scope_of_proof_mark_(std::meta::info scope, std::meta::info owner,
                                                        ::foundation::reflect::TypeNodeList const& slots, int depth) {
    namespace refl = ::foundation::reflect;
    namespace tr = ::foundation::transfer;
    std::meta::access_context const unchecked = std::meta::access_context::unchecked();
    bool holds = true;
    static_cast<void>(refl::find_member(scope, unchecked, [owner, &slots, &holds](std::meta::info member) {
        if (!is_door_of_proof_mark_(member)) return false;
        holds = holds_door_of_proof_mark_(member, owner, slots);
        return !holds;
    }));
    if (!holds) return false;
    for (refl::TypeNode const& part : refl::member_components_of(refl::TypeNode{scope, true})) {
        if (part.member != std::meta::info{}) continue;
        if (!holds_scope_of_proof_mark_(part.type, owner, slots, depth)) return false;
    }
    static_cast<void>(
        refl::find_member(scope, unchecked, [scope, owner, &slots, depth, &holds](std::meta::info member) {
            bool const is_template = std::meta::is_class_template(member);
            if ((!std::meta::is_type(member) && !is_template) || std::meta::is_type_alias(member)
                || std::meta::parent_of(member) != scope) {
                return false;
            }
            std::meta::info nested = member;
            if (is_template) nested = tr::detail::specialize_nested_template(member, ^^tr::door_probe_contexts);
            if (nested == owner || nested == scope || (nested != std::meta::info{} && !is_class_or_union_(nested))) {
                return false;
            }
            if (nested == std::meta::info{} || !std::meta::is_complete_type(nested) || depth <= 0) {
                holds = refuse_part_of_proof_mark_(^^unread_door_of_proof_mark_refusal_, owner, member);
                return true;
            }
            holds = holds_scope_of_proof_mark_(nested, owner, slots, depth - 1);
            return !holds;
        }));
    return holds;
}

// Gives true when the class holds each fact of the mark, and else stops the
// build (the check of synchronized_by_proof).
[[nodiscard]] consteval bool check_synchronized_by_proof_(std::meta::info owner) {
    namespace tr = ::foundation::transfer;
    if (!tr::detail::states(owner, ^^tr::synchronized)) {
        return refuse_class_of_proof_mark_(^^unsynchronized_proof_mark_refusal_, owner);
    }
    tr::detail::check_synchronized_statement(owner);
    if (!keeps_parts_private_(owner, owner)) return false;
    ::foundation::reflect::TypeNodeList const slots = slot_targets_of_(owner);
    if (!holds_scope_of_proof_mark_(owner, owner, slots, tr::detail::closed_walk_depth)) return false;
    bool const copies = std::meta::is_copy_constructible_type(owner) || std::meta::is_move_constructible_type(owner)
                     || std::meta::is_copy_assignable_type(owner) || std::meta::is_move_assignable_type(owner);
    if (copies && lists_meet_(given_targets_(std::meta::add_lvalue_reference(owner)), slots)) {
        return refuse_class_of_proof_mark_(^^copy_of_proof_mark_refusal_, owner);
    }
    return true;
}

// The check of the mark of each class, one time for each translation unit.
template <class Class>
inline constexpr bool synchronized_by_proof_check_v = check_synchronized_by_proof_(^^Class);

[[nodiscard]] consteval bool proves_synchronized_by_proof_(std::meta::info type) {
    std::meta::info const arguments[] = {type};
    return std::meta::extract<bool>(std::meta::substitute(^^synchronized_by_proof_check_v, arguments));
}
//   The verdict of one type in one region does not depend on the body that
//   reaches it, so each translation unit computes it one time, in an
//   instance of this variable template.  The bodies of one class capture
//   this, a context or an owner of the class again and again, and each such
//   capture then costs one lookup.
template <class Type, StoreRegion Region, StoreFacts Facts>
inline constexpr bool reaches_slot_from_v = reaches_slot_from_(^^Type, Region, Facts);

// The class that a pointer to a type converts to when the walk from the type
// in the region reaches no borrow slot in the writable region, and else not.
template <StoreRegion Region, StoreFacts Facts>
struct NoSlotTest final {
    template <class Type>
        requires(!reaches_slot_from_v<Type, Region, Facts>)
    consteval NoSlotTest(Type*) noexcept {}
};

// The verdict of the type in the region, from the memo.  The read is the read
// of is_plain_class_ of foundation/core/TypeParts.h, with the constraint of
// NoSlotTest, so a type with no verdict refuses the body.
[[nodiscard]] consteval bool reaches_slot_(std::meta::info type, StoreRegion region, StoreFacts facts) {
    constexpr StoreFacts each = StoreFacts::each_borrow;
    constexpr StoreFacts epoch = StoreFacts::epoch_borrow;
    std::meta::info const each_tests[] = {^^NoSlotTest<StoreRegion::owned, each>,
                                          ^^NoSlotTest<StoreRegion::read_only, each>,
                                          ^^NoSlotTest<StoreRegion::writable, each>};
    std::meta::info const epoch_tests[] = {^^NoSlotTest<StoreRegion::owned, epoch>,
                                           ^^NoSlotTest<StoreRegion::read_only, epoch>,
                                           ^^NoSlotTest<StoreRegion::writable, epoch>};
    std::size_t const place = static_cast<std::size_t>(region);
    std::meta::info const test = facts == epoch ? epoch_tests[place] : each_tests[place];
    return !std::meta::is_convertible_type(std::meta::add_pointer(type), test);
}

// True when the walk from one capture or one base of the class of a body
// reaches a borrow slot in the writable region: a capture by reference in the
// region of its target, and each other one owned.  Most captures name a scalar
// or a plain class, which holds a borrow slot in no region.
[[nodiscard]] consteval bool stores_borrow_in_(std::meta::info declared, StoreFacts facts = StoreFacts::each_borrow) {
    std::meta::info const type = ::foundation::reflect::bare_type(declared);
    if (is_leaf_(type)) return false;
    if (is_class_or_union_(type) && std::meta::is_complete_type(type) && is_plain_class_(type)) return false;
    StoreRegion region = StoreRegion::owned;
    if (std::meta::is_reference_type(declared)) {
        bool const is_const = std::meta::is_const_type(std::meta::remove_reference(declared));
        region = is_const ? StoreRegion::read_only : StoreRegion::writable;
    }
    return reaches_slot_(type, region, facts);
}

// True when the walk from the class of a body reaches a borrow slot in the
// writable region.  body is the class of the body with no reference.  The
// class of a closure is unique to its lambda, so the walk reads its captures
// here and asks the memo for each of them.  A named class is the start of the
// memo itself.
[[nodiscard]] consteval bool stores_borrow_outside_(std::meta::info body, BodyForm form,
                                                    StoreFacts facts = StoreFacts::each_borrow) {
    std::meta::info const root = ::foundation::reflect::bare_type(body);
    if (!std::meta::is_class_type(root)) return false;
    bool const is_named_class = form == BodyForm::named && std::meta::has_identifier(root)
                             && !std::meta::is_const_type(std::meta::dealias(body));
    if (is_named_class) return reaches_slot_(root, StoreRegion::writable, facts);
    std::meta::access_context const unchecked = std::meta::access_context::unchecked();
    std::vector<std::meta::info> const bases = std::meta::bases_of(root, unchecked);
    std::vector<std::meta::info> const members = std::meta::nonstatic_data_members_of(root, unchecked);
    std::size_t const base_count = bases.size();
    std::size_t const member_count = members.size();
    bool const shows_no_state = base_count == 0 && member_count == 0;
    if (shows_no_state && !std::meta::has_template_arguments(root) && !std::meta::is_empty_type(root)) return true;
    for (std::size_t index = 0; index < base_count; ++index) {
        if (stores_borrow_in_(std::meta::type_of(bases[index]), facts)) return true;
    }
    for (std::size_t index = 0; index < member_count; ++index) {
        if (stores_borrow_in_(std::meta::type_of(members[index]), facts)) return true;
    }
    return false;
}

}  // namespace detail

// A body of a frame door that stores no borrow outside the frame (the walk):
// no capture reaches a borrow slot that the caller can read after the call.
// Body is the type that the door deduces for its forwarding parameter, so an
// lvalue reference names a body that the caller holds.
template <class Body>
concept StoresNoBorrowOutside =
    !detail::stores_borrow_outside_(^^std::remove_reference_t<Body>, std::is_lvalue_reference_v<Body>
                                                                         ? detail::BodyForm::named
                                                                         : detail::BodyForm::unnamed);

// A body of an epoch that stores no borrow of the bytes of the epoch outside
// the epoch: the walk of StoresNoBorrowOutside, which reads a borrow of the
// base whose owner is an arena tag, and a stated address, as no slot
// (StoreFacts above).  So a body can fill an object of an arena or a plan
// that it captures by reference, and a View of a run of the epoch in a
// captured place is a refusal.  ArenaKeeps of foundation/core/BumpArena.h
// reads the same two facts with the same readers.  Only with_epoch of an
// EpochArena asks it.
template <class Body>
concept StoresNoEpochBorrowOutside = !detail::stores_borrow_outside_(
    ^^std::remove_reference_t<Body>,
    std::is_lvalue_reference_v<Body> ? detail::BodyForm::named : detail::BodyForm::unnamed,
    detail::StoreFacts::epoch_borrow);

// The arms of a frame door of a sum, each of which stores no borrow outside
// the frame.
template <class... Arms>
concept ArmsStoreNoBorrowOutside = (StoresNoBorrowOutside<Arms> && ...);

// The reason of the refused twin of a frame door whose body stores a borrow
// outside the frame.
#define CRUCIBLE_STORING_BODY_REFUSAL                                                                            \
    "a body of a frame door stores no borrow outside the frame: capture a View or a Ref by value or by a const " \
    "reference, and capture only the part of an object that the body writes (StoresNoBorrowOutside of "          \
    "foundation/core/CaptureStore.h)"

}  // namespace foundation::core
