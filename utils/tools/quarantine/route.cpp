// The family borrow route: how a borrow of one type reaches an object of another
// type.  live.cpp reads a call with these answers: the types alone tell which
// object of an argument a result can name, and which owners hold that object.
//
// Owners and ends
//     A class has an end when its destructor is not trivial.  An owner is a class
//     with an end: each pointer of it, and of its parts by value that have no end
//     of their own, names storage that it owns.  A borrow class of the base
//     (refers{}, refers_unique{} and refers_deep{} of foundation/Transfer.h) owns
//     nothing.  A class that states owns_its_storage{} of foundation/core/Owner.h
//     keeps each block until its own end, so it is one owner with no owner behind
//     it.  When no code can move or assign such a class, a call through a reference
//     to it ends nothing.  Behind a pointer it ends with the owner of the pointer.  An
//     owner of the base that keeps its elements behind a pointer to void owns the types
//     of its arguments (escape.cpp, the borrow).
//
// Targets
//     The targets of a result are the types that its pointers name, also in the
//     fields and in the arguments of an owner.  A pointer to void, to a character
//     or to std::byte can name each object.  A block of std::uint8_t or std::byte
//     can hold an object of each type, so it is on the route of a target that no
//     part of the argument names.
//
// Reach
//     A result reaches into an argument when a target is the argument, a part of
//     it or an object that it owns.  It reaches through the argument when a target
//     is only behind a borrow that the argument holds.  A copy of a borrow reaches
//     through it.
//
// Route
//     The route of a result into an argument holds each owner of the argument that
//     has a target behind it.  The end of such an owner ends the borrow, and a move
//     of it keeps the storage, so the borrow goes with it.  A target in the bytes
//     of the argument has no owner on its route.
//
// Blocks
//     A class that states owns_its_storage{} can keep an object of each type in a
//     raw block.  Then it is on the route of each result, also when a part of the
//     argument has the type of a target.  A block is raw when a pointer of the owner
//     names bytes or void, and when a class behind a pointer holds an array of
//     bytes.  A type argument of the owner is an element.  The bytes of an element
//     are no raw block, so a list of elements is on the route of its elements only.
//     An owner with no pointer keeps the address of its blocks as a number, so its
//     blocks are raw.  The state of such an owner is each part by value that holds its
//     pointers.  A result that names the state, such as a walk of a list, keeps a place
//     in the blocks, so the owner is on its route too.
//
// Answers
//     An answer is a set of TYPE_UID values, which no collection reuses.  The
//     family keeps an answer for the unit when the walk read each part of each
//     class.  A walk that meets an incomplete class counts an unread part, so the
//     body that asks waits for the end of the unit (the kept bodies of plugin.h),
//     and its answer holds only until begin_route_reading().  A wide reading takes
//     an incomplete class as one that can name, hold and own each object.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The target of a pointer that can name each object, the content that can hold
// each object, and the storage of an owner behind a pointer to void.  No TYPE_UID
// has these values.
constexpr unsigned kErasedType = ~0U;
constexpr unsigned kRawBytes = ~0U - 1U;
constexpr unsigned kVoidStorage = ~0U - 2U;

// True in a wide reading (the answers).
bool is_wide_reading = false;

void add_id(TypeSet& set, unsigned id) {
    auto const place = std::lower_bound(set.begin(), set.end(), id);
    if (place == set.end() || *place != id) {
        set.insert(place, id);
    }
}

bool has_id(const TypeSet& set, unsigned id) { return std::binary_search(set.begin(), set.end(), id); }

// The answers of one question for each type or each pair of types.
template <typename Answer>
class RouteAnswers {
public:
    // The answer for KEY, which MAKE gives one time.  A reference to an answer
    // lives until the next begin_route_reading().
    template <typename Make>
    const Answer& get(std::uint64_t key, Make make) {
        auto found = closed_.find(key);
        if (found != closed_.end()) {
            return found->second;
        }
        auto open = open_.find(key);
        if (open != open_.end()) {
            ++unread_parts;
            return open->second;
        }
        unsigned const unread = unread_parts;
        Answer answer = make();
        auto& answers = unread_parts == unread ? closed_ : open_;
        return answers.emplace(key, std::move(answer)).first->second;
    }

    void clear_open() { open_.clear(); }

private:
    std::unordered_map<std::uint64_t, Answer> closed_;
    std::unordered_map<std::uint64_t, Answer> open_;
};

RouteAnswers<TypeSet> target_answers;
RouteAnswers<TypeSet> owned_content_answers;
RouteAnswers<TypeSet> content_answers;
RouteAnswers<TypeSet> behind_answers;
RouteAnswers<TypeSet> part_answers;
RouteAnswers<TypeSet> cell_answers;
RouteAnswers<TypeSet> held_answers;
RouteAnswers<std::vector<OwnerClass>> owner_answers;
RouteAnswers<BorrowRoute> route_answers;
RouteAnswers<bool> keeps_answers;
RouteAnswers<bool> raw_block_answers;
RouteAnswers<TypeSet> state_answers;

std::uint64_t pair_key(tree first, tree second) {
    return (static_cast<std::uint64_t>(TYPE_UID(first)) << 32) | TYPE_UID(second);
}

// The classes that one walk visited, with two properties of the visit.
class Visits {
public:
    bool enter(tree type, bool first = false, bool second = false) {
        unsigned const properties = (first ? 1U : 0U) | (second ? 2U : 0U);
        return seen_.insert((static_cast<std::uint64_t>(TYPE_UID(type)) << 2) | properties).second;
    }

private:
    std::unordered_set<std::uint64_t> seen_;
};

// The class of TYPE when a walk can read its members.  An incomplete class is a
// part that the walk cannot read.  A wide reading adds the erased type and the raw
// bytes to SET for it.
tree readable_class(tree type, TypeSet* set = nullptr) {
    if (type == NULL_TREE || !RECORD_OR_UNION_TYPE_P(type)) {
        return NULL_TREE;
    }
    if (!COMPLETE_TYPE_P(type)) {
        ++unread_parts;
        if (is_wide_reading && set != nullptr) {
            add_id(*set, kErasedType);
            add_id(*set, kRawBytes);
        }
        return NULL_TREE;
    }
    return type;
}

bool is_byte_enumeration(tree type) {
    return TREE_CODE(type) == ENUMERAL_TYPE && TYPE_NAME(type) != NULL_TREE && DECL_P(TYPE_NAME(type))
        && DECL_NAME(TYPE_NAME(type)) != NULL_TREE
        && std::strcmp(IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(type))), "byte") == 0;
}

// True for void, a type of one byte and std::byte.
bool is_erased_type(tree type) {
    return VOID_TYPE_P(type) || (TREE_CODE(type) == INTEGER_TYPE && TYPE_PRECISION(type) == BITS_PER_UNIT)
        || is_byte_enumeration(type);
}

// True for std::uint8_t and std::byte, whose runs hold objects and no text.
bool is_raw_byte(tree type) { return type == unsigned_char_type_node || is_byte_enumeration(type); }

// True for a class whose values own the storage that its pointers name.
bool is_owner_class(tree type) { return CLASS_TYPE_P(type) && TYPE_HAS_NONTRIVIAL_DESTRUCTOR(type); }

// True for a class of the namespace std or of a namespace inside it.
bool is_standard_class(tree type) {
    tree top = top_namespace(type);
    return top != NULL_TREE && top != global_namespace && DECL_NAMESPACE_STD_P(top);
}

// Calls VISIT with each type argument that the values of the owner TYPE of the
// base hold (escape.cpp, the borrow).  Returns false for a class that is no such
// owner.
template <typename Visit>
bool visit_owner_arguments(tree type, Visit visit) {
    int const owned = CLASS_TYPE_P(type) ? held_owner_arguments(type) : -1;
    if (owned < 0) {
        return false;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < owned && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument)) {
            visit(argument);
        }
    }
    return true;
}

// Calls VISIT with each type argument of the class TYPE, also in a pack.
template <typename Visit>
void visit_type_arguments(tree type, Visit visit) {
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument == NULL_TREE) {
            continue;
        }
        if (TYPE_P(argument) && TREE_CODE(argument) != TYPE_ARGUMENT_PACK) {
            visit(argument);
        } else if (TREE_CODE(argument) == TYPE_ARGUMENT_PACK) {
            tree pack = ARGUMENT_PACK_ARGS(argument);
            for (int item = 0; item < TREE_VEC_LENGTH(pack); ++item) {
                if (TREE_VEC_ELT(pack, item) != NULL_TREE && TYPE_P(TREE_VEC_ELT(pack, item))) {
                    visit(TREE_VEC_ELT(pack, item));
                }
            }
        }
    }
}

template <typename Visit>
void visit_fields(tree type, Visit visit) {
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL) {
            visit(field);
        }
    }
}

// The type TYPE with no array.
tree without_array(tree type) {
    while (type != NULL_TREE && TREE_CODE(type) == ARRAY_TYPE) {
        type = TREE_TYPE(type);
    }
    return type;
}

// Adds the targets of TYPE to SET.
void add_targets(tree type, TypeSet& set, Visits& visits) {
    if (is_indirect_type(type)) {
        tree target = element_class(TREE_TYPE(type));
        if (target != NULL_TREE) {
            add_id(set, is_erased_type(target) ? kErasedType : TYPE_UID(target));
        }
        return;
    }
    type = readable_class(element_class(type), &set);
    if (type == NULL_TREE || !visits.enter(type)) {
        return;
    }
    auto add = [&](tree argument) { add_targets(argument, set, visits); };
    if (visit_owner_arguments(type, add)) {
        return;
    }
    // An owner of the standard library, such as a std::vector, owns the storage
    // that its pointers name.
    if (is_owner_class(type) && is_standard_class(type)) {
        visit_type_arguments(type, add);
        return;
    }
    visit_fields(type, [&](tree field) { add_targets(without_array(TREE_TYPE(field)), set, visits); });
}

const TypeSet& targets_of(tree type) {
    return target_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_targets(type, set, visits);
        return set;
    });
}

// Adds each type that an object of TYPE holds or can reach to SET.  With IS_OWNED,
// the walk goes only through the pointers of owners.  IS_IN_OWNER tells that an
// owner holds the object, so its pointers are of that owner.
void add_contents(tree type, TypeSet& set, Visits& visits, bool is_owned, bool is_in_owner) {
    if (type == NULL_TREE) {
        return;
    }
    if (is_indirect_type(type)) {
        if (!is_owned || is_in_owner) {
            add_contents(TREE_TYPE(type), set, visits, is_owned, false);
        }
        return;
    }
    type = element_class(type);
    if (type == NULL_TREE) {
        return;
    }
    if (is_erased_type(type)) {
        add_id(set, kErasedType);
        if (is_raw_byte(type)) {
            add_id(set, kRawBytes);
        }
        return;
    }
    add_id(set, TYPE_UID(type));
    if (readable_class(type, &set) == NULL_TREE || !visits.enter(type, is_in_owner)
        || (is_owned && is_borrow_class(type))) {
        return;
    }
    // A unique handle, such as a ViewMut, has no copy and a move, and it lends its
    // target as its own.  An atomic cell has no move, and a load of it copies a
    // pointer.
    bool const is_handle =
        CLASS_TYPE_P(type) && !trivially_copyable_p(type) && classtype_has_move_assign_or_move_ctor_p(type, true);
    bool const is_owner = is_in_owner || is_owner_class(type) || is_handle;
    visit_fields(type,
                 [&](tree field) { add_contents(without_array(TREE_TYPE(field)), set, visits, is_owned, is_owner); });
}

const TypeSet& owned_contents_of(tree type) {
    return owned_content_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_contents(type, set, visits, true, false);
        return set;
    });
}

const TypeSet& contents_of(tree type) {
    return content_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_contents(type, set, visits, false, true);
        return set;
    });
}

// Adds each type that is behind a pointer of the owner TYPE or of a part of it to
// SET.  IS_IN_OWNER tells that an owner holds the object.
void add_behind(tree type, TypeSet& set, Visits& visits, bool is_in_owner) {
    type = readable_class(element_class(type), &set);
    if (type == NULL_TREE || is_borrow_class(type)) {
        return;
    }
    bool const is_owner = is_in_owner || is_owner_class(type);
    if (!visits.enter(type, is_owner)) {
        return;
    }
    visit_fields(type, [&](tree field) {
        tree held = without_array(TREE_TYPE(field));
        if (!is_indirect_type(held)) {
            add_behind(held, set, visits, is_owner);
            return;
        }
        if (is_owner) {
            Visits behind;
            add_contents(TREE_TYPE(held), set, behind, true, true);
            if (VOID_TYPE_P(TREE_TYPE(held))) {
                add_id(set, kVoidStorage);
            }
        }
    });
}

// The types behind the owner TYPE.  An owner of the base with storage behind a
// pointer to void has the types of its arguments behind it.
const TypeSet& types_behind(tree type) {
    return behind_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_behind(type, set, visits, false);
        if (has_id(set, kVoidStorage)) {
            visit_owner_arguments(type, [&](tree argument) {
                Visits behind;
                add_contents(argument, set, behind, true, true);
            });
        }
        return set;
    });
}

// What the pointers of an owner name (the blocks).
struct Blocks {
    bool have_pointer = false;
    bool are_raw = false;
};

void read_blocks(tree type, const TypeSet& elements, Blocks& blocks, Visits& visits);

// Reads the objects that the POINTER of an owner names.
void read_pointee(tree pointer, const TypeSet& elements, Blocks& blocks, Visits& visits) {
    tree const named = TYPE_MAIN_VARIANT(TREE_TYPE(pointer));
    if (FUNC_OR_METHOD_TYPE_P(named)) {
        return;
    }
    blocks.have_pointer = true;
    if (is_indirect_type(named)) {
        read_pointee(named, elements, blocks, visits);
    } else if (is_erased_type(element_class(named))) {
        blocks.are_raw = true;
    } else {
        read_blocks(named, elements, blocks, visits);
    }
}

// Reads the pointers and the arrays of bytes of TYPE, but not the content of an
// element.
void read_blocks(tree type, const TypeSet& elements, Blocks& blocks, Visits& visits) {
    tree const object = element_class(type);
    if (object == NULL_TREE || has_id(elements, TYPE_UID(object))) {
        return;
    }
    if (is_raw_byte(object)) {
        blocks.are_raw = blocks.are_raw || TREE_CODE(type) == ARRAY_TYPE;
        return;
    }
    if (!RECORD_OR_UNION_TYPE_P(object)) {
        return;
    }
    if (readable_class(object) == NULL_TREE) {
        blocks.are_raw = true;
        return;
    }
    if (!visits.enter(object)) {
        return;
    }
    visit_fields(object, [&](tree field) {
        tree held = without_array(TREE_TYPE(field));
        if (is_indirect_type(held)) {
            read_pointee(held, elements, blocks, visits);
        } else {
            read_blocks(TREE_TYPE(field), elements, blocks, visits);
        }
    });
}

// True when a block of the owner TYPE can hold an object of each type (the blocks).
// The walk is linear in the count of the fields.
bool has_raw_blocks(tree type) {
    return raw_block_answers.get(TYPE_UID(type), [type] {
        TypeSet elements;
        visit_type_arguments(type, [&](tree argument) {
            tree element = element_class(argument);
            if (element != NULL_TREE) {
                add_id(elements, TYPE_UID(element));
            }
        });
        Blocks blocks;
        Visits visits;
        read_blocks(type, elements, blocks, visits);
        return blocks.are_raw || !blocks.have_pointer;
    });
}

// True when an object of TYPE holds a pointer in its bytes.
bool holds_pointer(tree type, Visits& visits) {
    type = without_array(type);
    if (is_indirect_type(type)) {
        return true;
    }
    type = readable_class(element_class(type));
    if (type == NULL_TREE || !visits.enter(type)) {
        return false;
    }
    bool is_held = false;
    visit_fields(type, [&](tree field) { is_held = is_held || holds_pointer(TREE_TYPE(field), visits); });
    return is_held;
}

// Adds each class part by value of TYPE that holds a pointer to SET.
void add_state(tree type, TypeSet& set, Visits& visits) {
    visit_fields(type, [&](tree field) {
        tree part = readable_class(element_class(TREE_TYPE(field)));
        Visits pointers;
        if (part != NULL_TREE && visits.enter(part) && holds_pointer(part, pointers)) {
            add_id(set, TYPE_UID(part));
            add_state(part, set, visits);
        }
    });
}

// The parts by value of the owner TYPE that hold its pointers (the blocks).  The walk
// is quadratic in the depth of the parts.
const TypeSet& state_of(tree type) {
    return state_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_state(type, set, visits);
        return set;
    });
}

// True when no code can take the value of an object of TYPE away: the class has no
// move constructor and no assignment that a caller can use.
bool is_fixed_class(tree type) {
    // A member that the front end did not declare can be one, and the walk declares
    // none.
    if (CLASSTYPE_LAZY_MOVE_CTOR(type) || CLASSTYPE_LAZY_MOVE_ASSIGN(type) || CLASSTYPE_LAZY_COPY_ASSIGN(type)
        || classtype_has_non_deleted_move_ctor(type)) {
        return false;
    }
    for (ovl_iterator each(get_class_binding(type, assign_op_identifier)); each; ++each) {
        if (!DECL_DELETED_FN(STRIP_TEMPLATE(*each))) {
            return false;
        }
    }
    return true;
}

// Adds each owner that an object of TYPE is or owns to OWNERS.  IS_BEHIND tells that
// the object is behind a pointer, so the end of the owner of the pointer ends it, also
// when no code can move it.
void add_owners(tree type, std::vector<OwnerClass>& owners, Visits& visits, bool is_in_owner, bool is_behind) {
    tree const object = element_class(type);
    type = readable_class(object);
    // In a wide reading an incomplete class is an owner of each object.
    if (type == NULL_TREE && is_wide_reading && object != NULL_TREE && RECORD_OR_UNION_TYPE_P(object)) {
        static const TypeSet each_object{kRawBytes, kErasedType};
        owners.push_back(OwnerClass{TYPE_UID(object), &each_object, nullptr, false, false});
    }
    if (type == NULL_TREE || is_borrow_class(type)) {
        return;
    }
    bool const is_owner = is_owner_class(type);
    bool const owns_pointers = is_in_owner || is_owner;
    if (!visits.enter(type, owns_pointers, is_behind)) {
        return;
    }
    if (is_owner) {
        unsigned const id = TYPE_UID(type);
        bool const keeps_blocks = keeps_its_blocks(type);
        bool const is_fixed = keeps_blocks && !is_behind && is_fixed_class(type);
        auto const known =
            std::find_if(owners.begin(), owners.end(), [id](const OwnerClass& owner) { return owner.id == id; });
        if (known == owners.end()) {
            owners.push_back(OwnerClass{id, &types_behind(type), keeps_blocks ? &state_of(type) : nullptr,
                                        keeps_blocks && has_raw_blocks(type), is_fixed});
        } else {
            known->is_fixed = known->is_fixed && is_fixed;
        }
        if (keeps_blocks) {
            return;
        }
    }
    visit_fields(type, [&](tree field) {
        tree held = without_array(TREE_TYPE(field));
        if (!is_indirect_type(held)) {
            add_owners(held, owners, visits, owns_pointers, is_behind);
        } else if (owns_pointers) {
            add_owners(TREE_TYPE(held), owners, visits, false, true);
        }
    });
    if (is_owner && has_id(types_behind(type), kVoidStorage)) {
        visit_owner_arguments(type, [&](tree argument) { add_owners(argument, owners, visits, false, true); });
    }
}

const std::vector<OwnerClass>& owners_of(tree type) {
    return owner_answers.get(TYPE_UID(type), [type] {
        std::vector<OwnerClass> owners;
        Visits visits;
        add_owners(type, owners, visits, false, false);
        return owners;
    });
}

// Adds the classes with an end that the pointers of TYPE name to SET.
void add_ended_targets(tree type, TypeSet& set, Visits& visits) {
    if (is_indirect_type(type)) {
        tree target = element_class(TREE_TYPE(type));
        if (target != NULL_TREE && CLASS_TYPE_P(target)) {
            bool const is_readable = readable_class(target) != NULL_TREE;
            if (is_readable ? is_owner_class(target) : is_wide_reading) {
                add_id(set, TYPE_UID(target));
            }
        }
        return;
    }
    type = readable_class(element_class(type));
    if (type == NULL_TREE || !visits.enter(type)) {
        return;
    }
    visit_fields(type, [&](tree field) { add_ended_targets(without_array(TREE_TYPE(field)), set, visits); });
}

// Adds TYPE and each part of it by value that has an end to SET.
void add_parts(tree type, TypeSet& set, Visits& visits) {
    type = element_class(type);
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || readable_class(type) == NULL_TREE || !is_owner_class(type)
        || !visits.enter(type)) {
        return;
    }
    add_id(set, TYPE_UID(type));
    visit_fields(type, [&](tree field) { add_parts(TREE_TYPE(field), set, visits); });
}

// Adds each class with an end that an object of TYPE holds in a member of a union
// to SET.
void add_cells(tree type, TypeSet& set, Visits& visits, bool is_in_union) {
    type = readable_class(element_class(type));
    if (type == NULL_TREE || !visits.enter(type, is_in_union)) {
        return;
    }
    if (is_in_union && TREE_CODE(type) == RECORD_TYPE) {
        Visits parts;
        add_parts(type, set, parts);
        return;
    }
    bool const is_union = TREE_CODE(type) == UNION_TYPE;
    visit_fields(type, [&](tree field) { add_cells(TREE_TYPE(field), set, visits, is_union); });
}

// Adds each type that a value of TYPE can hold to SET: its parts by value and its
// type arguments, at each depth.
void add_held(tree type, TypeSet& set) {
    std::vector<tree> pending{type};
    auto add = [&](tree held) {
        held = element_class(held);
        if (held != NULL_TREE && !has_id(set, TYPE_UID(held))) {
            add_id(set, TYPE_UID(held));
            pending.push_back(held);
        }
    };
    while (!pending.empty()) {
        tree held = pending.back();
        pending.pop_back();
        if (!CLASS_TYPE_P(held)) {
            continue;
        }
        visit_type_arguments(held, add);
        // An incomplete class holds nothing here, so a move into it ends the owner.
        if (COMPLETE_TYPE_P(held)) {
            visit_fields(held, [&](tree field) { add(TREE_TYPE(field)); });
        }
    }
}

// True when an object of TYPE holds an object of HELD in its bytes.
bool holds_in_bytes(tree type, tree held, Visits& visits) {
    type = element_class(type);
    if (type == held) {
        return true;
    }
    type = readable_class(type);
    if (type == NULL_TREE || !visits.enter(type)) {
        return false;
    }
    bool is_held = false;
    visit_fields(type, [&](tree field) { is_held = is_held || holds_in_bytes(TREE_TYPE(field), held, visits); });
    return is_held;
}

// True when HELD is a borrow and RESULT can hold a copy of it.
bool is_copy_of_borrow(tree result, tree held) {
    Visits visits;
    return CLASS_TYPE_P(held) && !is_indirect_type(result) && holds_borrow(held)
        && !TYPE_HAS_NONTRIVIAL_DESTRUCTOR(held) && holds_in_bytes(result, held, visits);
}

bool reaches_any(const TypeSet& targets, const TypeSet& contents) {
    return !targets.empty() && (has_id(contents, kErasedType) || have_common_type(targets, contents));
}

BorrowRoute make_route(tree result, tree held) {
    BorrowRoute answer;
    if (result == held && is_owner_class(result)) {
        return answer;
    }
    const TypeSet& targets = targets_of(result);
    if (targets.empty()) {
        return answer;
    }
    bool const is_erased = has_id(targets, kErasedType);
    if (!is_erased && !reaches_any(targets, owned_contents_of(held))) {
        answer.reach = reaches_any(targets, contents_of(held)) ? Reach::through : Reach::none;
        return answer;
    }
    if (is_copy_of_borrow(result, held)) {
        answer.reach = Reach::through;
        return answer;
    }
    answer.reach = Reach::into;
    const TypeSet& contents = contents_of(held);
    bool can_be_raw = true;
    for (unsigned target : targets) {
        can_be_raw = can_be_raw && (target == kErasedType || !has_id(contents, target));
    }
    for (const OwnerClass& owner : owners_of(held)) {
        if (owner.has_raw_blocks || have_common_type(targets, *owner.behind)
            || (owner.state != nullptr && have_common_type(targets, *owner.state))
            || (can_be_raw && has_id(*owner.behind, kRawBytes))) {
            add_id(answer.route, owner.id);
        }
    }
    Visits visits;
    add_ended_targets(result, answer.targets, visits);
    return answer;
}

}  // namespace

void begin_route_reading(bool is_wide) {
    is_wide_reading = is_wide;
    target_answers.clear_open();
    owned_content_answers.clear_open();
    content_answers.clear_open();
    behind_answers.clear_open();
    part_answers.clear_open();
    cell_answers.clear_open();
    held_answers.clear_open();
    owner_answers.clear_open();
    route_answers.clear_open();
    keeps_answers.clear_open();
    raw_block_answers.clear_open();
    state_answers.clear_open();
}

bool have_common_type(const TypeSet& first, const TypeSet& second) {
    auto one = first.begin();
    auto other = second.begin();
    while (one != first.end() && other != second.end()) {
        if (*one == *other) {
            return true;
        }
        if (*one < *other) {
            ++one;
        } else {
            ++other;
        }
    }
    return false;
}

void add_types(TypeSet& set, const TypeSet& added) {
    for (unsigned id : added) {
        add_id(set, id);
    }
}

tree element_class(tree type) {
    type = without_array(type);
    return type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(type) : NULL_TREE;
}

bool is_indirect_type(tree type) {
    return type != NULL_TREE && (TREE_CODE(type) == POINTER_TYPE || TREE_CODE(type) == REFERENCE_TYPE);
}

bool keeps_its_blocks(tree type) {
    type = element_class(type);
    if (type == NULL_TREE || !CLASS_TYPE_P(type)) {
        return false;
    }
    return keeps_answers.get(TYPE_UID(type), [type] { return is_storage_owner(type); });
}

const BorrowRoute& borrow_route(tree result, tree held) {
    static const BorrowRoute none;
    result = result != NULL_TREE ? TYPE_MAIN_VARIANT(result) : NULL_TREE;
    held = element_class(held);
    if (result == NULL_TREE || held == NULL_TREE) {
        return none;
    }
    return route_answers.get(pair_key(result, held), [result, held] { return make_route(result, held); });
}

const TypeSet& ended_parts(tree type) {
    static const TypeSet none;
    type = element_class(type);
    if (type == NULL_TREE || !CLASS_TYPE_P(type)) {
        return none;
    }
    return part_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_parts(type, set, visits);
        return set;
    });
}

const TypeSet& ended_cells(tree type) {
    static const TypeSet none;
    type = element_class(type);
    if (type == NULL_TREE || !RECORD_OR_UNION_TYPE_P(type)) {
        return none;
    }
    return cell_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        Visits visits;
        add_cells(type, set, visits, false);
        return set;
    });
}

bool can_hold_object(tree type, tree held) {
    type = element_class(type);
    held = element_class(held);
    if (type == NULL_TREE || held == NULL_TREE) {
        return false;
    }
    if (type == held) {
        return true;
    }
    if (!CLASS_TYPE_P(type)) {
        return false;
    }
    const TypeSet& types = held_answers.get(TYPE_UID(type), [type] {
        TypeSet set;
        add_held(type, set);
        return set;
    });
    return has_id(types, TYPE_UID(held));
}

const std::vector<OwnerClass>& owner_classes(tree type) {
    static const std::vector<OwnerClass> none;
    type = element_class(type);
    if (type == NULL_TREE || !RECORD_OR_UNION_TYPE_P(type)) {
        return none;
    }
    return owners_of(type);
}

}  // namespace crucible_plugin
