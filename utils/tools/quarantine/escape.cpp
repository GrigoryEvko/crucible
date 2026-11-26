// The family view escape: the rule view-escape.  A function returns a
// borrow, and the borrow names an object that ends with the frame of the
// function: a local, a parameter by value or a temporary.  A read of the
// result then reads a dead stack frame.  The rule refuses each such return.
//
// Borrow
//   A type holds a borrow when it is a reference or a pointer, or a class that
//   states refers{}, refers_unique{} or refers_deep{} of
//   include/foundation/Transfer.h, such as a View or a Ref, at each depth of
//   its bases and its members.  A class of the base that states by_arguments
//   owns an object of each type argument that the mark names, as a Box does,
//   so the walk reads those arguments and no member.  The rule trusts the mark
//   only in a class of the base, and reads the members of a marked class
//   outside it.  A class that states payload_cell{} holds the object of its
//   first type argument in its own bytes, as an Option does, so the walk reads
//   that argument too.  A class that states one of the three refers marks names
//   its target: the type that its statement names, as refers_deep{^^T} names T,
//   or else its first type argument (borrow_target of plugin.cpp).  A class of the
//   base that states owns_its_storage{} holds no borrow: its pointers name its
//   own storage.  So a Box of an arena holds no borrow, and the object of a
//   door of the Box is the Box (the marked owners).  A class of the base that
//   states refers_static{}, such as a StaticText, holds no borrow: its pointers
//   name static storage (the static texts).  The rule reads each function whose
//   return type holds a borrow.
//
// Origins
//   The origins of an expression are the objects that its borrow can name.
//   Each borrow of the base comes from a door of the base: to(), erase_brand(),
//   view(), get(), append(), and the constructors of a View and a Ref.  A
//   function or a constructor outside the base, a helper or a closure, can pass
//   such a borrow on.  So the rule follows a call of each function:
//     * an argument by reference or by pointer names an object.  When the type
//       of the object holds a borrow, the result can carry that borrow on, as
//       expect() of a Result of a Ref does, so the origins are the origins of
//       the object.  Each other object, such as an owner or an int, is the
//       object that the result borrows
//     * an argument by value whose type holds a borrow gives its own origins,
//       as the Ref of erase_brand(ref) does
//     * a constructor builds the object of its first argument, which is no
//       origin
//   A function of the namespace std that gives a reference, such as
//   std::move or std::as_const, gives the object of its first argument.  A
//   local that holds a borrow has the origins of its initializer and of each
//   assignment of it or of its members, and a local reference names the object
//   of its initializer.  A member of an object has the origins of the object,
//   and a name of a structured binding has the origins of the member that it
//   reads.  A conversion, a conditional, a comma and a temporary pass their
//   origins on.
//
// Runs outside
//   A region of fixy/OwnedRegion.h holds a run in storage that it does not
//   own, such as an arena: its state states releases_nothing of
//   include/foundation/core/Owner.h, so the end of its core is trivial.  Only
//   the walk of the stores reads this property (OutsideRuns), and only on an
//   owner of the namespace foundation or fixy (outside_run_core): no element
//   of the run is a part of the region, and a run that a door takes from a
//   region names no region, also at a parameter that CRUCIBLE_LIFETIMEBOUND
//   marks.  So a constructor that consumes a region keeps its run, as
//   fixy/SharedRegion.h does.  The rule does not follow the arena of a region,
//   and the arena can be a local of the same frame.  So the walk of the
//   returns keeps the region as the origin of its run, and the return of a
//   run of a local region stays a finding.
//
// Marked owners
//   A parameter that CRUCIBLE_LIFETIMEBOUND marks states that the result
//   borrows its argument.  So the object of an argument at a marked parameter
//   of a door of the base is an origin of the call, also when the result names
//   no part of its type.  An arena gives a Ref of an int, and it holds no int.
//   The rule reads the mark only for an owner: an object that holds no borrow,
//   or a class that states owns_its_storage{} of
//   include/foundation/core/Owner.h, whose pointers name its own storage.  The
//   result of a door that takes a View or a Ref borrows the target of that
//   borrow, which the origins above follow.  An arena that a Box holds is the
//   object of the door operator-> of the Box, so a Ref of such an arena has
//   the Box as its origin.  Only a class of the namespace foundation or fixy
//   drops its pointers from the borrow this way, and the rule base-namespace
//   keeps each such class in the base.
//
// Finding
//   escaped-borrow   a return whose value has an origin that ends with the
//                    frame: a local of automatic storage, a parameter by
//                    value, a temporary of the full expression, a parameter
//                    by an rvalue reference to a value that holds no borrow,
//                    or a capture by value of a closure
//   stored-borrow    a store of a value with such an origin into an object
//                    that outlives the frame (the stores)
//   The entity is the object.  A parameter by an rvalue reference is often a
//   forwarding T&& that deduced a value: the caller gave a temporary, which
//   ends with the full expression of the call.  A result by an rvalue
//   reference forwards such a parameter on, as get of an rvalue Record does,
//   so it is no finding.  The rule reads the call operator of a closure as a
//   function, and a capture by value lives in the closure object, which a
//   call of a temporary closure ends with the full expression.  A function that returns a borrow of a
//   parameter by reference names an object of its caller, which is no
//   finding.  The kind missing-mark of the rule lifetime-twin asks such a
//   function to mark the parameter, so the caller cannot give a temporary.
//
// Limits
//   The rule reads one body.  A call reads the signature of its callee and not
//   its body, so a function that gives back a borrow of a second argument by
//   reference, or of an object that no argument names, hides that origin.  A
//   borrow that passes through a field of a closure that the function keeps,
//   or through a container of borrows, gives no origin.  The two exceptions of
//   the linear design hold: a function returns
//   a borrow of a parameter that it marks, and a class that cannot move or
//   copy holds a view of a member that it declares first.  Neither of the two
//   meets a return or a store of a borrow of a local.  A refers mark whose
//   statement names no type that the plugin can read, such as the unknown type
//   of an unresolved overloaded function, names the first type argument as its
//   target.
//
// Open bodies
//   A body can name an owner of a class that is incomplete there, and the unit
//   can complete the class later.  The rule cannot tell whether such an owner
//   holds a borrow, so it reads the body again at the end of the unit, on a copy
//   (the kept bodies of plugin.cpp).  Only the last reading gives findings.
//
// Stores
//   A function can also keep a borrow past its frame with no return: it
//   stores the borrow into an object that outlives the frame.  Such an object
//   is the object of a parameter by reference, a member of the object of a
//   member function or of a constructor, or an object of static or thread
//   storage, through its members and its elements.  The rule reads each
//   assignment and initialization of such an object, each call of its
//   assignment operator, and each constructor that builds it in place, when
//   its type holds a borrow.  The stored value has the origins of a returned
//   value.  A store through a capture by reference of a closure, through a
//   local pointer or through a local reference names no such object, so the
//   rule does not read it.
//
// Statics
//   An object of static or thread storage duration lives past each frame, so
//   a borrow in it can name an object that ends first, whatever its brand: a
//   store can come from each function of the program, also through a member
//   function of the object, which the walk of the stores does not see.  So an
//   object of static or thread storage duration whose type holds a borrow
//   type of the base is the kind static-borrow: a variable of namespace
//   scope, a static data member and a static local, also a thread_local one.
//   A borrow type is a class that states refers, refers_unique or
//   refers_deep of foundation/Transfer.h.  The walk reads the bases and the
//   fields of a class and the type arguments that an owner holds, as the walk
//   of the borrow does, and it reads no pointer and no reference, which the
//   rule quarantine counts.  Two borrows name an object of static storage,
//   and the kind admits them:
//     * a borrow type whose template arguments name StaticStorageBrand of
//       foundation/core/Ref.h, such as the Ref of ref_of_static, whose door
//       proves the static storage of its object
//     * a borrow in a const object whose constant initializer holds only
//       addresses of objects of static storage, or no address.  The compiler
//       fixes such an address, and no store can change a const object.
//   A const object with a dynamic initializer is a finding: its initializer
//   runs a function whose body the rule does not read.
//
// Retirement
//   A View with a brand or a lifetime parameter would carry this fact in its
//   type, as a borrow of Rust carries its lifetime.  C++ gives neither today,
//   so the plugin holds it.
//
// Errors
//   The census of the tree, the parse presets and the units of bench-fuzz,
//   gave no finding.  So each finding is a compile error in each build and each
//   mode (is_each_mode_kind of plugin.h), and an opt-out region covers it.  The
//   message does not start with "quarantine: ", so the driver of the negative
//   fixtures keeps it.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The deepest walk of a type or an expression.
constexpr int kEscapeDepth = 32;

// The marks of the borrow of the borrow.
struct EscapeNames {
    tree transfer = NULL_TREE;
    tree refers = NULL_TREE;
    tree refers_unique = NULL_TREE;
    tree refers_deep = NULL_TREE;
    tree owns_by_number = NULL_TREE;
    tree refers_by_number = NULL_TREE;
    tree refers_static = NULL_TREE;
    tree payload_cell = NULL_TREE;
    tree foundation = NULL_TREE;
    tree fixy = NULL_TREE;
    tree std_name = NULL_TREE;
};

EscapeNames escape_names;

// The answers of holds_borrow (ReachWalk).
ReachAnswers borrow_classes;

bool carries_mark(tree type, tree name) {
    return carries_base_annotation(TYPE_ATTRIBUTES(type), name, escape_names.transfer);
}

// True for a type or a declaration of the namespace foundation or fixy, or of
// a namespace inside them.  The rule base-namespace refuses a class of these
// namespaces in a file outside the base.
bool is_of_base_namespace(tree entity) {
    tree top = top_namespace(entity);
    return top != NULL_TREE && top != global_namespace
        && (DECL_NAME(top) == escape_names.foundation || DECL_NAME(top) == escape_names.fixy);
}

// True for a class of the namespace foundation or fixy with a base OwnerCore
// of include/foundation/core/Owner.h, the template of the base: an owner of
// the base that holds its storage alone.  The core asks that its Owner is a
// class of the two namespaces, so a class outside the base is no owner on the
// core, and neither is a class on a template of another namespace with the
// name OwnerCore.
bool is_owner_core_class(tree type) {
    static tree const owner_core = get_identifier("OwnerCore");
    if (!is_of_base_namespace(type)) {
        return false;
    }
    tree binfo = TYPE_BINFO(type);
    for (unsigned index = 0; binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        tree base = BINFO_TYPE(BINFO_BASE_BINFO(binfo, index));
        tree info = base != NULL_TREE && CLASS_TYPE_P(base) ? CLASSTYPE_TEMPLATE_INFO(base) : NULL_TREE;
        if (info != NULL_TREE && DECL_NAME(TI_TEMPLATE(info)) == owner_core
            && is_of_base_namespace(TI_TEMPLATE(info))) {
            return true;
        }
    }
    return false;
}

// The base OwnerCore of include/foundation/core/Owner.h of an owner of the
// namespace foundation or fixy whose core releases nothing at its end, or
// null.  The state of such a core states releases_nothing (ReleasesNothing),
// so the end of the core is trivial.  Such an owner, a region, holds a run in
// storage that it does not own, such as an arena.  No element of the run is
// a part of the owner, and the end of the owner ends no element (the runs
// outside).
tree outside_run_core(tree type) {
    static tree const owner_core = get_identifier("OwnerCore");
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type) || !is_of_base_namespace(type)) {
        return NULL_TREE;
    }
    tree binfo = TYPE_BINFO(type);
    for (unsigned index = 0; binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        tree base = BINFO_TYPE(BINFO_BASE_BINFO(binfo, index));
        tree info = base != NULL_TREE && CLASS_TYPE_P(base) ? CLASSTYPE_TEMPLATE_INFO(base) : NULL_TREE;
        if (info != NULL_TREE && DECL_NAME(TI_TEMPLATE(info)) == owner_core
            && is_of_base_namespace(TI_TEMPLATE(info))) {
            return COMPLETE_TYPE_P(base) && !TYPE_HAS_NONTRIVIAL_DESTRUCTOR(base) ? base : NULL_TREE;
        }
    }
    return NULL_TREE;
}

// True for a class that states owns_its_storage{} of
// include/foundation/core/Owner.h: its pointers name its own storage, and no
// door of it stores a borrow that a caller gives (the marked owners).
bool owns_its_storage(tree type) {
    static tree const mark = get_identifier("owns_its_storage");
    static tree const core = get_identifier("core");
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type) && carries_base_annotation(TYPE_ATTRIBUTES(type), mark, core);
}

// True for a class of the namespace foundation or fixy that states
// owns_its_storage{} (the borrow).  The rule base-namespace refuses a class of
// these namespaces in a file outside the base, so only the base states the
// mark in this form.
bool is_base_storage_owner(tree type) { return owns_its_storage(type) && is_of_base_namespace(type); }

// True for a class of the namespace foundation or fixy that states
// keeps_its_blocks{} of include/foundation/core/Owner.h: each member that is not
// const keeps each block.  The mark states nothing about the borrows that the
// class holds.  The rule trusts it only in the base, as it trusts
// owns_its_storage{}.
bool is_base_block_keeper(tree type) {
    static tree const mark = get_identifier("keeps_its_blocks");
    static tree const core = get_identifier("core");
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type) && carries_base_annotation(TYPE_ATTRIBUTES(type), mark, core)
        && is_of_base_namespace(type);
}

// True when TYPE holds a borrow, in the walk WALK of holds_borrow.
bool reaches_borrow(tree type, ReachWalk& walk, int depth) {
    if (type == NULL_TREE || type == error_mark_node) {
        return false;
    }
    if (depth > kEscapeDepth) {
        walk.reach_unread_part();
        return false;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE) {
        return TREE_CODE(TREE_TYPE(type)) != FUNCTION_TYPE && TREE_CODE(TREE_TYPE(type)) != METHOD_TYPE;
    }
    if (TREE_CODE(type) == ARRAY_TYPE) {
        return reaches_borrow(TREE_TYPE(type), walk, depth + 1);
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return false;
    }
    ReachWalk::Step const step = walk.enter(type);
    if (step != ReachWalk::Step::read) {
        return step == ReachWalk::Step::yes;
    }
    // The pointers of an owner of its storage in the base, such as an arena,
    // name its own blocks, so the class holds no borrow.  The pointers of a class
    // of the base that states refers_static{}, such as a StaticText, name static
    // storage, so it holds no borrow either.
    if (is_base_storage_owner(type) || names_static_storage(type)) {
        return walk.leave(type, false);
    }
    bool const is_borrow = carries_mark(type, escape_names.refers) || carries_mark(type, escape_names.refers_unique)
                        || carries_mark(type, escape_names.refers_deep);
    bool is_held = is_borrow;
    // An owner holds its pointers, and a value of each type argument that it
    // owns, so the walk reads those arguments and no member.
    OwnedPositions const owned = held_owner_arguments(type);
    if (owned.is_stated) {
        visit_owned_arguments(type, owned,
                              [&](tree argument) { is_held = is_held || reaches_borrow(argument, walk, depth + 1); });
        return walk.leave(type, is_held);
    }
    tree binfo = TYPE_BINFO(type);
    for (unsigned index = 0; !is_held && binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        is_held = reaches_borrow(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), walk, depth + 1);
    }
    for (tree field = TYPE_FIELDS(type); !is_held && field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)) {
            is_held = reaches_borrow(TREE_TYPE(field), walk, depth + 1);
        }
    }
    return walk.leave(type, is_held);
}

// How the reading in progress takes an open question about a class (the kept
// bodies of plugin.cpp): as a "no", or as the two answers at once.  A wide
// reading gives each origin that an answer of each open question can give.  So a
// body with no finding and no borrowed parameter in it needs no second reading.
enum class OpenAnswer : std::uint8_t {
    no,
    each,
};

OpenAnswer open_answer = OpenAnswer::no;

// True after a wide reading that can note a borrowed parameter.
bool has_wide_parameter = false;

// The answer of the question whether a type holds a borrow (the borrow).  A wide
// reading takes an open question as the two answers.
struct BorrowAnswer {
    bool can_hold = false;
    bool can_hold_none = false;
    bool is_open = false;
};

// Complexity: linear in the classes that the type reaches (ReachWalk).
BorrowAnswer borrow_answer(tree type) {
    ReachWalk walk(borrow_classes);
    bool const holds = reaches_borrow(type, walk, 0);
    bool const is_open = !holds && walk.is_open();
    return BorrowAnswer{holds || (is_open && open_answer == OpenAnswer::each), !holds, is_open};
}

// True when TYPE can hold a borrow, and true when it can hold none.
bool can_hold_borrow(tree type) { return borrow_answer(type).can_hold; }
bool can_hold_no_borrow(tree type) { return borrow_answer(type).can_hold_none; }

// True for a function of the namespace std that gives a reference.
bool is_standard_reference(tree function) {
    tree top = top_namespace(function);
    tree type = TREE_TYPE(TREE_TYPE(function));
    return top != global_namespace && DECL_NAME(top) == escape_names.std_name && type != NULL_TREE
        && TREE_CODE(type) == REFERENCE_TYPE;
}

// The deepest walk of a type that could name something.
constexpr int kNameDepth = 6;

// Adds the types that a borrow of TYPE names: the element of a reference or a
// pointer, the target of a class with a refers mark (borrow_target of
// plugin.cpp), and the targets of each part of a class that holds a borrow, such
// as an Option of a Ref.  A class with an open answer can name each type in a
// wide reading, which the target null stands for.  Complexity: linear in the
// parts of the type, to the depth kNameDepth.
void add_borrow_targets(tree type, std::vector<tree>& out, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > kNameDepth) {
        return;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE) {
        out.push_back(TYPE_MAIN_VARIANT(TREE_TYPE(type)));
        return;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return;
    }
    BorrowAnswer const answer = borrow_answer(type);
    if (!answer.can_hold) {
        return;
    }
    if (answer.is_open) {
        out.push_back(NULL_TREE);
        return;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    bool const is_borrow = carries_mark(type, escape_names.refers) || carries_mark(type, escape_names.refers_unique)
                        || carries_mark(type, escape_names.refers_deep);
    if (is_borrow) {
        tree const target = borrow_target(type);
        if (target != NULL_TREE) {
            out.push_back(TYPE_MAIN_VARIANT(target));
        }
        return;
    }
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument)) {
            add_borrow_targets(argument, out, depth + 1);
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)) {
            add_borrow_targets(TREE_TYPE(field), out, depth + 1);
        }
    }
}

// How a walk of the origins reads an owner whose run lives outside it (the
// runs outside): the walk of the stores reads that property, and the walk of
// the returns ignores it, so it stays closed for a run in a local arena.
enum class OutsideRuns : std::uint8_t {
    ignore,
    read,
};

// True when WHOLE holds a part of the type PART, in the walk of holds_part.
bool reaches_part(tree whole, tree part, int depth, OutsideRuns runs) {
    if (whole == NULL_TREE || whole == error_mark_node || depth > kNameDepth) {
        return false;
    }
    whole = TYPE_MAIN_VARIANT(whole);
    if (whole == part) {
        return true;
    }
    if (TREE_CODE(whole) == ARRAY_TYPE) {
        return reaches_part(TREE_TYPE(whole), part, depth + 1, runs);
    }
    if (!CLASS_TYPE_P(whole)) {
        return false;
    }
    if (!COMPLETE_TYPE_P(whole)) {
        ++unread_parts;
        return false;
    }
    tree const outside_core = runs == OutsideRuns::read ? outside_run_core(whole) : NULL_TREE;
    tree info = CLASSTYPE_TEMPLATE_INFO(whole);
    tree arguments =
        info != NULL_TREE && outside_core == NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument) && reaches_part(argument, part, depth + 1, runs)) {
            return true;
        }
    }
    tree binfo = TYPE_BINFO(whole);
    for (unsigned index = 0; binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        if (BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)) == outside_core) {
            continue;
        }
        if (reaches_part(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), part, depth + 1, runs)) {
            return true;
        }
    }
    for (tree field = TYPE_FIELDS(whole); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)
            && reaches_part(TREE_TYPE(field), part, depth + 1, runs)) {
            return true;
        }
    }
    return false;
}

// The answers of holds_part for each reading of the runs, by the TYPE_UID of the
// whole and of the part, which no collection reuses: the final answers, and the
// open "no" answers of the body in progress, where no class becomes complete.
std::unordered_map<std::uint64_t, bool> part_answers[2];
std::unordered_set<std::uint64_t> open_parts[2];

// True when WHOLE holds a part of the type PART: the type itself, the element
// of an array, a base, a member, or a type argument of a class template, which
// an owner holds in its bytes or on the heap.  With RUNS read, an owner whose
// run lives outside it (outside_run_core) holds no part through its type
// arguments or its core, which name the elements of the run.  A "no" is final
// when the walk read each part, and at the end of the unit.  Complexity: linear
// in the parts of the type, to the depth kNameDepth, one time for each pair with
// a final answer, and one time in each body for each other pair.
bool holds_part(tree whole, tree part, OutsideRuns runs) {
    if (whole == NULL_TREE || whole == error_mark_node || part == NULL_TREE) {
        return false;
    }
    std::uint64_t const key =
        (static_cast<std::uint64_t>(TYPE_UID(TYPE_MAIN_VARIANT(whole))) << 32) | TYPE_UID(TYPE_MAIN_VARIANT(part));
    std::unordered_map<std::uint64_t, bool>& answers = part_answers[static_cast<std::size_t>(runs)];
    std::unordered_set<std::uint64_t>& open = open_parts[static_cast<std::size_t>(runs)];
    auto const found = answers.find(key);
    if (found != answers.end()) {
        return found->second;
    }
    if (open.count(key) != 0) {
        return open_answer == OpenAnswer::each;
    }
    unsigned const first_unread = unread_parts;
    bool const is_held = reaches_part(whole, part, 0, runs);
    if (is_held || unread_parts == first_unread || state.is_unit_parsed) {
        answers.emplace(key, is_held);
        return is_held;
    }
    open.insert(key);
    return open_answer == OpenAnswer::each;
}

// How a call can lend an object that an argument names (the origins).
enum class Lend : std::uint8_t {
    object,  // std::move and a local reference: the object itself
    part,  // a door: an object that holds a target of the result
    exact,  // a constructor: an object of a target of the result
    forward_only,  // the borrows that the object holds, and not the object
};

// True when a borrow of the type RESULT can name an object of the type
// OBJECT: an object of a target of the result for Lend::exact, or an object
// that holds a target for Lend::part.  A door that gives a View of ints
// borrows an owner of ints, and not a key, a flag or an Option of an index.
bool could_name(tree result, tree object, Lend lend, OutsideRuns runs) {
    std::vector<tree> targets;
    add_borrow_targets(result, targets, 0);
    tree const bare = object != NULL_TREE ? TYPE_MAIN_VARIANT(object) : NULL_TREE;
    for (tree target : targets) {
        if (target == NULL_TREE || (lend == Lend::exact ? bare == target : holds_part(object, target, runs))) {
            return true;
        }
    }
    return false;
}

// True when a borrow of the type RESULT can carry on a borrow that the type
// HELD holds: the result names a part of a target of HELD, as a window of a
// View names a part of its run, or erase_brand gives the target of its Ref.
bool could_forward(tree result, tree held, OutsideRuns runs) {
    std::vector<tree> results;
    std::vector<tree> helds;
    add_borrow_targets(result, results, 0);
    add_borrow_targets(held, helds, 0);
    for (tree result_target : results) {
        for (tree held_target : helds) {
            if (held_target == NULL_TREE || result_target == NULL_TREE
                || holds_part(held_target, result_target, runs)) {
                return true;
            }
        }
    }
    return false;
}

tree strip_conversions(tree node);

// The object that a call of a function of std that gives a reference names,
// such as std::move(x) or std::as_const(x): the object of its first argument.
// Null for each other call.
tree standard_reference_object(tree call) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || !is_standard_reference(callee)
        || argument_count(call) < 1) {
        return NULL_TREE;
    }
    tree argument = strip_conversions(argument_at(call, 0));
    if (argument != NULL_TREE && TREE_CODE(argument) == ADDR_EXPR) {
        return TREE_OPERAND(argument, 0);
    }
    return argument;
}

tree strip_conversions(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kEscapeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case SAVE_EXPR:
            case CLEANUP_POINT_EXPR:
            case EXPR_STMT:
                node = TREE_OPERAND(node, 0);
                continue;
            default:
                return node;
        }
    }
    return node;
}

// The origins of the expressions of one function (the origins).
class OriginWalk {
public:
    OriginWalk(tree function, OutsideRuns runs) : function_{function}, runs_{runs} {}

    // True for an object that ends with the frame of the function: a local, a
    // parameter by value, a parameter by an rvalue reference to a value that
    // holds no borrow, and a capture by value of a closure (the finding).
    bool ends_with_frame(tree decl) const {
        if (TREE_CODE(decl) == PARM_DECL) {
            tree type = TREE_TYPE(decl);
            if (is_this_parameter(decl)) {
                return false;
            }
            return TREE_CODE(type) != REFERENCE_TYPE
                || (TYPE_REF_IS_RVALUE(type) && can_hold_no_borrow(TREE_TYPE(type)));
        }
        if (!VAR_P(decl) || TREE_STATIC(decl) || DECL_EXTERNAL(decl) || DECL_THREAD_LOCAL_P(decl)
            || decl_function_context(decl) != function_ || TREE_CODE(TREE_TYPE(decl)) == REFERENCE_TYPE) {
            return false;
        }
        return !is_capture_proxy(decl) || is_value_capture(decl);
    }

    // True for the proxy of a capture by value: its value is a field of the
    // closure that is no reference.  A capture by reference names an object of
    // the caller.
    static bool is_value_capture(tree proxy) {
        tree value = tree_strip_any_location_wrapper(DECL_VALUE_EXPR(proxy));
        if (value == NULL_TREE || TREE_CODE(value) != COMPONENT_REF) {
            return false;
        }
        tree field = TREE_OPERAND(value, 1);
        return TREE_CODE(field) == FIELD_DECL && TREE_CODE(TREE_TYPE(field)) != REFERENCE_TYPE;
    }

    // Records the origins of each local that holds a borrow, and the object of
    // each local reference.  A local can take its origins from a later local,
    // so the walk runs until no set grows, at most three times.
    void record_locals(tree body) {
        for (int round = 0; round < 3; ++round) {
            is_grown_ = false;
            cp_walk_tree_without_duplicates(&body, record_node, this);
            if (!is_grown_) {
                return;
            }
        }
    }

    // The origins of the value of EXPRESSION.
    void add_origins(tree expression, std::vector<tree>& out, int depth) {
        expression = strip_conversions(expression);
        if (expression == NULL_TREE || depth > kEscapeDepth) {
            return;
        }
        switch (TREE_CODE(expression)) {
            case TARGET_EXPR:
                add_origins(TARGET_EXPR_INITIAL(expression), out, depth + 1);
                return;
            case COMPOUND_EXPR:
                add_origins(TREE_OPERAND(expression, 1), out, depth + 1);
                return;
            case COND_EXPR:
                add_origins(TREE_OPERAND(expression, 1), out, depth + 1);
                add_origins(TREE_OPERAND(expression, 2), out, depth + 1);
                return;
            case INIT_EXPR:
            case MODIFY_EXPR:
                add_origins(TREE_OPERAND(expression, 1), out, depth + 1);
                return;
            case ADDR_EXPR: {
                // A capture by reference of an object that holds a borrow, as
                // in a closure, carries on the borrows of the object.
                tree object = TREE_OPERAND(expression, 0);
                BorrowAnswer const answer = borrow_answer(TREE_TYPE(object));
                if (answer.can_hold) {
                    add_origins(object, out, depth + 1);
                }
                if (answer.can_hold_none) {
                    add_object(object, out, depth + 1);
                }
                return;
            }
            case INDIRECT_REF: {
                // A read through the address of a local, or through a local
                // reference, has the origins of the object that it reads.
                tree operand = strip_conversions(TREE_OPERAND(expression, 0));
                if (operand != NULL_TREE && TREE_CODE(operand) == ADDR_EXPR) {
                    add_origins(TREE_OPERAND(operand, 0), out, depth + 1);
                } else if (operand != NULL_TREE && VAR_P(operand) && TREE_CODE(TREE_TYPE(operand)) == REFERENCE_TYPE) {
                    auto found = reference_objects_.find(DECL_UID(operand));
                    if (found != reference_objects_.end()) {
                        std::vector<tree> const referents = found->second;
                        for (tree referent : referents) {
                            add_origins(referent, out, depth + 1);
                        }
                    }
                }
                return;
            }
            case PARM_DECL:
                // A parameter by reference as a value names its object, as a
                // reference result of it does.
                if (TREE_CODE(TREE_TYPE(expression)) == REFERENCE_TYPE
                    && (reads_reference_parameters_ || ends_with_frame(expression))) {
                    out.push_back(expression);
                }
                return;
            case VAR_DECL:
            case RESULT_DECL: {
                // The front end gives a named return value the RESULT_DECL of
                // the function, so the result holds the origins of the local.
                // A name of a structured binding reads a member of its hidden
                // variable, so it holds the origins of that member.
                auto found = local_origins_.find(DECL_UID(expression));
                bool const is_noted = found != local_origins_.end();
                if (is_noted) {
                    out.insert(out.end(), found->second.begin(), found->second.end());
                }
                // A local reference as a value is the address of its object.
                if (TREE_CODE(TREE_TYPE(expression)) == REFERENCE_TYPE) {
                    add_object(expression, out, depth + 1);
                    return;
                }
                if ((!is_noted || open_answer == OpenAnswer::each) && VAR_P(expression)
                    && DECL_HAS_VALUE_EXPR_P(expression) && !is_capture_proxy(expression)) {
                    add_origins(DECL_VALUE_EXPR(expression), out, depth + 1);
                }
                return;
            }
            case COMPONENT_REF:
                // A member of an object that holds a borrow carries the origins
                // of the object, as a member of a local aggregate does.
                add_origins(TREE_OPERAND(expression, 0), out, depth + 1);
                return;
            case CONSTRUCTOR: {
                unsigned index = 0;
                tree value = NULL_TREE;
                FOR_EACH_CONSTRUCTOR_VALUE(CONSTRUCTOR_ELTS(expression), index, value) {
                    add_origins(value, out, depth + 1);
                }
                return;
            }
            case CALL_EXPR:
            case AGGR_INIT_EXPR: {
                tree object = standard_reference_object(expression);
                if (object != NULL_TREE) {
                    add_origins(object, out, depth + 1);
                } else {
                    add_call_origins(expression, out, depth + 1);
                }
                return;
            }
            default:
                return;
        }
    }

    // The objects that the lvalue OBJECT names.
    void add_object(tree object, std::vector<tree>& out, int depth) {
        object = strip_conversions(object);
        if (object == NULL_TREE || depth > kEscapeDepth) {
            return;
        }
        switch (TREE_CODE(object)) {
            case VAR_DECL: {
                if (TREE_CODE(TREE_TYPE(object)) == REFERENCE_TYPE) {
                    auto found = reference_objects_.find(DECL_UID(object));
                    if (found != reference_objects_.end()) {
                        out.insert(out.end(), found->second.begin(), found->second.end());
                    }
                    return;
                }
                if (ends_with_frame(object)) {
                    out.push_back(object);
                }
                return;
            }
            case PARM_DECL:
                if (ends_with_frame(object)
                    || (reads_reference_parameters_ && TREE_CODE(TREE_TYPE(object)) == REFERENCE_TYPE)) {
                    out.push_back(object);
                }
                return;
            case TARGET_EXPR:
                out.push_back(TARGET_EXPR_SLOT(object));
                return;
            case COMPONENT_REF:
            case ARRAY_REF:
                add_object(TREE_OPERAND(object, 0), out, depth + 1);
                return;
            case INDIRECT_REF: {
                tree operand = strip_conversions(TREE_OPERAND(object, 0));
                if (operand != NULL_TREE && TREE_CODE(operand) == PARM_DECL
                    && TREE_CODE(TREE_TYPE(operand)) == REFERENCE_TYPE
                    && (reads_reference_parameters_ || ends_with_frame(operand))) {
                    out.push_back(operand);
                    return;
                }
                add_origins(TREE_OPERAND(object, 0), out, depth + 1);
                return;
            }
            case CALL_EXPR:
            case AGGR_INIT_EXPR: {
                tree named = standard_reference_object(object);
                if (named != NULL_TREE) {
                    add_object(named, out, depth + 1);
                } else {
                    add_call_origins(object, out, depth + 1);
                }
                return;
            }
            case COMPOUND_EXPR:
                add_object(TREE_OPERAND(object, 1), out, depth + 1);
                return;
            case COND_EXPR:
                add_object(TREE_OPERAND(object, 1), out, depth + 1);
                add_object(TREE_OPERAND(object, 2), out, depth + 1);
                return;
            default:
                return;
        }
    }

    // The origins of a call (the origins).  RESULT is the type that the call
    // makes: the class of a constructor, or the return type.  A function
    // outside the base can pass a borrow on as a door of the base does, so the
    // walk reads the call of each function.
    void add_call_origins(tree call, std::vector<tree>& out, int depth) {
        tree callee = cp_get_callee_fndecl_nofold(call);
        if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
            return;
        }
        int const count = argument_count(call);
        bool const is_constructor = DECL_CONSTRUCTOR_P(callee);
        bool const is_member = DECL_IOBJ_MEMBER_FUNCTION_P(callee) && !is_constructor;
        tree result = is_constructor ? DECL_CONTEXT(callee) : TREE_TYPE(TREE_TYPE(callee));
        bool const is_rvalue_object = is_member && FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(callee));
        // A member lends its object, and each other function lends its first
        // argument by reference that holds a value, as to(object) does.  An
        // object that the call takes by an rvalue reference goes into the
        // result together with each borrow of it, as a Box and a Ref of its
        // object do, so it is no origin of this call.
        bool is_lend_taken = false;
        std::size_t const first_found = out.size();
        std::vector<tree> moved;
        for (int index = is_constructor ? 1 : 0; index < count; ++index) {
            tree argument = argument_at(call, index);
            tree type = argument != NULL_TREE ? TREE_TYPE(argument) : NULL_TREE;
            if (type == NULL_TREE) {
                continue;
            }
            if (!POINTER_TYPE_P(type)) {
                if (can_hold_borrow(type) && could_forward(result, type, runs_)) {
                    add_named_origins(argument, result, out, depth + 1);
                }
                continue;
            }
            bool const is_moved =
                (index == 0 && is_rvalue_object) || (TREE_CODE(type) == REFERENCE_TYPE && TYPE_REF_IS_RVALUE(type));
            bool const may_lend = !is_moved && (is_member ? index == 0 : !is_lend_taken);
            Lend const lend = !may_lend ? Lend::forward_only : (is_constructor ? Lend::exact : Lend::part);
            bool const is_value = add_reference_argument(argument, lend, result, out, depth + 1);
            is_lend_taken = is_lend_taken || is_value;
            if (is_moved) {
                add_object(argument_object(argument), moved, depth + 1);
            } else if (is_marked_owner_argument(callee, index, argument)) {
                add_object(argument_object(argument), out, depth + 1);
            }
        }
        // An answer can take an object out of the moved ones, so a wide reading
        // keeps each origin.
        if (open_answer == OpenAnswer::each) {
            return;
        }
        out.erase(std::remove_if(
                      out.begin() + static_cast<std::ptrdiff_t>(first_found), out.end(),
                      [&moved](tree origin) { return std::find(moved.begin(), moved.end(), origin) != moved.end(); }),
                  out.end());
    }

    // True when the argument at INDEX of a call of CALLEE names an owner at a
    // parameter that CRUCIBLE_LIFETIMEBOUND marks (the marked owners).  The
    // index counts the object parameter of a member first, as the arguments
    // of the call do.  In the walk of the stores, an owner whose run lives
    // outside it (the runs outside) is no such owner: the run that the call
    // gives names the storage of the run, and the end of the owner ends no
    // element of it.
    bool is_marked_owner_argument(tree callee, int index, tree argument) const {
        tree parameter = DECL_ARGUMENTS(callee);
        for (int position = 0; parameter != NULL_TREE && position < index; ++position) {
            parameter = DECL_CHAIN(parameter);
        }
        if (parameter == NULL_TREE || TREE_CODE(parameter) != PARM_DECL || !is_lifetime_bound_parameter(parameter)) {
            return false;
        }
        tree stripped = strip_conversions(argument);
        if (stripped == NULL_TREE || TREE_TYPE(stripped) == NULL_TREE || !POINTER_TYPE_P(TREE_TYPE(stripped))) {
            return false;
        }
        tree object_type = TREE_TYPE(TREE_TYPE(stripped));
        bool const is_run_outside = runs_ == OutsideRuns::read && outside_run_core(object_type) != NULL_TREE;
        return object_type != NULL_TREE && !is_run_outside
            && (can_hold_no_borrow(object_type) || owns_its_storage(object_type));
    }

    // The object that an argument by reference names: the operand of an
    // address, through std::move and its kin.
    static tree argument_object(tree argument) {
        tree stripped = strip_conversions(argument);
        for (int depth = 0; stripped != NULL_TREE && depth < kEscapeDepth; ++depth) {
            if (TREE_CODE(stripped) == ADDR_EXPR) {
                return TREE_OPERAND(stripped, 0);
            }
            tree named = (TREE_CODE(stripped) == CALL_EXPR || TREE_CODE(stripped) == AGGR_INIT_EXPR)
                           ? standard_reference_object(stripped)
                           : NULL_TREE;
            if (named == NULL_TREE) {
                return stripped;
            }
            stripped = strip_conversions(named);
        }
        return stripped;
    }

    // The origins of EXPRESSION that a borrow of RESULT can name: an argument
    // such as a closure carries on the borrows of many objects, and the result
    // names only an object that holds a target of its borrow.
    void add_named_origins(tree expression, tree result, std::vector<tree>& out, int depth) {
        std::vector<tree> found;
        add_origins(expression, found, depth);
        for (tree origin : found) {
            tree type = origin != NULL_TREE ? TREE_TYPE(origin) : NULL_TREE;
            if (type != NULL_TREE && TREE_CODE(type) == REFERENCE_TYPE) {
                type = TREE_TYPE(type);
            }
            // An owner of its storage, such as an arena, holds objects of each
            // type, so it can hold each target of the result (the marked
            // owners).
            bool const holds_each_type = type != NULL_TREE && owns_its_storage(type);
            if (origin != NULL_TREE && (holds_each_type || could_name(result, type, Lend::part, runs_))) {
                out.push_back(origin);
            }
        }
    }

    // The origins of an argument by reference or by pointer, under LEND: the
    // borrows that the object holds, when RESULT can carry them on, and the
    // object itself when LEND and the type of the object let the result name
    // it (could_name).  The object of the address of an lvalue is that lvalue,
    // the object of a reference is its referent, and the object of a pointer
    // value is what the pointer names, whose origins are the origins of the
    // value.  An object of an empty class, such as a key or a tag, holds
    // nothing that a borrow can name, so it is an origin only when the result
    // can name its class.  Returns true when the argument names an object that
    // holds a value.
    bool add_reference_argument(tree argument, Lend lend, tree result, std::vector<tree>& out, int depth) {
        tree stripped = strip_conversions(argument);
        if (stripped == NULL_TREE || TREE_TYPE(stripped) == NULL_TREE || !POINTER_TYPE_P(TREE_TYPE(stripped))) {
            return false;
        }
        tree object_type = TREE_TYPE(TREE_TYPE(stripped));
        // An empty object holds no part that a borrow can name, but a result can name
        // the object itself, as a closure that captures it by reference does.
        if (object_type != NULL_TREE && CLASS_TYPE_P(object_type) && is_empty_class(object_type)
            && !could_name(result, object_type, Lend::exact, runs_)) {
            return false;
        }
        bool const is_address = TREE_CODE(stripped) == ADDR_EXPR;
        bool const is_pointer_value = !is_address && TREE_CODE(TREE_TYPE(stripped)) == POINTER_TYPE;
        if (is_pointer_value) {
            if (lend == Lend::object || could_forward(result, TREE_TYPE(stripped), runs_)) {
                add_origins(stripped, out, depth + 1);
            }
            return true;
        }
        tree object = is_address ? TREE_OPERAND(stripped, 0) : stripped;
        BorrowAnswer const answer = borrow_answer(object_type);
        if (answer.can_hold) {
            if (lend == Lend::object) {
                add_origins(object, out, depth + 1);
            } else if (could_forward(result, object_type, runs_)) {
                add_named_origins(object, result, out, depth + 1);
            }
        }
        if (answer.can_hold_none
            && (lend == Lend::object || (lend != Lend::forward_only && could_name(result, object_type, lend, runs_)))) {
            add_object(object, out, depth + 1);
        }
        // An incomplete class can be empty, so a wide reading also lends the next
        // argument.
        return open_answer == OpenAnswer::no || !CLASS_TYPE_P(object_type) || COMPLETE_TYPE_P(object_type);
    }

private:
    // The callback of cp_walk_tree of record_locals.
    static tree record_node(tree* node, int*, void* data) {
        OriginWalk& walk = *static_cast<OriginWalk*>(data);
        tree current = *node;
        if (TREE_CODE(current) == DECL_EXPR) {
            tree decl = DECL_EXPR_DECL(current);
            if (VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
                walk.note_value(decl, DECL_INITIAL(decl));
            }
        } else if (TREE_CODE(current) == INIT_EXPR || TREE_CODE(current) == MODIFY_EXPR) {
            // An initialization of a member of a local gives its origins to the
            // local, as the front end splits a braced aggregate into one
            // initialization for each member.
            tree target = strip_conversions(TREE_OPERAND(current, 0));
            while (target != NULL_TREE && TREE_CODE(target) == COMPONENT_REF) {
                target = strip_conversions(TREE_OPERAND(target, 0));
            }
            if (target != NULL_TREE && (VAR_P(target) || TREE_CODE(target) == RESULT_DECL)) {
                walk.note_value(target, TREE_OPERAND(current, 1));
            }
        } else if (TREE_CODE(current) == CALL_EXPR || TREE_CODE(current) == AGGR_INIT_EXPR) {
            // A call that builds a class in place writes its slot, as the
            // initializer of a local of a class type does.
            tree slot =
                TREE_CODE(current) == AGGR_INIT_EXPR ? strip_conversions(AGGR_INIT_EXPR_SLOT(current)) : NULL_TREE;
            if (slot != NULL_TREE && (VAR_P(slot) || TREE_CODE(slot) == RESULT_DECL)) {
                walk.note_value(slot, current);
            }
            tree callee = cp_get_callee_fndecl_nofold(current);
            if (callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL && DECL_ASSIGNMENT_OPERATOR_P(callee)
                && argument_count(current) == 2) {
                tree target = strip_conversions(argument_at(current, 0));
                if (target != NULL_TREE && TREE_CODE(target) == ADDR_EXPR) {
                    target = strip_conversions(TREE_OPERAND(target, 0));
                }
                if (target != NULL_TREE && VAR_P(target)) {
                    walk.note_value(target, argument_at(current, 1));
                }
            }
        }
        return NULL_TREE;
    }

    // Adds the origins of VALUE to the local DECL: the borrows of a local
    // that holds a borrow, or the object of a local reference.
    void note_value(tree decl, tree value) {
        if (decl_function_context(decl) != function_ || TREE_STATIC(decl)) {
            return;
        }
        std::vector<tree> found;
        bool const is_reference = TREE_CODE(TREE_TYPE(decl)) == REFERENCE_TYPE;
        if (is_reference) {
            add_reference_argument(value, Lend::object, NULL_TREE, found, 0);
        } else if (can_hold_borrow(TREE_TYPE(decl))) {
            add_origins(value, found, 0);
        } else {
            return;
        }
        std::vector<tree>& held = is_reference ? reference_objects_[DECL_UID(decl)] : local_origins_[DECL_UID(decl)];
        for (tree origin : found) {
            if (std::find(held.begin(), held.end(), origin) == held.end()) {
                held.push_back(origin);
                is_grown_ = true;
            }
        }
    }

public:
    // True when the walk gives each parameter by reference that an origin
    // names too, for the borrowed parameters, and not only the objects that
    // end with the frame.
    bool reads_reference_parameters_ = false;

private:
    tree function_;
    // How the walk reads an owner whose run lives outside it.
    OutsideRuns runs_;
    bool is_grown_ = false;
    std::unordered_map<unsigned, std::vector<tree>> local_origins_;
    std::unordered_map<unsigned, std::vector<tree>> reference_objects_;
};

// The data of the walk of the returns of one function.
struct ReturnWalk {
    OriginWalk* origins = nullptr;
    tree function = NULL_TREE;
};

// The text of an object of a finding.
std::string object_text(tree decl) {
    if (DECL_NAME(decl) != NULL_TREE && VAR_P(decl) && is_capture_proxy(decl)) {
        return "the capture " + identifier_text(DECL_NAME(decl));
    }
    if (DECL_ARTIFICIAL(decl) || DECL_NAME(decl) == NULL_TREE) {
        return "a temporary";
    }
    return (TREE_CODE(decl) == PARM_DECL ? "the parameter " : "the local ") + identifier_text(DECL_NAME(decl));
}

// True for a parameter by an rvalue reference.
bool is_rvalue_parameter(tree decl) {
    return TREE_CODE(decl) == PARM_DECL && TREE_CODE(TREE_TYPE(decl)) == REFERENCE_TYPE
        && TYPE_REF_IS_RVALUE(TREE_TYPE(decl));
}

// The callback of cp_walk_tree over the returns of a function (the finding).
tree check_return(tree* node, int*, void* data) {
    ReturnWalk& walk = *static_cast<ReturnWalk*>(data);
    tree current = *node;
    if (TREE_CODE(current) != RETURN_EXPR || TREE_OPERAND(current, 0) == NULL_TREE) {
        return NULL_TREE;
    }
    std::vector<tree> found;
    walk.origins->add_origins(TREE_OPERAND(current, 0), found, 0);
    // A result by an rvalue reference forwards a parameter by an rvalue
    // reference on, as get of an rvalue Record does, and the caller reads it
    // in the full expression of the call.
    tree const result = TREE_TYPE(DECL_RESULT(walk.function));
    bool const is_forwarding = TREE_CODE(result) == REFERENCE_TYPE && TYPE_REF_IS_RVALUE(result);
    for (tree origin : found) {
        if (origin == NULL_TREE || !walk.origins->ends_with_frame(origin)
            || (is_forwarding && is_rvalue_parameter(origin))) {
            continue;
        }
        location_t location = EXPR_HAS_LOCATION(current) ? EXPR_LOCATION(current) : DECL_SOURCE_LOCATION(walk.function);
        Place place = place_of(location, Scope::quarantine);
        if (place.file != nullptr) {
            add_finding(Kind::escaped_borrow, nullptr, place, "borrow of " + object_text(origin));
        }
        break;
    }
    return NULL_TREE;
}

// One store of a value that holds a borrow into an object that outlives the
// frame (the stores).  PLACE is the parameter by reference, the object
// parameter this, or the object of static or thread storage that the store
// writes.  VALUE is the stored value, or the call of a constructor that
// builds the object in place.
struct Store {
    tree place = NULL_TREE;
    tree value = NULL_TREE;
    bool is_construction = false;
    location_t location = UNKNOWN_LOCATION;
};

// The object outside the frame that the lvalue TARGET names, through its
// members and its elements: a parameter by reference, the object parameter
// this, or an object of static or thread storage.  Null for each other
// target, such as a local.  With IS_ADDRESS, TARGET is the address of the
// object, as the object argument of an assignment operator or a constructor
// is.  The front end reads a parameter by reference through a dereference,
// and its address is the parameter itself.
tree outer_place_of(tree target, bool is_address) {
    target = strip_conversions(target);
    if (target == NULL_TREE) {
        return NULL_TREE;
    }
    if (is_address) {
        if (TREE_CODE(target) == PARM_DECL) {
            return is_this_parameter(target) || TREE_CODE(TREE_TYPE(target)) == REFERENCE_TYPE ? target : NULL_TREE;
        }
        if (TREE_CODE(target) != ADDR_EXPR) {
            return NULL_TREE;
        }
        target = strip_conversions(TREE_OPERAND(target, 0));
    }
    for (int depth = 0; target != NULL_TREE && depth < kEscapeDepth; ++depth) {
        if (TREE_CODE(target) != COMPONENT_REF && TREE_CODE(target) != ARRAY_REF) {
            break;
        }
        target = strip_conversions(TREE_OPERAND(target, 0));
    }
    if (target == NULL_TREE) {
        return NULL_TREE;
    }
    if (TREE_CODE(target) == INDIRECT_REF) {
        tree object = strip_conversions(TREE_OPERAND(target, 0));
        bool const is_outer = object != NULL_TREE && TREE_CODE(object) == PARM_DECL
                           && (is_this_parameter(object) || TREE_CODE(TREE_TYPE(object)) == REFERENCE_TYPE);
        return is_outer ? object : NULL_TREE;
    }
    bool const is_static =
        VAR_P(target) && (TREE_STATIC(target) || DECL_EXTERNAL(target) || DECL_THREAD_LOCAL_P(target));
    return is_static ? target : NULL_TREE;
}

// The callback of cp_walk_tree that collects the stores of a body into an
// object outside the frame, whose type holds a borrow (the stores): an
// assignment or an initialization, a call of an assignment operator, and a
// call that builds such an object in place.  A call that gives a class by value
// is an AGGR_INIT_EXPR, also the call of an assignment operator.
tree collect_store(tree* node, int*, void* data) {
    auto& stores = *static_cast<std::vector<Store>*>(data);
    tree current = *node;
    location_t const location = EXPR_HAS_LOCATION(current) ? EXPR_LOCATION(current) : UNKNOWN_LOCATION;
    if (TREE_CODE(current) == INIT_EXPR || TREE_CODE(current) == MODIFY_EXPR) {
        tree target = TREE_OPERAND(current, 0);
        tree place = outer_place_of(target, false);
        if (place != NULL_TREE && can_hold_borrow(TREE_TYPE(target))) {
            stores.push_back(Store{place, TREE_OPERAND(current, 1), false, location});
        }
        return NULL_TREE;
    }
    if (TREE_CODE(current) == AGGR_INIT_EXPR) {
        tree place = outer_place_of(AGGR_INIT_EXPR_SLOT(current), false);
        if (place != NULL_TREE && can_hold_borrow(TREE_TYPE(AGGR_INIT_EXPR_SLOT(current)))) {
            stores.push_back(Store{place, current, true, location});
        }
    } else if (TREE_CODE(current) != CALL_EXPR) {
        return NULL_TREE;
    }
    tree callee = cp_get_callee_fndecl_nofold(current);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || argument_count(current) < 1) {
        return NULL_TREE;
    }
    bool const is_assignment = DECL_ASSIGNMENT_OPERATOR_P(callee) && argument_count(current) == 2;
    bool const is_construction = TREE_CODE(current) == CALL_EXPR && DECL_CONSTRUCTOR_P(callee);
    if (!is_assignment && !is_construction) {
        return NULL_TREE;
    }
    // The object argument of a member, and the first argument of an operator
    // of namespace scope such as operator^= of std::byte, is the address of
    // the target.
    tree object = argument_at(current, 0);
    tree object_type = object != NULL_TREE ? TREE_TYPE(object) : NULL_TREE;
    if (object_type == NULL_TREE || !POINTER_TYPE_P(object_type)) {
        return NULL_TREE;
    }
    tree place = outer_place_of(object, true);
    if (place != NULL_TREE && can_hold_borrow(TREE_TYPE(object_type))) {
        tree value = is_assignment ? argument_at(current, 1) : current;
        stores.push_back(Store{place, value, is_construction, location});
    }
    return NULL_TREE;
}

// The text of the object outside the frame of a store.
std::string place_text(tree place) {
    if (TREE_CODE(place) == PARM_DECL && is_this_parameter(place)) {
        return "the object of the member function";
    }
    std::string const name = DECL_NAME(place) != NULL_TREE ? identifier_text(DECL_NAME(place)) : "(unnamed)";
    return (TREE_CODE(place) == PARM_DECL ? "the parameter " : "the static ") + name;
}

// Gives the line stored-borrow for each store of STORES whose value has an
// origin that ends with the frame (the stores).  A store into the object of
// a move or a copy takes the borrows of its source parameter, which names an
// object of the caller, so it gives no origin.
void check_stores(OriginWalk& origins, const std::vector<Store>& stores, tree function) {
    for (const Store& store : stores) {
        std::vector<tree> found;
        if (store.is_construction) {
            origins.add_call_origins(store.value, found, 0);
        } else {
            origins.add_origins(store.value, found, 0);
        }
        for (tree origin : found) {
            if (origin == NULL_TREE || origin == store.place || !origins.ends_with_frame(origin)) {
                continue;
            }
            location_t location = store.location != UNKNOWN_LOCATION ? store.location : DECL_SOURCE_LOCATION(function);
            Place place = place_of(location, Scope::quarantine);
            if (place.file != nullptr) {
                add_finding(Kind::stored_borrow, nullptr, place,
                            "borrow of " + object_text(origin) + " into " + place_text(store.place));
            }
            break;
        }
    }
}

// The data of the walk of the returns for the borrowed parameters.
struct ParameterWalk {
    OriginWalk* origins = nullptr;
    std::vector<tree>* found = nullptr;
};

// The callback of cp_walk_tree that adds the origins of each return.
tree add_return_origins(tree* node, int*, void* data) {
    ParameterWalk& walk = *static_cast<ParameterWalk*>(data);
    if (TREE_CODE(*node) == RETURN_EXPR && TREE_OPERAND(*node, 0) != NULL_TREE) {
        walk.origins->add_origins(TREE_OPERAND(*node, 0), *walk.found, 0);
    }
    return NULL_TREE;
}

// True for a function that no overload set holds: an artificial function, and
// the call operator and the static thunk of a closure.
bool is_closure_function(tree function) {
    tree owner = DECL_CONTEXT(function);
    return DECL_ARTIFICIAL(function) || (owner != NULL_TREE && TYPE_P(owner) && LAMBDA_TYPE_P(owner));
}

// True when the parameter at INDEX of the pattern of the instance FUNCTION is
// a reference to a template parameter that is not const: T& of to(T&), or the
// forwarding T&&.  A temporary that is not const deduces no const lvalue
// reference there, and a const object deduces the const reference of the
// instance.  False for a function that is no instance.
bool is_deduced_lvalue_parameter(tree function, int index) {
    if (DECL_LANG_SPECIFIC(function) == nullptr || !DECL_TEMPLATE_INFO(function)
        || !DECL_TEMPLATE_INSTANTIATION(function)) {
        return false;
    }
    tree pattern = DECL_TEMPLATE_RESULT(most_general_template(DECL_TI_TEMPLATE(function)));
    if (pattern == NULL_TREE || TREE_CODE(pattern) != FUNCTION_DECL) {
        return false;
    }
    tree parameter = FUNCTION_FIRST_USER_PARM(pattern);
    for (int position = 0; parameter != NULL_TREE && position < index; ++position) {
        parameter = DECL_CHAIN(parameter);
    }
    tree type = parameter != NULL_TREE ? TREE_TYPE(parameter) : NULL_TREE;
    return type != NULL_TREE && TREE_CODE(type) == REFERENCE_TYPE && TREE_CODE(TREE_TYPE(type)) == TEMPLATE_TYPE_PARM
        && !TYPE_READONLY(TREE_TYPE(type));
}

// Gives each parameter by a const reference that an origin of a return names
// to the kind missing-mark (the borrowed parameters).  An assignment gives
// back its object by its convention, and an rvalue reference in the result
// forwards the parameter on, as get of an rvalue Record does, so the rule
// reads neither.  A parameter whose type holds a borrow lends the objects of
// its borrows, and an empty class holds nothing to borrow.  A reading that the
// end of the unit runs again notes none, and a wide reading only tells that it
// can note one.
void note_borrowed_parameters(tree function, tree body) {
    tree result = TREE_TYPE(DECL_RESULT(function));
    if (DECL_ASSIGNMENT_OPERATOR_P(function) || is_closure_function(function)
        || (TREE_CODE(result) == REFERENCE_TYPE && TYPE_REF_IS_RVALUE(result))) {
        return;
    }
    OriginWalk origins{function, OutsideRuns::ignore};
    origins.reads_reference_parameters_ = true;
    origins.record_locals(body);
    std::vector<tree> found;
    ParameterWalk walk{&origins, &found};
    cp_walk_tree_without_duplicates(&body, add_return_origins, &walk);
    std::vector<std::pair<tree, int>> borrowed;
    int index = 0;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter), ++index) {
        tree type = TREE_TYPE(parameter);
        if (TREE_CODE(type) != REFERENCE_TYPE || TYPE_REF_IS_RVALUE(type) || !TYPE_READONLY(TREE_TYPE(type))) {
            continue;
        }
        tree referred = TREE_TYPE(type);
        bool const is_value = can_hold_no_borrow(referred) && !(CLASS_TYPE_P(referred) && is_empty_class(referred));
        if (is_value && std::find(found.begin(), found.end(), parameter) != found.end()
            && !is_deduced_lvalue_parameter(function, index)) {
            borrowed.emplace_back(parameter, index);
        }
    }
    if (open_answer == OpenAnswer::each) {
        has_wide_parameter = has_wide_parameter || !borrowed.empty();
    } else if (!is_run_again()) {
        for (auto const& [parameter, place] : borrowed) {
            note_borrowed_parameter(function, parameter, place);
        }
    }
}

// The rule on the body of FUNCTION: the stores, the returns and the borrowed
// parameters.
void read_body(tree function) {
    open_parts[0].clear();
    open_parts[1].clear();
    tree body = DECL_SAVED_TREE(function);
    bool const returns_borrow = can_hold_borrow(TREE_TYPE(DECL_RESULT(function)));
    std::vector<Store> stores;
    cp_walk_tree_without_duplicates(&body, collect_store, &stores);
    // The walk of the stores reads the runs outside, and the walk of the
    // returns does not, so each one records the locals of its own reading.
    if (!stores.empty()) {
        OriginWalk store_origins{function, OutsideRuns::read};
        store_origins.record_locals(body);
        check_stores(store_origins, stores, function);
    }
    if (!returns_borrow) {
        return;
    }
    OriginWalk origins{function, OutsideRuns::ignore};
    origins.record_locals(body);
    ReturnWalk walk{&origins, function};
    cp_walk_tree_without_duplicates(&body, check_return, &walk);
    note_borrowed_parameters(function, body);
}

// The wide reading of the body of FUNCTION (BodyReader of plugin.h).  Returns
// true when an answer of the open questions can give a borrowed parameter.
bool read_body_wide(tree function) {
    open_answer = OpenAnswer::each;
    has_wide_parameter = false;
    read_body(function);
    open_answer = OpenAnswer::no;
    return has_wide_parameter;
}

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || !is_body_read_here(function, UNKNOWN_LOCATION)) {
        return;
    }
    // The constant evaluator refuses a read of an ended object in a consteval
    // function itself, so the rule reads no such function.
    if (DECL_RESULT(function) != NULL_TREE && !DECL_IMMEDIATE_FUNCTION_P(function)) {
        read_or_keep_body(function, BodyReader{&read_body, &read_body_wide, nullptr});
    }
}

// The answers of holds_unstatic_borrow (the statics, ReachWalk).
ReachAnswers unstatic_borrow_classes;

// True when the class TYPE names StaticStorageBrand of foundation/core/Ref.h
// as a template argument (the statics).
bool names_static_brand(tree type) {
    static tree const brand = get_identifier("StaticStorageBrand");
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument == NULL_TREE || !TYPE_P(argument) || !CLASS_TYPE_P(argument)) {
            continue;
        }
        tree main = TYPE_MAIN_VARIANT(argument);
        if (TYPE_IDENTIFIER(main) == brand && is_of_base_namespace(main)) {
            return true;
        }
    }
    return false;
}

// True when TYPE holds a borrow type of the base whose template arguments
// name no StaticStorageBrand, in the walk WALK of holds_unstatic_borrow.
bool reaches_unstatic_borrow(tree type, ReachWalk& walk, int depth) {
    if (type == NULL_TREE || type == error_mark_node) {
        return false;
    }
    if (depth > kEscapeDepth) {
        walk.reach_unread_part();
        return false;
    }
    if (TREE_CODE(type) == ARRAY_TYPE) {
        return reaches_unstatic_borrow(TREE_TYPE(type), walk, depth + 1);
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return false;
    }
    ReachWalk::Step const step = walk.enter(type);
    if (step != ReachWalk::Step::read) {
        return step == ReachWalk::Step::yes;
    }
    bool const is_borrow = carries_mark(type, escape_names.refers) || carries_mark(type, escape_names.refers_unique)
                        || carries_mark(type, escape_names.refers_deep);
    bool is_held = is_borrow && !names_static_brand(type);
    // The walk also reads the bases and the members, so it reads the owned
    // arguments of a class outside the base too: the mark only adds a reach.
    OwnedPositions owned = owned_positions(type);
    if (!owned.is_stated && carries_mark(type, escape_names.payload_cell)) {
        owned = OwnedPositions::first();
    }
    if (!owned.is_stated && is_owner_core_class(type)) {
        owned = OwnedPositions::each();
    }
    if (!is_borrow) {
        visit_owned_arguments(type, owned, [&](tree argument) {
            is_held = is_held || reaches_unstatic_borrow(argument, walk, depth + 1);
        });
    }
    tree binfo = TYPE_BINFO(type);
    for (unsigned index = 0; !is_borrow && !is_held && binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo);
         ++index) {
        is_held = reaches_unstatic_borrow(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), walk, depth + 1);
    }
    for (tree field = TYPE_FIELDS(type); !is_borrow && !is_held && field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)) {
            is_held = reaches_unstatic_borrow(TREE_TYPE(field), walk, depth + 1);
        }
    }
    return walk.leave(type, is_held);
}

// True when TYPE holds a borrow type of the base whose template arguments
// name no StaticStorageBrand (the statics).  Complexity: linear in the
// classes that the type reaches (ReachWalk).
bool holds_unstatic_borrow(tree type) {
    ReachWalk walk(unstatic_borrow_classes);
    return reaches_unstatic_borrow(type, walk, 0);
}

// The callback of cp_walk_tree that finds an address of an object that has no
// static storage in an initializer: a local, a parameter, a temporary or a
// thread_local object (the statics).
tree find_unstatic_address(tree* node, int* walk_subtrees, void*) {
    if (TREE_CODE(*node) != ADDR_EXPR) {
        return NULL_TREE;
    }
    *walk_subtrees = 0;
    tree base = get_base_address(TREE_OPERAND(*node, 0));
    bool const is_static =
        base != NULL_TREE
        && (TREE_CODE(base) == STRING_CST || TREE_CODE(base) == FUNCTION_DECL
            || (VAR_P(base) && (TREE_STATIC(base) || DECL_EXTERNAL(base)) && !DECL_THREAD_LOCAL_P(base)));
    return is_static ? NULL_TREE : *node;
}

// True when DECL is const and its constant initializer holds only addresses of
// objects of static storage, or no address (the statics).
bool has_static_constant_initializer(tree decl) {
    tree initial = DECL_INITIAL(decl);
    bool const is_const = TREE_READONLY(decl) || CP_TYPE_CONST_P(TREE_TYPE(decl));
    if (!is_const || initial == NULL_TREE || initial == error_mark_node
        || !(TREE_CONSTANT(initial) || DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P(decl))) {
        return false;
    }
    return cp_walk_tree_without_duplicates(&initial, find_unstatic_address, nullptr) == NULL_TREE;
}

void on_collection(void*, void*) {
    borrow_classes.clear();
    unstatic_borrow_classes.clear();
}

}  // namespace

bool holds_borrow(tree type) { return can_hold_borrow(type); }

OwnedPositions held_owner_arguments(tree type) {
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return OwnedPositions{};
    }
    if (is_base_storage_owner(type)) {
        return OwnedPositions{true, false, 0};
    }
    // The rule trusts by_arguments{} only in a class of the base, because the
    // owner gives no member to the walk.
    if (is_of_base_namespace(type)) {
        OwnedPositions const owned = owned_positions(type);
        if (owned.is_stated) {
            return owned;
        }
    }
    if (carries_mark(type, escape_names.payload_cell)) {
        return OwnedPositions::first();
    }
    return is_owner_core_class(type) ? OwnedPositions::each() : OwnedPositions{};
}

bool is_borrow_class(tree type) {
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type)
        && (carries_mark(type, escape_names.refers) || carries_mark(type, escape_names.refers_unique)
            || carries_mark(type, escape_names.refers_deep));
}

bool is_unique_borrow_class(tree type) {
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type) && carries_mark(type, escape_names.refers_unique);
}

bool is_number_owner_class(tree type) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(type) : NULL_TREE;
    return type != NULL_TREE && CLASS_TYPE_P(type) && carries_mark(type, escape_names.owns_by_number);
}

bool is_number_borrow_class(tree type) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(type) : NULL_TREE;
    return type != NULL_TREE && CLASS_TYPE_P(type) && carries_mark(type, escape_names.refers_by_number);
}

// The rule trusts refers_static{} only in a class of the base, as the store walk
// of foundation/core/CaptureStore.h does: check_refers_static_statement of
// foundation/Transfer.h checks each constructor and each part of such a class.
bool names_static_storage(tree type) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(type) : NULL_TREE;
    return type != NULL_TREE && CLASS_TYPE_P(type) && carries_mark(type, escape_names.refers_static)
        && is_of_base_namespace(type);
}

bool is_storage_owner(tree type) { return is_base_storage_owner(type); }

bool is_block_keeper(tree type) { return is_base_storage_owner(type) || is_base_block_keeper(type); }

bool is_outside_owner(tree type) { return outside_run_core(type) != NULL_TREE; }

void set_wide_borrows(bool is_wide) { open_answer = is_wide ? OpenAnswer::each : OpenAnswer::no; }

void check_static_borrow(tree decl, const std::string& entity) {
    if (!holds_unstatic_borrow(TREE_TYPE(decl)) || has_static_constant_initializer(decl)) {
        return;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(decl), Scope::quarantine);
    if (place.file != nullptr) {
        add_finding(Kind::static_borrow, nullptr, place, entity);
    }
}

void report_view_escape(const Finding& finding) {
    if (finding.kind == Kind::stale_borrow) {
        report_stale_borrow(finding);
        return;
    }
    if (finding.kind == Kind::two_names) {
        report_two_names(finding);
        return;
    }
    if (finding.kind == Kind::number_borrow) {
        error_at(finding.spelling, "this borrow class names no object by a pointer: %s", finding.entity.c_str());
        inform(finding.spelling, "the rule view-escape follows a borrow to the objects that it names, so no end of "
                                 "an owner can reach a loan of this class");
        inform(finding.spelling,
               "a class that refers to a resource of the kernel by its number states "
               "%<::foundation::transfer::refers_by_number{}%>, and its first type argument names the resource");
        return;
    }
    if (finding.kind == Kind::static_borrow) {
        error_at(finding.spelling,
                 "an object of static or thread storage duration holds a borrow that names no object of static "
                 "storage: %s",
                 finding.entity.c_str());
        inform(finding.spelling, "a View, a Ref and a run own nothing, and the object lives past each frame, so its "
                                 "borrow can name an object that ended, whatever its brand");
        inform(finding.spelling,
               "keep the borrow in an object of automatic storage, take a Ref of a static object through "
               "%<ref_of_static%>, or make the object const with a constant initializer of static objects");
        return;
    }
    if (finding.kind == Kind::stored_borrow) {
        error_at(finding.spelling,
                 "this function stores a %s, and the borrowed object ends with the frame of the function",
                 finding.entity.c_str());
        inform(finding.spelling, "a View, a Ref and an address own nothing, so a later read of the store reads the "
                                 "dead stack frame of the call");
        inform(finding.spelling, "store a value that owns its storage, or a borrow of an object that outlives the "
                                 "store, such as an object of the caller or a member that the object holds");
        inform(finding.spelling,
               "a test of the store itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region "
               "of %<foundation/Quarantine.h%>");
        return;
    }
    error_at(finding.spelling, "this function gives back a %s, and that object ends with the frame of the function",
             finding.entity.c_str());
    inform(finding.spelling,
           "a View, a Ref and an address own nothing, so a read of the result reads the dead stack frame of the call");
    inform(finding.spelling,
           "return a value that owns its storage, or take the object by a reference that %<CRUCIBLE_LIFETIMEBOUND%> "
           "marks and give back a borrow of it");
    inform(finding.spelling,
           "a test of the return itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

void register_view_escape(const char* plugin_name) {
    escape_names.transfer = get_identifier("transfer");
    escape_names.refers = get_identifier("refers");
    escape_names.refers_unique = get_identifier("refers_unique");
    escape_names.refers_deep = get_identifier("refers_deep");
    escape_names.owns_by_number = get_identifier("owns_by_number");
    escape_names.refers_by_number = get_identifier("refers_by_number");
    escape_names.refers_static = get_identifier("refers_static");
    escape_names.payload_cell = get_identifier("payload_cell");
    escape_names.foundation = get_identifier("foundation");
    escape_names.fixy = get_identifier("fixy");
    escape_names.std_name = get_identifier("std");
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
