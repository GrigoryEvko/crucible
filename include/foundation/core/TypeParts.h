#pragma once

// The parts of a class, read one time in each translation unit.
//
// A walk of a frame door reads each class that a body reaches: the store check
// of foundation/core/CaptureStore.h and the walks of the loan keys of
// foundation/core/KeyWalk.h read the template arguments, the bases, the members
// and the marks of each one.  A query of reflection that gives a list costs
// about 0.4 M compile instructions for each element, so the read of one class
// costs some millions, and the bodies of one unit reach the same classes again
// and again.  TypeParts holds what a walk reads of one class, and type_parts_v
// keeps it.
//
//   type_parts_v<Class>    the parts of a complete class or union
//   kept_parts_of(type)    the reflection of that variable for a type
//   held_parts_v<Class>    the bases and the data members alone, also of a
//                          borrow and of an owner
//   kept_held_parts_of(type)
//                          the reflection of that variable for a type
//   parts.run()            the parts in their order: each type argument, then
//                          the type of each value argument, then each base,
//                          and then each data member.  A borrow and an owner
//                          give their arguments alone
//   is_plain_class_(type)  true for a plain class or union, with a read that
//                          costs less than a read of the parts
//
// Kept answer
//   The answer does not depend on the body, the brand or the region of a walk,
//   so one variable of each class serves each of them.  A part names its type,
//   and a walk asks for itself if a type that it reaches other than by value
//   is complete, so no kept answer holds the state of a class that a unit
//   completes later.  A type argument that is incomplete at the first read is
//   no scope brand (is_scope_brand_).  The evaluation of a variable has an
//   operation budget of its own, so the read of a class costs the budget of no
//   walk.  The kept variables are templates, and the rule specialization-home
//   refuses a specialization of one outside this header.
//
// Parts that a walk does not read
//   An arithmetic type, an enumeration, void and the null pointer hold no
//   borrow, so no part that a class holds by value names one (is_leaf_).  A
//   reference to one is a part, because an object with such a member borrows
//   its target.  A part that the class holds by value and that is plain holds a
//   borrow at no depth.  A plain class gives each object of it only values: no
//   pointer to an object, no reference, no mark of a borrow or of an owner, and
//   no raw storage that a type argument can fill.  A class holds each part by
//   value, so the read of a part that it holds by value ends with the depth of
//   the class.

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/LoanKeyTrait.h>
#include <foundation/core/Owner.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <meta>

namespace foundation::core {

// The mark of a handle of the base whose doors store an object only through the
// gate ArenaKeeps of foundation/core/BumpArena.h, such as the Epoch of an
// EpochArena.  An object that such a door stores holds borrows only of the
// brand of the handle, and no door of the handle stores a borrow in a member of
// it.  So a body of a frame that captures the handle by a reference that is not
// const stores no borrow of the frame into it, and the store check of
// foundation/core/CaptureStore.h reads the handle as no borrow slot and reads no
// member of it.  The check trusts the mark only in a class of the base, and the
// check file of each marked class pins its doors.
struct keeps_own_borrows final {};

// The mark of a class that states synchronized{} of foundation/Transfer.h and
// whose doors keep no borrow that a caller gives them.  The store check of
// foundation/core/CaptureStore.h reads a class with the mark as no borrow
// slot, also outside the base, because it checks each fact behind the mark
// one time for each class before it trusts it (synchronized_by_proof_check_v
// of foundation/core/CaptureStore.h).  A class that states the mark and fails
// the check stops the build.  The check reads a door template through the
// probe body of foundation/Transfer.h, which stands for each type that a
// caller can give.  So a class with a borrow slot and a door template that
// takes a body cannot take the mark (the fixture neg_core_proof_mark_body_door).
struct synchronized_by_proof final {};

// The mark of a class template of the base that holds a value of its first
// type argument and nothing else that borrows, such as Tagged of
// fixy/Tagged.h.  When that argument is a void*, the class holds an address
// that no reader follows: each raw pointer outside the base is a finding of
// the quarantine, so no code outside the base follows the address.
// ArenaKeeps of foundation/core/BumpArena.h and the store check of an epoch
// body of foundation/core/CaptureStore.h read such a class as no borrow
// (is_stated_address_ below).  A walk trusts the mark only in the base, and
// each other void* stays a borrow with no type, as in a CallRef or an
// Option<void*>.
struct stated_address final {};

}  // namespace foundation::core

namespace foundation::core::detail {

// The mark of a borrow of the base whose second type argument is the brand of
// the owner of its target, such as a Ref, a Link, an ArenaRun, a FrameLink
// and the first element of a run of a set of runs (foundation/core/RunSet.h).
// Only the doors of the class make a value that is not empty, from a borrow of
// that brand.  A walk trusts the mark only in the base (owner_brand of
// TypeParts below).
struct names_owner_second {};

// What a class tells of one of its parts, one bit each: a reference, a const
// object (the referent of a reference, or the part itself), a mutable member,
// a type argument that is a scope brand, a plain part, the first template
// argument that is no template, and a type argument that the class owns (its
// statement of by_arguments of foundation/Transfer.h).
enum class PartFact : unsigned char {
    was_reference = 1,
    was_const = 2,
    is_mutable = 4,
    is_scope_brand = 8,
    is_plain = 16,
    is_first = 32,
    is_owned = 64,
};

[[nodiscard]] consteval unsigned char bit_of_(PartFact fact) { return static_cast<unsigned char>(fact); }

// One part of a class.  The type is bare.  kept is the reflection of the kept
// parts of a class that the parent holds by value and that is not plain, and
// null for each other part.
struct TypePart {
    std::meta::info type{};
    std::meta::info kept{};
    unsigned char facts = 0;

    [[nodiscard]] consteval bool has(PartFact fact) const { return (facts & bit_of_(fact)) != 0; }
};

// The parts that stand in TypeParts, and the most parts of one class.
inline constexpr std::size_t near_part_room = 12;
inline constexpr std::size_t wide_part_room = 512;

// The fields of some parts, in one array for each field.  GCC 16 refuses a
// constant that holds an array of a class with an element that no write
// reaches, and it takes an array of scalars.
template <std::size_t Room>
struct PartRows {
    std::meta::info types[Room]{};
    std::meta::info kept[Room]{};
    unsigned char facts[Room]{};
};

// The kept parts of a class with more than near_part_room parts.
using WideTypeParts = PartRows<wide_part_room>;

// The parts of a class in their order.
class PartRun {
public:
    template <std::size_t Room>
    consteval explicit PartRun(PartRows<Room> const& rows) : types_{rows.types}, kept_{rows.kept}, facts_{rows.facts} {}

    [[nodiscard]] consteval TypePart operator[](std::size_t index) const {
        return TypePart{types_[index], kept_[index], facts_[index]};
    }

private:
    std::meta::info const* types_ = nullptr;
    std::meta::info const* kept_ = nullptr;
    unsigned char const* facts_ = nullptr;
};

// The parts of one class.  The type arguments stand before argument_count, the
// types of the value arguments after them, and the bases and the members from
// member_start.  first_argument is the first type argument of a specialization,
// also when it is no part, and null when there is none.
struct TypeParts {
    PartRows<near_part_room> near{};
    std::meta::info wide{};
    std::size_t size = 0;
    std::size_t argument_count = 0;
    std::size_t member_start = 0;
    std::meta::info first_argument{};
    bool is_first_argument_const = false;
    // A loan key, a keyed owner or a token of a side: a class that states
    // loan_key_mark{} (foundation/core/LoanKeyTrait.h).
    bool is_key_marked = false;
    // A class of the base that states by_arguments{}, such as a Box: it owns
    // the object of each argument with the fact is_owned.  A walk trusts the
    // mark only in the base, so a marked class outside the base gives its
    // members.
    bool states_by_arguments = false;
    // A class that states synchronized{} or payload_cell{}: it hands its values
    // in and out by value.
    bool states_cell = false;
    // A borrow or an owner, whose parts give no base and no member.  The held
    // parts give them.
    bool hides_members = false;
    // A class whose state reflection does not show (holds_unreadable_state of
    // foundation/reflect/TypeComponents.h).
    bool is_unreadable = false;
    // A class with a type argument that is a scope brand: a borrow of a frame,
    // such as ViewMut<T, Brand>, RefMut<T, Brand> or Ref<T, Brand>.
    bool is_frame_borrow = false;
    // A class that states refers{}, refers_unique{} or refers_deep{}.
    bool is_borrow = false;
    // A borrow whose const object gives only const access to its target: a
    // class of the base (is_class_of_the_base_ of foundation/reflect/Members.h) that
    // states refers_deep{}.  A walk trusts the mark only in the base, and the
    // check file of each such class pins that each const door gives a borrow
    // of a const target.
    bool gives_const_access = false;
    // An owner: a class of the base that states an owner mark or derives from
    // OwnerCore in public, with a state that owns its storage.  No rule keeps
    // the marks in the base, so a walk reads the members of a marked class
    // outside the base.
    bool is_storage_owner = false;
    // A share of a pool: a class that states shares{} of foundation/Transfer.h,
    // such as the guard of a share.  It holds the address of its pool, which it
    // does not own, so it is no owner of storage, whatever its other marks
    // state.  Its mark by_arguments<0>{} states how it moves between threads.
    bool is_share = false;
    // A handle of the base that states keeps_own_borrows{}.  The store check
    // reads it as no borrow slot.
    bool keeps_own_borrows = false;
    // A class that states synchronized_by_proof{}, in the base or outside it.
    // The bit is the statement alone: the store check runs the check of the
    // mark before it trusts it.  Such a class is no plain class, so each walk
    // that reaches it runs the check.
    bool states_synchronized_by_proof = false;
    // A class of the base that states refers_static{} of foundation/Transfer.h,
    // such as a StaticText: each pointer that it holds as its own data member
    // names static storage.  The read of the mark checks each constructor of
    // the class (check_refers_static_statement of foundation/Transfer.h).  The
    // store check reads such a pointer as no borrow slot, and reads each other
    // part of the class.
    bool refers_static = false;
    // The brand of the owner of the target of a borrow of the base that states
    // names_owner_second, and null for each other class.  The read takes the
    // brand by its position, the second type argument, because the base has
    // no member that names it on each such class.  The walks take the target
    // of a borrow by its position too, the first type argument.  ArenaKeeps
    // of foundation/core/BumpArena.h and the store check of an epoch body of
    // foundation/core/CaptureStore.h read the owner of a borrow here alone.
    std::meta::info owner_brand{};
    // A class of the base that states stated_address{}.
    bool states_stated_address = false;
    // A class with an array of bytes or a union in a member.  Such a member can
    // hold a value of a type argument.  A class with no such member holds a
    // value of a type argument only in a member of a type that names it, which
    // a walk reads.
    bool holds_bytes = false;
    bool is_plain = false;

    // The parts in their order.  The parts of a class with more than
    // near_part_room parts stand in a variable of their own.
    [[nodiscard]] consteval PartRun run() const {
        if (size <= near_part_room) return PartRun{near};
        return PartRun{std::meta::extract<WideTypeParts const&>(wide)};
    }
};

[[nodiscard]] consteval bool is_class_or_union_(std::meta::info type) {
    return std::meta::is_class_type(type) || std::meta::is_union_type(type);
}

// True for a type that holds no part: an arithmetic type, an enumeration,
// void, the null pointer, each other type that is no class, union, pointer,
// array, function or pointer to member, such as a vector type, and an integer
// sequence, which is empty and can have some hundreds of template arguments.
[[nodiscard]] consteval bool is_leaf_(std::meta::info type) {
    if (is_class_or_union_(type)) {
        return std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^std::integer_sequence;
    }
    if (std::meta::is_pointer_type(type)) return false;
    if (std::meta::is_array_type(type)) {
        return is_leaf_(std::meta::dealias(std::meta::remove_cv(std::meta::remove_all_extents(type))));
    }
    return !std::meta::is_function_type(type) && !std::meta::is_member_pointer_type(type);
}

// True for a named tag of an arena: a member tag or a family tag
// (foundation/Brand.h).  ArenaKeeps of foundation/core/BumpArena.h and the
// store check of an epoch body of foundation/core/CaptureStore.h read the
// owner of a borrow through it.
[[nodiscard]] consteval bool is_arena_tag_(std::meta::info brand) {
    if (brand == std::meta::info{}) return false;
    std::meta::info const tag = std::meta::dealias(brand);
    return std::meta::is_class_type(tag) && std::meta::is_complete_type(tag)
        && (::foundation::reflect::carries_annotation(tag, ^^::foundation::brand::arena_member_brand)
            || ::foundation::reflect::carries_annotation(tag, ^^::foundation::brand::arena_family_brand));
}

// True for a class of the base that states stated_address{} and whose first
// type argument is a void*, such as a Tagged<void*, Tag> of fixy/Tagged.h:
// an address that no reader follows.  ArenaKeeps of
// foundation/core/BumpArena.h and the store check of an epoch body of
// foundation/core/CaptureStore.h read such a class as no borrow here alone,
// and read no part of it.
[[nodiscard]] consteval bool is_stated_address_(TypeParts const& parts) {
    std::meta::info const value = parts.first_argument;
    return parts.states_stated_address && value != std::meta::info{} && std::meta::is_pointer_type(value)
        && std::meta::is_void_type(std::meta::remove_pointer(value));
}

// True for raw storage that can hold a value of a type argument of its
// class: an array of bytes, or a union.
[[nodiscard]] consteval bool is_byte_storage_(std::meta::info type) {
    if (std::meta::is_union_type(type)) return true;
    if (!std::meta::is_array_type(type)) return false;
    std::meta::info const element = std::meta::dealias(std::meta::remove_cv(std::meta::remove_all_extents(type)));
    return element == ^^unsigned char || element == ^^char || element == ^^signed char || element == ^^std::byte;
}

[[nodiscard]] consteval bool is_scope_brand_(std::meta::info type);

// True when a type argument of the specialization is a scope brand.
[[nodiscard]] consteval bool names_scope_brand_(std::meta::info specialization) {
    std::vector<std::meta::info> const arguments = std::meta::template_arguments_of(specialization);
    std::meta::info const* const first = arguments.data();
    std::size_t const count = arguments.size();
    for (std::size_t index = 0; index < count; ++index) {
        if (std::meta::is_type(first[index]) && is_scope_brand_(std::meta::dealias(first[index]))) return true;
    }
    return false;
}

// The kept answer of is_scope_brand_ for one class: a specialization answers
// by its type arguments, and a closure by the mark of its call operator.
template <class Class>
consteval bool is_kept_scope_brand_() {
    if constexpr (std::meta::has_template_arguments(^^Class)) {
        return names_scope_brand_(^^Class);
    } else if constexpr (requires { &Class::operator(); }) {
        return std::meta::annotations_of_with_type(^^Class::operator(), ^^::foundation::brand::scope_brand).size() != 0;
    } else {
        return false;
    }
}

template <class Class>
inline constexpr bool is_scope_brand_v = is_kept_scope_brand_<Class>();

// The class that a pointer to a scope brand converts to, and a pointer to no
// other class.
struct ScopeBrandTest final {
    template <class Class>
        requires(is_scope_brand_v<Class>)
    consteval ScopeBrandTest(Class*) noexcept {}
};

// True for a scope brand: the closure type of CRUCIBLE_SCOPE_BRAND, whose call
// operator holds the mark foundation::brand::scope_brand, or a class template
// specialization with such a type argument at each depth, such as the brand of
// a view of a walk of rows.  A scope brand has no name, so the test reads no
// named class.  A specialization has no name either, and it answers by its
// arguments alone, so the test reads no member of it.  An incomplete class is
// no brand.  The read is the read of is_plain_class_ with the constraint of
// ScopeBrandTest.  Complexity: linear in the type arguments at each depth.
[[nodiscard]] consteval bool is_scope_brand_(std::meta::info type) {
    if (!std::meta::is_class_type(type) || std::meta::has_identifier(type)) return false;
    if (!std::meta::is_complete_type(type)) return false;
    return std::meta::is_convertible_type(std::meta::add_pointer(type), ^^ScopeBrandTest);
}

// The refusal of a class with more than wide_part_room parts.  Only the list of
// such a class instantiates it.  The condition depends on Class and is false
// for each class.
template <class Class>
consteval bool refuse_class_of_many_parts_() {
    static_assert(sizeof(Class) == 0,
                  "the walk of a frame door reads at most 512 parts of one class, and this class has more: divide the "
                  "class, or raise wide_part_room of foundation/core/TypeParts.h");
    return false;
}

template <class Class>
inline constexpr bool refuses_class_of_many_parts_v = refuse_class_of_many_parts_<Class>();

// The list of the parts of one class.  The rows keep the first Room parts, and
// size counts each one.
template <std::size_t Room>
class PartList {
public:
    consteval explicit PartList(PartRows<Room>& rows) : rows_{rows} {}

    consteval void push(std::meta::info type, std::meta::info kept, unsigned char facts) {
        if (size_ < Room) {
            rows_.types[size_] = type;
            rows_.kept[size_] = kept;
            rows_.facts[size_] = facts;
        }
        ++size_;
    }

    [[nodiscard]] consteval std::size_t size() const { return size_; }

private:
    PartRows<Room>& rows_;
    std::size_t size_ = 0;
};

// The two reads of one class: each part, as TypeParts gives them, or the bases
// and the members alone.
enum class PartRead : unsigned char {
    each,
    held,
};

// The two lists of the parts of one class.  They are templates on their room
// and their read, so a unit that reads no class instantiates no read.
template <std::size_t Room, PartRead Read>
[[nodiscard]] consteval TypeParts list_type_parts_(std::meta::info type);
template <std::size_t Room, PartRead Read>
[[nodiscard]] consteval PartRows<Room> list_wide_type_parts_(std::meta::info type);

// The kept parts of one class (the kept answer).
template <class Class>
inline constexpr TypeParts type_parts_v = list_type_parts_<near_part_room, PartRead::each>(^^Class);

// The kept parts of a class with more than near_part_room parts.
template <class Class>
inline constexpr WideTypeParts wide_type_parts_v = list_wide_type_parts_<wide_part_room, PartRead::each>(^^Class);

// The kept bases and members of one class, also of a borrow and of an owner.
// The argument_count and the member_start of these parts are zero.
template <class Class>
inline constexpr TypeParts held_parts_v = list_type_parts_<near_part_room, PartRead::held>(^^Class);

// The kept held parts of a class with more than near_part_room held parts.
template <class Class>
inline constexpr WideTypeParts wide_held_parts_v = list_wide_type_parts_<wide_part_room, PartRead::held>(^^Class);

// The reflection of the kept parts of a complete class or union.
[[nodiscard]] consteval std::meta::info kept_parts_of(std::meta::info type) {
    std::meta::info const arguments[] = {type};
    return std::meta::substitute(^^type_parts_v, arguments);
}

// The reflection of the kept held parts of a complete class or union.
[[nodiscard]] consteval std::meta::info kept_held_parts_of(std::meta::info type) {
    std::meta::info const arguments[] = {type};
    return std::meta::substitute(^^held_parts_v, arguments);
}

// The class that a pointer to a plain class converts to, and a pointer to no
// other class.
struct PlainClassTest final {
    template <class Class>
        requires(type_parts_v<Class>.is_plain)
    consteval PlainClassTest(Class*) noexcept {}
};

// True for a plain class or union.  The type is complete.  The trait takes two
// reflections, and substitute takes a range, which the compiler reads through
// the concepts of the ranges library at each call.  So this read of a kept
// answer costs about 0.1 M compile instructions, and a read through
// kept_parts_of about 1.7 M.  The answer is the constraint of one constructor
// of PlainClassTest, so it is the kept answer itself.
[[nodiscard]] consteval bool is_plain_class_(std::meta::info type) {
    return std::meta::is_convertible_type(std::meta::add_pointer(type), ^^PlainClassTest);
}

// True for a template argument that is a value or an object, whose type can
// name a class.
[[nodiscard]] consteval bool is_value_argument_(std::meta::info argument) {
    return std::meta::is_value(argument) || std::meta::is_object(argument);
}

// Adds the type of each value argument that is no leaf to the list.  first_place
// is the place of the first argument that is a type or a value argument.
template <std::size_t Room>
consteval void list_value_arguments_(PartList<Room>& list, std::vector<std::meta::info> const& arguments,
                                     std::size_t first_place) {
    for (std::size_t index = first_place; index < arguments.size(); ++index) {
        if (!is_value_argument_(arguments[index])) continue;
        std::meta::info const declared = std::meta::dealias(std::meta::type_of(arguments[index]));
        std::meta::info const bare = ::foundation::reflect::bare_type(declared);
        if (is_leaf_(bare)) continue;
        unsigned char facts = index == first_place ? bit_of_(PartFact::is_first) : 0;
        if (std::meta::is_reference_type(declared)) {
            facts = static_cast<unsigned char>(facts | bit_of_(PartFact::was_reference));
        }
        list.push(bare, std::meta::info{}, facts);
    }
}

// Adds each type argument that is no leaf to the list, and then the type of each
// value argument, and reads the facts of the class that its arguments give.
template <std::size_t Room>
consteval void list_arguments_(TypeParts& parts, PartList<Room>& list, std::meta::info type) {
    std::vector<std::meta::info> const arguments = std::meta::template_arguments_of(type);
    std::meta::info const* const first = arguments.data();
    std::size_t const count = arguments.size();
    std::size_t first_place = count;
    bool has_value_arguments = false;
    ::foundation::transfer::detail::OwnedArguments const owned =
        ::foundation::reflect::has_annotations(type) ? ::foundation::transfer::detail::owned_arguments_of(type)
                                                     : ::foundation::transfer::detail::OwnedArguments{};
    // The position of an argument in the list of the type and value arguments,
    // as by_arguments counts it (foundation/Transfer.h).
    std::size_t next_position = 0;
    for (std::size_t index = 0; index < count; ++index) {
        std::meta::info const argument = first[index];
        if (!std::meta::is_type(argument)) {
            if (!is_value_argument_(argument)) continue;
            ++next_position;
            if (first_place == count) first_place = index;
            std::meta::info const value_type = std::meta::dealias(std::meta::type_of(argument));
            has_value_arguments = has_value_arguments
                               || (!std::meta::is_arithmetic_type(value_type) && !std::meta::is_enum_type(value_type));
            continue;
        }
        std::size_t const position = next_position;
        ++next_position;
        bool const is_first = first_place == count;
        if (is_first) first_place = index;
        std::meta::info const declared = std::meta::dealias(argument);
        bool const is_const = std::meta::is_const_type(std::meta::remove_reference(declared));
        std::meta::info const bare = ::foundation::reflect::bare_type(declared);
        if (parts.first_argument == std::meta::info{}) {
            parts.first_argument = bare;
            parts.is_first_argument_const = is_const;
        }
        if (is_leaf_(bare)) continue;
        unsigned char facts = is_const ? bit_of_(PartFact::was_const) : 0;
        if (is_first) facts = static_cast<unsigned char>(facts | bit_of_(PartFact::is_first));
        if (owned.owns(position)) facts = static_cast<unsigned char>(facts | bit_of_(PartFact::is_owned));
        if (std::meta::is_reference_type(declared)) {
            facts = static_cast<unsigned char>(facts | bit_of_(PartFact::was_reference));
        }
        if (is_scope_brand_(bare)) {
            facts = static_cast<unsigned char>(facts | bit_of_(PartFact::is_scope_brand));
            parts.is_frame_borrow = parts.is_frame_borrow || !std::meta::is_reference_type(declared);
        }
        list.push(bare, std::meta::info{}, facts);
    }
    parts.argument_count = list.size();
    if (has_value_arguments) list_value_arguments_(list, arguments, first_place);
}

// The second type argument of a specialization, bare, or a null reflection
// when it has fewer than two.  A value argument does not count.  Complexity:
// linear in the template arguments of the class.
[[nodiscard]] consteval std::meta::info second_type_argument_(std::meta::info type) {
    if (!std::meta::has_template_arguments(type)) return std::meta::info{};
    std::vector<std::meta::info> const arguments = std::meta::template_arguments_of(type);
    std::meta::info const* const first = arguments.data();
    std::size_t const count = arguments.size();
    std::size_t types = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::meta::is_type(first[index])) continue;
        if (types == 1) return ::foundation::reflect::bare_type(std::meta::dealias(first[index]));
        ++types;
    }
    return std::meta::info{};
}

// Reads the marks of the class that a walk reads: is_borrow and
// gives_const_access, is_storage_owner and the marks of an owner, the key
// mark, keeps_own_borrows, refers_static, is_share, the statement of
// synchronized_by_proof, owner_brand and states_stated_address.
consteval void read_marks_(TypeParts& parts, std::meta::info type) {
    namespace tr = ::foundation::transfer;
    bool states_owner = false;
    bool owns_arguments = false;
    bool refers_deep = false;
    bool keeps_own = false;
    bool refers_static = false;
    bool names_owner = false;
    bool states_address = false;
    std::vector<std::meta::info> const annotations = std::meta::annotations_of(type);
    std::meta::info const* const first = annotations.data();
    std::size_t const count = annotations.size();
    for (std::size_t index = 0; index < count; ++index) {
        std::meta::info const mark = std::meta::dealias(std::meta::remove_cv(std::meta::type_of(first[index])));
        if (mark == ^^tr::refers || mark == ^^tr::refers_unique) {
            parts.is_borrow = true;
        } else if (mark == ^^tr::refers_deep) {
            parts.is_borrow = true;
            refers_deep = true;
        } else if (tr::detail::is_by_arguments(first[index])) {
            states_owner = true;
            owns_arguments = true;
        } else if (mark == ^^tr::synchronized || mark == ^^tr::payload_cell) {
            states_owner = true;
            parts.states_cell = true;
        } else if (mark == ^^owns_its_storage) {
            states_owner = true;
        } else if (mark == ^^loan_key_mark) {
            parts.is_key_marked = true;
        } else if (mark == ^^keeps_own_borrows) {
            keeps_own = true;
        } else if (mark == ^^tr::refers_static) {
            refers_static = true;
        } else if (mark == ^^tr::shares) {
            parts.is_share = true;
        } else if (mark == ^^synchronized_by_proof) {
            parts.states_synchronized_by_proof = true;
        } else if (mark == ^^names_owner_second) {
            names_owner = true;
        } else if (mark == ^^stated_address) {
            states_address = true;
        }
    }
    if (!states_owner && !refers_deep && !keeps_own && !refers_static && !names_owner && !states_address) return;
    bool const is_of_the_base = ::foundation::reflect::is_class_of_the_base_(type);
    if (names_owner && is_of_the_base) parts.owner_brand = second_type_argument_(type);
    parts.states_stated_address = states_address && is_of_the_base;
    parts.is_storage_owner = states_owner && is_of_the_base && !parts.is_share;
    parts.states_by_arguments = owns_arguments && is_of_the_base;
    parts.gives_const_access = refers_deep && is_of_the_base;
    parts.keeps_own_borrows = keeps_own && is_of_the_base;
    // The check of the constructors of a class that states refers_static{}
    // stops the build when one of them can take a pointer of the run time.
    if (refers_static && is_of_the_base) tr::detail::check_refers_static_statement(type);
    parts.refers_static = refers_static && is_of_the_base;
}

// True when the state of a core states releases_nothing as true
// (ReleasesNothing of foundation/core/Owner.h).  The read takes the public
// static member itself, so it reads no template that a unit can specialize and
// no private state.
[[nodiscard]] consteval bool releases_nothing_(std::meta::info state) {
    std::meta::access_context const in_public = std::meta::access_context::unprivileged();
    std::vector<std::meta::info> const members = std::meta::static_data_members_of(state, in_public);
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member = members[index];
        if (::foundation::reflect::has_identifier_named(member, "releases_nothing")) {
            return std::meta::extract<bool>(member);
        }
    }
    return false;
}

// True when one of the bases is a public OwnerCore whose state owns its
// storage.  Each owner of the base derives from OwnerCore in public.  A state
// that releases nothing, such as the run of a region, names storage of another
// owner, so its core owns no storage, and a walk reads its members.
[[nodiscard]] consteval bool derives_from_storage_core_(std::vector<std::meta::info> const& bases) {
    for (std::size_t index = 0; index < bases.size(); ++index) {
        std::meta::info const base = std::meta::dealias(std::meta::type_of(bases[index]));
        bool const is_core = std::meta::has_template_arguments(base) && std::meta::template_of(base) == ^^OwnerCore;
        if (!is_core || !std::meta::is_public(bases[index])) continue;
        return !releases_nothing_(std::meta::template_arguments_of(base)[0]);
    }
    return false;
}

// True for a part that a class holds by value and that is plain: a plain class,
// a pointer to a function, a pointer to member, and an array of plain elements.
// The part is bare and no leaf.
[[nodiscard]] consteval bool is_plain_held_(std::meta::info type) {
    if (is_class_or_union_(type)) return is_plain_class_(type);
    if (std::meta::is_pointer_type(type)) return std::meta::is_function_type(std::meta::remove_pointer(type));
    if (!std::meta::is_array_type(type)) return true;
    std::meta::info const element = std::meta::dealias(std::meta::remove_cv(std::meta::remove_all_extents(type)));
    return is_leaf_(element) || is_plain_held_(element);
}

// Adds the part that the class holds by value to the list, with the facts.
// True when the part is plain.
template <std::size_t Room>
[[nodiscard]] consteval bool list_held_(PartList<Room>& list, std::meta::info type, unsigned char facts) {
    bool const is_plain = is_plain_held_(type);
    std::meta::info kept{};
    if (is_plain) {
        facts = static_cast<unsigned char>(facts | bit_of_(PartFact::is_plain));
    } else if (is_class_or_union_(type)) {
        kept = kept_parts_of(type);
    }
    list.push(type, kept, facts);
    return is_plain;
}

// Adds each base of the class to the list.  True when each base is plain.
template <std::size_t Room>
[[nodiscard]] consteval bool list_bases_(PartList<Room>& list, std::vector<std::meta::info> const& bases) {
    std::meta::info const* const first = bases.data();
    std::size_t const count = bases.size();
    bool is_plain = true;
    for (std::size_t index = 0; index < count; ++index) {
        std::meta::info const bare = ::foundation::reflect::bare_type(std::meta::type_of(first[index]));
        is_plain = list_held_(list, bare, 0) && is_plain;
    }
    return is_plain;
}

// Adds each data member of the class to the list: each member that is a
// reference, and each other member that is no leaf.  True when each member is
// plain, which a reference never is.
template <std::size_t Room>
[[nodiscard]] consteval bool list_members_(TypeParts& parts, PartList<Room>& list,
                                           std::vector<std::meta::info> const& members) {
    std::meta::info const* const first = members.data();
    std::size_t const count = members.size();
    bool is_plain = true;
    for (std::size_t index = 0; index < count; ++index) {
        std::meta::info const declared = std::meta::dealias(std::meta::type_of(first[index]));
        if (std::meta::is_arithmetic_type(declared) || std::meta::is_enum_type(declared)) continue;
        std::meta::info const bare = ::foundation::reflect::bare_type(declared);
        bool const is_reference = std::meta::is_reference_type(declared);
        parts.holds_bytes = parts.holds_bytes || is_byte_storage_(bare);
        if (is_leaf_(bare) && !is_reference) continue;
        bool const is_const = std::meta::is_const_type(std::meta::remove_reference(declared));
        unsigned char facts = is_const ? bit_of_(PartFact::was_const) : 0;
        if (is_reference) {
            facts = static_cast<unsigned char>(facts | bit_of_(PartFact::was_reference));
            list.push(bare, std::meta::info{}, facts);
            is_plain = false;
            continue;
        }
        if (std::meta::is_mutable_member(first[index])) {
            facts = static_cast<unsigned char>(facts | bit_of_(PartFact::is_mutable));
        }
        is_plain = list_held_(list, bare, facts) && is_plain;
    }
    return is_plain;
}

// Lists the parts of the class, and reads its facts.  The class is complete.
// For the read of each part, a borrow and an owner that are specializations
// give their arguments and no member, because a walk that asks for their parts
// reads no member of one, and a read of the members of a specialization makes
// the compiler instantiate each member function with a deduced result.  For the
// read of the held parts, each class gives its bases and its members, and no
// argument.  Complexity: linear in the template arguments, the bases and the
// members of the class.
template <std::size_t Room, PartRead Read>
consteval void list_parts_(TypeParts& parts, PartList<Room>& list, std::meta::info type) {
    std::meta::access_context const unchecked = std::meta::access_context::unchecked();
    bool const is_specialization = std::meta::has_template_arguments(type);
    constexpr bool lists_held_alone = Read == PartRead::held;
    if (is_specialization && !lists_held_alone) list_arguments_(parts, list, type);
    parts.member_start = list.size();
    parts.size = list.size();
    read_marks_(parts, type);
    bool const is_any_borrow = parts.is_frame_borrow || parts.is_borrow;
    parts.hides_members = is_any_borrow;
    if (is_specialization && is_any_borrow && !lists_held_alone) return;
    std::vector<std::meta::info> const bases = std::meta::bases_of(type, unchecked);
    if (!parts.is_storage_owner && !parts.is_share && derives_from_storage_core_(bases)) {
        parts.is_storage_owner = ::foundation::reflect::is_class_of_the_base_(type);
    }
    parts.hides_members = is_any_borrow || parts.is_storage_owner;
    if (is_specialization && parts.is_storage_owner && !lists_held_alone) return;
    std::vector<std::meta::info> const members = std::meta::nonstatic_data_members_of(type, unchecked);
    parts.is_unreadable =
        !is_specialization && bases.size() == 0 && members.size() == 0 && !std::meta::is_empty_type(type);
    if (parts.hides_members && !lists_held_alone) return;
    bool const has_plain_bases = list_bases_(list, bases);
    bool const has_plain_members = list_members_(parts, list, members);
    parts.size = list.size();
    // A class that states synchronized_by_proof{} is no plain class, so each
    // walk that reaches it runs the check of the mark.
    parts.is_plain = has_plain_bases && has_plain_members && !parts.is_unreadable && !parts.hides_members
                  && (!parts.holds_bytes || parts.argument_count == 0) && !parts.states_synchronized_by_proof;
}

template <std::size_t Room, PartRead Read>
[[nodiscard]] consteval TypeParts list_type_parts_(std::meta::info type) {
    TypeParts parts{};
    PartList<Room> list{parts.near};
    list_parts_<Room, Read>(parts, list, type);
    if (parts.size > Room) {
        std::meta::info const arguments[] = {type};
        if (parts.size > wide_part_room) {
            static_cast<void>(
                std::meta::extract<bool>(std::meta::substitute(^^refuses_class_of_many_parts_v, arguments)));
        }
        std::meta::info const wide_parts = Read == PartRead::held ? ^^wide_held_parts_v : ^^wide_type_parts_v;
        parts.wide = std::meta::substitute(wide_parts, arguments);
    }
    return parts;
}

template <std::size_t Room, PartRead Read>
[[nodiscard]] consteval PartRows<Room> list_wide_type_parts_(std::meta::info type) {
    TypeParts facts{};
    PartRows<Room> rows{};
    PartList<Room> list{rows};
    list_parts_<Room, Read>(facts, list, type);
    return rows;
}

}  // namespace foundation::core::detail
