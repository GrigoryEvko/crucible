// The family view escape: the rule view-escape.  A function returns a
// borrow, and the borrow names an object that ends with the frame of the
// function: a local, a parameter by value or a temporary.  A read of the
// result then reads a dead stack frame.  The rule refuses each such return.
//
// THE BORROW
//   A type holds a borrow when it is a reference or a pointer, or a class that
//   states refers{}, refers_unique{} or refers_deep{} of
//   include/foundation/Transfer.h, such as a View or a Ref, at each depth of
//   its bases and its members.  A class that states by_arguments{},
//   payload_cell{} or one of the three refers marks holds the object of its
//   first type argument in its own bytes or behind a pointer, as an Option, a
//   Box and a Ref do, so the walk reads that argument too.  The rule reads
//   each function whose return type holds a borrow.
//
// THE ORIGINS
//   The origins of an expression are the objects that its borrow can name.
//   Each borrow of the base comes from a door of the base: to(), erase_brand(),
//   view(), get(), append(), and the constructors of a View and a Ref.  So the
//   rule follows a call of a function of the namespace foundation or fixy:
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
//   assignment of it, and a local reference names the object of its
//   initializer.  A conversion, a conditional, a comma and a temporary pass
//   their origins on.
//
// THE FINDING
//   escaped-borrow   a return whose value has an origin that ends with the
//                    frame: a local of automatic storage, a parameter by
//                    value, or a temporary of the full expression
//   The entity is the object.  A function that returns a borrow of a
//   parameter by reference names an object of its caller, which is no
//   finding.  The kind missing-mark of the rule lifetime-twin asks such a
//   function to mark the parameter, so the caller cannot give a temporary.
//
// THE LIMITS
//   The rule reads one body.  A borrow that a function of the tree outside the
//   base makes from an argument, a borrow that passes through a member of a
//   local object, a field of a closure, or a container of borrows, gives no
//   origin.  The two exceptions of the linear design hold: a function returns
//   a borrow of a parameter that it marks, and a class that cannot move or
//   copy holds a view of a member that it declares first.  The rule reads only
//   the return of a borrow, and neither of the two meets a return of a local.
//
// THE RETIREMENT
//   A View with a brand or a lifetime parameter would carry this fact in its
//   type, as a borrow of Rust carries its lifetime.  C++ gives neither today,
//   so the plugin holds it.
//
// THE ERRORS
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

// The marks of the borrow of THE BORROW.
struct EscapeNames {
    tree transfer = NULL_TREE;
    tree refers = NULL_TREE;
    tree refers_unique = NULL_TREE;
    tree refers_deep = NULL_TREE;
    tree by_arguments = NULL_TREE;
    tree payload_cell = NULL_TREE;
    tree foundation = NULL_TREE;
    tree fixy = NULL_TREE;
    tree std_name = NULL_TREE;
};

EscapeNames escape_names;

// The answer of holds_borrow for each class of the unit, by TYPE_UID.  A class
// that the walk meets again during its walk holds nothing there.
std::unordered_map<unsigned, bool> borrow_classes;

bool carries_mark(tree type, tree name) {
    return carries_base_annotation(TYPE_ATTRIBUTES(type), name, escape_names.transfer);
}

// True for a class with a base OwnerCore of include/foundation/core/Owner.h:
// an owner of the base that holds its storage alone.
bool is_owner_core_class(tree type) {
    static tree const owner_core = get_identifier("OwnerCore");
    tree binfo = TYPE_BINFO(type);
    for (unsigned index = 0; binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        tree base = BINFO_TYPE(BINFO_BASE_BINFO(binfo, index));
        tree info = base != NULL_TREE && CLASS_TYPE_P(base) ? CLASSTYPE_TEMPLATE_INFO(base) : NULL_TREE;
        if (info != NULL_TREE && DECL_NAME(TI_TEMPLATE(info)) == owner_core) {
            return true;
        }
    }
    return false;
}

// True when TYPE holds a borrow (THE BORROW).  Complexity: linear in the
// distinct classes that the type reaches, one time for each class in a unit.
bool holds_borrow(tree type, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > kEscapeDepth) {
        return false;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE) {
        return TREE_CODE(TREE_TYPE(type)) != FUNCTION_TYPE && TREE_CODE(TREE_TYPE(type)) != METHOD_TYPE;
    }
    if (TREE_CODE(type) == ARRAY_TYPE) {
        return holds_borrow(TREE_TYPE(type), depth + 1);
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type)) {
        return false;
    }
    auto found = borrow_classes.find(TYPE_UID(type));
    if (found != borrow_classes.end()) {
        return found->second;
    }
    borrow_classes.emplace(TYPE_UID(type), false);
    bool const is_borrow = carries_mark(type, escape_names.refers) || carries_mark(type, escape_names.refers_unique)
                        || carries_mark(type, escape_names.refers_deep);
    bool is_held = is_borrow;
    // An owner holds its pointers, and a value of its first type argument or
    // of each type argument: a class that states by_arguments{} or
    // payload_cell{}, and a class on OwnerCore.
    bool const is_first_owner =
        carries_mark(type, escape_names.by_arguments) || carries_mark(type, escape_names.payload_cell);
    bool const is_core_owner = is_owner_core_class(type);
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    if (is_first_owner || is_core_owner) {
        int const last = arguments == NULL_TREE ? 0 : (is_first_owner ? 1 : TREE_VEC_LENGTH(arguments));
        for (int index = 0; !is_held && arguments != NULL_TREE && index < last && index < TREE_VEC_LENGTH(arguments);
             ++index) {
            tree argument = TREE_VEC_ELT(arguments, index);
            is_held = argument != NULL_TREE && TYPE_P(argument) && holds_borrow(argument, depth + 1);
        }
        borrow_classes[TYPE_UID(type)] = is_held;
        return is_held;
    }
    tree binfo = TYPE_BINFO(type);
    for (unsigned index = 0; !is_held && binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        is_held = holds_borrow(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
    }
    for (tree field = TYPE_FIELDS(type); !is_held && field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)) {
            is_held = holds_borrow(TREE_TYPE(field), depth + 1);
        }
    }
    borrow_classes[TYPE_UID(type)] = is_held;
    return is_held;
}

// True for a function of the base, whose borrow THE ORIGINS follow.
bool is_base_door(tree function) {
    tree top = top_namespace(function);
    return top != global_namespace
        && (DECL_NAME(top) == escape_names.foundation || DECL_NAME(top) == escape_names.fixy);
}

// True for a function of the namespace std that gives a reference.
bool is_standard_reference(tree function) {
    tree top = top_namespace(function);
    tree type = TREE_TYPE(TREE_TYPE(function));
    return top != global_namespace && DECL_NAME(top) == escape_names.std_name && type != NULL_TREE
        && TREE_CODE(type) == REFERENCE_TYPE;
}

// The deepest walk of COULD NAME.
constexpr int kNameDepth = 6;

// Adds the types that a borrow of TYPE names: the element of a reference or a
// pointer, the first type argument of a class with a refers mark, and the
// targets of each part of a class that holds a borrow, such as an Option of a
// Ref.  Complexity: linear in the parts of the type, to the depth kNameDepth.
void add_borrow_targets(tree type, std::vector<tree>& out, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > kNameDepth) {
        return;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE) {
        out.push_back(TYPE_MAIN_VARIANT(TREE_TYPE(type)));
        return;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || !holds_borrow(type, 0)) {
        return;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    bool const is_borrow = carries_mark(type, escape_names.refers) || carries_mark(type, escape_names.refers_unique)
                        || carries_mark(type, escape_names.refers_deep);
    if (is_borrow) {
        if (arguments != NULL_TREE && TREE_VEC_LENGTH(arguments) > 0 && TYPE_P(TREE_VEC_ELT(arguments, 0))) {
            out.push_back(TYPE_MAIN_VARIANT(TREE_VEC_ELT(arguments, 0)));
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

// True when WHOLE holds a part of the type PART: the type itself, the element
// of an array, a base, a member, or a type argument of a class template, which
// an owner holds in its bytes or on the heap.  Complexity: linear in the parts
// of the type, to the depth kNameDepth.
bool holds_part(tree whole, tree part, int depth) {
    if (whole == NULL_TREE || whole == error_mark_node || depth > kNameDepth) {
        return false;
    }
    whole = TYPE_MAIN_VARIANT(whole);
    if (whole == part) {
        return true;
    }
    if (TREE_CODE(whole) == ARRAY_TYPE) {
        return holds_part(TREE_TYPE(whole), part, depth + 1);
    }
    if (!CLASS_TYPE_P(whole) || !COMPLETE_TYPE_P(whole)) {
        return false;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(whole);
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument) && holds_part(argument, part, depth + 1)) {
            return true;
        }
    }
    tree binfo = TYPE_BINFO(whole);
    for (unsigned index = 0; binfo != NULL_TREE && index < BINFO_N_BASE_BINFOS(binfo); ++index) {
        if (holds_part(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), part, depth + 1)) {
            return true;
        }
    }
    for (tree field = TYPE_FIELDS(whole); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)
            && holds_part(TREE_TYPE(field), part, depth + 1)) {
            return true;
        }
    }
    return false;
}

// How a call can lend an object that an argument names (THE ORIGINS).
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
bool could_name(tree result, tree object, Lend lend) {
    std::vector<tree> targets;
    add_borrow_targets(result, targets, 0);
    tree const bare = object != NULL_TREE ? TYPE_MAIN_VARIANT(object) : NULL_TREE;
    for (tree target : targets) {
        if (lend == Lend::exact ? bare == target : holds_part(object, target, 0)) {
            return true;
        }
    }
    return false;
}

// True when a borrow of the type RESULT can carry on a borrow that the type
// HELD holds: the result names a part of a target of HELD, as a window of a
// View names a part of its run, or erase_brand gives the target of its Ref.
bool could_forward(tree result, tree held) {
    std::vector<tree> results;
    std::vector<tree> helds;
    add_borrow_targets(result, results, 0);
    add_borrow_targets(held, helds, 0);
    for (tree result_target : results) {
        for (tree held_target : helds) {
            if (holds_part(held_target, result_target, 0)) {
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

// The origins of the expressions of one function (THE ORIGINS).
class OriginWalk {
public:
    explicit OriginWalk(tree function) : function_{function} {}

    // True for an object that ends with the frame of the function.
    bool ends_with_frame(tree decl) const {
        if (TREE_CODE(decl) == PARM_DECL) {
            return TREE_CODE(TREE_TYPE(decl)) != REFERENCE_TYPE && !is_this_parameter(decl);
        }
        return VAR_P(decl) && !TREE_STATIC(decl) && !DECL_EXTERNAL(decl) && !DECL_THREAD_LOCAL_P(decl)
            && decl_function_context(decl) == function_ && TREE_CODE(TREE_TYPE(decl)) != REFERENCE_TYPE;
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
                if (holds_borrow(TREE_TYPE(object), 0)) {
                    add_origins(object, out, depth + 1);
                } else {
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
                if (reads_reference_parameters_ && TREE_CODE(TREE_TYPE(expression)) == REFERENCE_TYPE) {
                    out.push_back(expression);
                }
                return;
            case VAR_DECL:
            case RESULT_DECL: {
                // The front end gives a named return value the RESULT_DECL of
                // the function, so the result holds the origins of the local.
                auto found = local_origins_.find(DECL_UID(expression));
                if (found != local_origins_.end()) {
                    out.insert(out.end(), found->second.begin(), found->second.end());
                }
                return;
            }
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
                if (reads_reference_parameters_ && operand != NULL_TREE && TREE_CODE(operand) == PARM_DECL
                    && TREE_CODE(TREE_TYPE(operand)) == REFERENCE_TYPE) {
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

    // The origins of a call (THE ORIGINS).  RESULT is the type that the call
    // makes: the class of a constructor, or the return type.
    void add_call_origins(tree call, std::vector<tree>& out, int depth) {
        tree callee = cp_get_callee_fndecl_nofold(call);
        if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
            return;
        }
        int const count = argument_count(call);
        if (!is_base_door(callee)) {
            return;
        }
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
                if (holds_borrow(type, 0) && could_forward(result, type)) {
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
            }
        }
        out.erase(std::remove_if(
                      out.begin() + static_cast<std::ptrdiff_t>(first_found), out.end(),
                      [&moved](tree origin) { return std::find(moved.begin(), moved.end(), origin) != moved.end(); }),
                  out.end());
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
            if (origin != NULL_TREE && could_name(result, type, Lend::part)) {
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
    // nothing that a borrow can name.  Returns true when the argument names an
    // object that holds a value.
    bool add_reference_argument(tree argument, Lend lend, tree result, std::vector<tree>& out, int depth) {
        tree stripped = strip_conversions(argument);
        if (stripped == NULL_TREE || TREE_TYPE(stripped) == NULL_TREE || !POINTER_TYPE_P(TREE_TYPE(stripped))) {
            return false;
        }
        tree object_type = TREE_TYPE(TREE_TYPE(stripped));
        if (object_type != NULL_TREE && CLASS_TYPE_P(object_type) && is_empty_class(object_type)) {
            return false;
        }
        bool const is_address = TREE_CODE(stripped) == ADDR_EXPR;
        bool const is_pointer_value = !is_address && TREE_CODE(TREE_TYPE(stripped)) == POINTER_TYPE;
        if (is_pointer_value) {
            if (lend == Lend::object || could_forward(result, TREE_TYPE(stripped))) {
                add_origins(stripped, out, depth + 1);
            }
            return true;
        }
        tree object = is_address ? TREE_OPERAND(stripped, 0) : stripped;
        if (holds_borrow(object_type, 0)) {
            if (lend == Lend::object) {
                add_origins(object, out, depth + 1);
            } else if (could_forward(result, object_type)) {
                add_named_origins(object, result, out, depth + 1);
            }
        } else if (lend == Lend::object || (lend != Lend::forward_only && could_name(result, object_type, lend))) {
            add_object(object, out, depth + 1);
        }
        return true;
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
            tree target = strip_conversions(TREE_OPERAND(current, 0));
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
        } else if (holds_borrow(TREE_TYPE(decl), 0)) {
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
    // names too, for THE BORROWED PARAMETERS, and not only the objects that
    // end with the frame.
    bool reads_reference_parameters_ = false;

private:
    tree function_;
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
    if (DECL_ARTIFICIAL(decl) || DECL_NAME(decl) == NULL_TREE) {
        return "a temporary";
    }
    return (TREE_CODE(decl) == PARM_DECL ? "the parameter " : "the local ") + identifier_text(DECL_NAME(decl));
}

// The callback of cp_walk_tree over the returns of a function (THE FINDING).
tree check_return(tree* node, int*, void* data) {
    ReturnWalk& walk = *static_cast<ReturnWalk*>(data);
    tree current = *node;
    if (TREE_CODE(current) != RETURN_EXPR || TREE_OPERAND(current, 0) == NULL_TREE) {
        return NULL_TREE;
    }
    std::vector<tree> found;
    walk.origins->add_origins(TREE_OPERAND(current, 0), found, 0);
    for (tree origin : found) {
        if (origin == NULL_TREE || !walk.origins->ends_with_frame(origin)) {
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

// The data of the walk of the returns for THE BORROWED PARAMETERS.
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
// to the kind missing-mark (THE BORROWED PARAMETERS).  An assignment gives
// back its object by its convention, and an rvalue reference in the result
// forwards the parameter on, as get of an rvalue Record does, so the rule
// reads neither.  A parameter whose type holds a borrow lends the objects of
// its borrows, and an empty class holds nothing to borrow.
void note_borrowed_parameters(tree function, tree body) {
    tree result = TREE_TYPE(DECL_RESULT(function));
    if (DECL_ASSIGNMENT_OPERATOR_P(function) || is_closure_function(function)
        || (TREE_CODE(result) == REFERENCE_TYPE && TYPE_REF_IS_RVALUE(result))) {
        return;
    }
    OriginWalk origins{function};
    origins.reads_reference_parameters_ = true;
    origins.record_locals(body);
    std::vector<tree> found;
    ParameterWalk walk{&origins, &found};
    cp_walk_tree_without_duplicates(&body, add_return_origins, &walk);
    int index = 0;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter), ++index) {
        tree type = TREE_TYPE(parameter);
        if (TREE_CODE(type) != REFERENCE_TYPE || TYPE_REF_IS_RVALUE(type) || !TYPE_READONLY(TREE_TYPE(type))) {
            continue;
        }
        tree referred = TREE_TYPE(type);
        bool const is_value = !holds_borrow(referred, 0) && !(CLASS_TYPE_P(referred) && is_empty_class(referred));
        if (is_value && std::find(found.begin(), found.end(), parameter) != found.end()
            && !is_deduced_lvalue_parameter(function, index)) {
            note_borrowed_parameter(function, parameter, index);
        }
    }
}

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || !is_body_read_here(function, UNKNOWN_LOCATION)) {
        return;
    }
    // The constant evaluator refuses a read of an ended object in a consteval
    // function itself, so the rule reads no such function.
    tree result = DECL_RESULT(function);
    if (result == NULL_TREE || DECL_IMMEDIATE_FUNCTION_P(function) || !holds_borrow(TREE_TYPE(result), 0)) {
        return;
    }
    OriginWalk origins{function};
    tree body = DECL_SAVED_TREE(function);
    origins.record_locals(body);
    ReturnWalk walk{&origins, function};
    cp_walk_tree_without_duplicates(&body, check_return, &walk);
    note_borrowed_parameters(function, body);
}

void on_collection(void*, void*) { borrow_classes.clear(); }

}  // namespace

void report_view_escape(const Finding& finding) {
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
    escape_names.by_arguments = get_identifier("by_arguments");
    escape_names.payload_cell = get_identifier("payload_cell");
    escape_names.foundation = get_identifier("foundation");
    escape_names.fixy = get_identifier("fixy");
    escape_names.std_name = get_identifier("std");
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
