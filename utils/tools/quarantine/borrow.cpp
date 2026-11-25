// The family borrow match: the rule borrow-match.  The borrow match of an
// Option, a Result or a OneOf gives an arm a reference into the sum.  An arm
// that assigns the sum, moves it, or calls a member of it that can change it
// ends or changes the payload while the arm holds that reference: a use after
// free in code that compiles.  The rule refuses each such write.
//
// Borrow match
//     A borrow match is a member function that carries the P3394 annotation
//     foundation::linear::borrow_match of include/foundation/Linear.h, also an
//     instance of a member template whose pattern carries it.  The match of a
//     const lvalue of Option, Result and OneOf carries it.  The rule reads the
//     annotation, and it names no function.
//
// Receiver and the arms
//     The receiver is the object of a call of a borrow match.  An arm is an
//     argument of the call that is an object of a class with a call operator
//     of at least one parameter: a lambda expression, a braced object of a
//     class, or a local variable of a closure or a class that the function of
//     the call initializes.  A parameter by value can hold a borrow too, such
//     as a View that a conversion makes from the payload, so the arm of a
//     parameter by value is an arm as well.  The arm on_none of an Option has
//     no parameter and gets no payload, so a write there is no finding.
//
// Loop
//     A range loop over a sum lends the payload too: its cursor and its
//     reference name the payload for the whole body.  The loop door begin() of
//     an Option carries the annotation, so the rule reads such a loop as a
//     borrow match: the range is the receiver, and the body of the loop is the
//     arm.  The front end lowers the loop into a cursor that the call of the
//     door initializes, an increment and a condition that name the cursor, and
//     the body.  So the rule records each local that a call of a borrow match
//     with the receiver as its one argument initializes, and it reads the body
//     of each loop whose increment or condition names such a local.  The body
//     is in the frame of the function, so its writes and the receiver need no
//     join.  A write after the end of the loop is no finding.  The entity of a
//     finding of a loop starts with "loop".
//
// Paths
//     A path names an object: a variable or a parameter, and then a field or a
//     dereference for each step.  The receiver is a path.  In the body of an
//     arm, the use of a capture names a field of the closure, and the call
//     names the value that initializes each field: the address of an object
//     for a capture by reference, and the pointer for a capture of this.  A
//     field of a pointer of a class arm names the object of its initializer
//     in the same way.  The value of each field of a named arm comes from its
//     initializer in the function of the call: one braced initializer, or one
//     store to each field.  A dereference of such a field is the path of the
//     object that its value points at.  A capture by copy of an object names
//     the copy, so a write to it is no finding.  A local reference names the
//     object of its initializer, as the range of a loop over the sum does.  A
//     receiver and the initializer of a local reference can go through std::move,
//     std::forward and std::as_const, which give their argument back.  A static
//     variable is the root of its own path.
//
// Writes
//     In the body of an arm, these writes are findings:
//       assignment     an assignment of the receiver or of an object that holds
//                      it, also through an assignment operator
//       move           std::move, std::forward to an rvalue, or a cast to an
//                      rvalue reference, of the receiver or of an object that
//                      holds it
//       mutating-call  a call of a member function of the receiver whose object
//                      parameter is not const
//       ending-call    a call that gives the receiver, or an object that holds
//                      it, to a parameter by a reference that is not const or
//                      to a capture of a closure, and that ends an owner of the
//                      receiver (the effects of live.cpp).  A callee with no
//                      body in the unit can end it
//     The entity of a finding is the kind of the write and the receiver, and for
//     an ending call the callee too.
//
// Calls
//     The front end gives a call as a CALL_EXPR, but a call whose result is a
//     class that a call cannot pass in registers (a class with a destructor, or
//     with a copy or a move that is not trivial) as an AGGR_INIT_EXPR.  The rule
//     reads the two forms, for a call of a borrow match and for a call in an
//     arm.  So a take of an Option of a payload with a destructor in a cast to
//     void, or in the initializer of a local, is a mutating call too.
//
// Join
//     PLUGIN_PRE_GENERICIZE gives the body of each arm before the body of the
//     function with the call, and the instance of a generic arm can come after
//     it.  So the rule records the writes of each arm at the event of its call
//     operator, and the receiver and the arms of each call at the event of its
//     function, by the TYPE_UID of each closure.  It joins the two after the
//     parse of the unit, when the front end made each instance.  A record holds
//     only identifiers, locations and text, so no collection can free a tree
//     that it holds.
//
// Bodies
//     The rule reads the bodies that is_body_read_here of plugin.h gives, as
//     the rule binding-hook does (binding.cpp, the bodies), for the arms and
//     for the calls.  The place of a finding is the place of the write.
//
// Limits
//     A write in a callee of the arm is a finding when it ends an owner of the
//     receiver.  A callee that changes a payload with no end, such as an int,
//     gives no finding.  A write through a pointer or a second reference
//     parameter to the sum, an arm that a function of a different unit or a
//     parameter of the caller gives, and a receiver behind a call, such as the
//     operator-> of a Box, give no finding.  The control control-known-gaps of
//     utils/plants/borrow-match/ pins each of them.  The owning storage of a sum
//     (foundation/core/Choice.h) counts the borrow matches that lend its
//     payload, and a write of the sum while a match lends the payload ends the
//     process, in each build.  A cursor counts no lend, so an owning Option has
//     no loop, and the limits of the body of a loop over a niche or a plain
//     Option are those of an arm.
//
// Errors
//     Each finding is a compile error in each build and each mode
//     (is_each_mode_kind of plugin.h), and an opt-out region covers it.  The
//     message does not start with "quarantine: ", so the driver of the
//     negative fixtures keeps it.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The step of a dereference in a path.  A field step is the DECL_UID of the
// field, which is never negative.
constexpr int kDereference = -1;

// The root of a path that names no object.  A DECL_UID is never negative.
constexpr int kNoRoot = -1;

// A path (the paths).  ROOT is the DECL_UID of the variable or the parameter,
// or kNoRoot.  IS_CLOSURE is true when the root is the object parameter of the
// call operator of an arm.
struct Path {
    int root = kNoRoot;
    bool is_closure = false;
    std::vector<int> steps;
    std::string text;  // the spelling of the receiver, for the entity
};

enum class WriteKind : std::uint8_t {
    assignment,
    move,
    mutating_call,
    ending_call,
};

const char* write_kind_name(WriteKind kind) {
    switch (kind) {
        case WriteKind::assignment:
            return "assignment";
        case WriteKind::move:
            return "move";
        case WriteKind::mutating_call:
            return "mutating-call";
        case WriteKind::ending_call:
            return "ending-call";
    }
    return "write";
}

// A write of an arm (the writes).  An ending call has the parameter or the capture
// that takes the object, and the name of its callee.  IS_OBJECT tells that the call
// is a member function of that object.
struct ArmWrite {
    Path target;
    WriteKind kind = WriteKind::assignment;
    location_t location = UNKNOWN_LOCATION;
    Taking taking;
    std::string callee;
    bool is_object = false;
};

// The receiver of a borrow match, and the key of the owners that it is or owns
// (owner_key of live.cpp).
struct Receiver {
    Path path;
    unsigned owners = kOwnsNothing;
};

// The arm of one call: the TYPE_UID of its closure, and the path of the object
// that the value of each field of a pointer or a reference type points at, by
// the DECL_UID of the field.
struct CallArm {
    unsigned closure = 0;
    std::unordered_map<int, Path> pointees;
};

struct MatchCall {
    Receiver receiver;
    std::vector<CallArm> arms;
};

// The identifiers that the family compares, interned one time.
struct Names {
    tree linear = NULL_TREE;
    tree borrow_match = NULL_TREE;
};

Names names;

// A write of the body of a loop to the sum that the loop reads (the loop).
struct LoopWrite {
    ArmWrite write;
    Receiver receiver;
};

// The writes of each arm that holds a reference, by the TYPE_UID of its
// closure, each call of a borrow match (the join), and each write of the body
// of a loop over a sum (the loop).
std::unordered_map<unsigned, std::vector<ArmWrite>> arm_writes;
std::vector<MatchCall> match_calls;
std::vector<LoopWrite> loop_writes;

// The answer of is_borrow_match for each function, keyed by the declaration.
// Each collection clears it, because a collection can free a tree and reuse
// its address.
std::unordered_map<tree, bool> borrow_match_answers;

bool is_borrow_match(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    auto found = borrow_match_answers.find(function);
    if (found != borrow_match_answers.end()) {
        return found->second;
    }
    bool answer = function_carries_base_annotation(function, names.borrow_match, names.linear);
    borrow_match_answers.emplace(function, answer);
    return answer;
}

tree strip_wrappers(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case PAREN_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            default:
                return node;
        }
    }
    return node;
}

// NODE with each wrapper taken off, and each call of std::move, std::forward and
// std::as_const that the front end did not fold: such a call gives its argument
// back.
tree strip_identity_calls(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = strip_wrappers(node);
        if (node == NULL_TREE || (TREE_CODE(node) != CALL_EXPR && TREE_CODE(node) != AGGR_INIT_EXPR)) {
            return node;
        }
        tree callee = cp_get_callee_fndecl_nofold(node);
        if (argument_count(node) < 1 || !(is_std_move_cast(callee) || is_std_as_const(callee))) {
            return node;
        }
        node = argument_at(node, 0);
    }
    return node;
}

// True when the text of a path ends with the dereference of a pointer.
bool ends_with_arrow(const std::string& text) {
    return text.size() >= 2 && text.compare(text.size() - 2, 2, "->") == 0;
}

// The text of DECL in a path.
std::string name_text(tree decl) {
    return decl != NULL_TREE && DECL_P(decl) && DECL_NAME(decl) != NULL_TREE ? IDENTIFIER_POINTER(DECL_NAME(decl))
                                                                             : std::string("<anonymous>");
}

// The value of each local reference of the body in hand that one store
// initializes, by its DECL_UID.  The front end gives a value with a call as a
// store, and each walk visits it before a use of the reference.
std::unordered_map<int, tree> reference_stores;

bool is_local_reference(tree node) {
    return VAR_P(node) && TREE_TYPE(node) != NULL_TREE && TYPE_REF_P(TREE_TYPE(node)) && !DECL_HAS_VALUE_EXPR_P(node)
        && !TREE_STATIC(node);
}

// The initializer of the local reference NODE, or null.
tree reference_initializer(tree node) {
    if (!is_local_reference(node)) {
        return NULL_TREE;
    }
    if (DECL_INITIAL(node) != NULL_TREE) {
        return DECL_INITIAL(node) != error_mark_node ? DECL_INITIAL(node) : NULL_TREE;
    }
    auto found = reference_stores.find(static_cast<int>(DECL_UID(node)));
    return found != reference_stores.end() ? found->second : NULL_TREE;
}

// Records the store of VALUE that initializes a local reference.
void record_reference(tree target, tree value) {
    target = strip_wrappers(target);
    if (target != NULL_TREE && is_local_reference(target)) {
        reference_stores.emplace(static_cast<int>(DECL_UID(target)), value);
    }
}

Path pointee_path(tree node, tree closure, int depth);

// The path of the lvalue NODE (the paths), or a path with no root.  CLOSURE is
// the object parameter of the call operator of an arm, or null.  Complexity:
// linear in the depth of the expression.
Path lvalue_path(tree node, tree closure, int depth) {
    node = strip_wrappers(node);
    if (node == NULL_TREE || depth > kTypeDepth) {
        return {};
    }
    switch (TREE_CODE(node)) {
        case COMPONENT_REF: {
            tree field = TREE_OPERAND(node, 1);
            if (TREE_CODE(field) != FIELD_DECL) {
                return {};
            }
            Path path = lvalue_path(TREE_OPERAND(node, 0), closure, depth + 1);
            if (path.root == kNoRoot) {
                return path;
            }
            path.steps.push_back(static_cast<int>(DECL_UID(field)));
            if (!DECL_FIELD_IS_BASE(field)) {
                path.text += (ends_with_arrow(path.text) ? "" : ".") + name_text(field);
            }
            return path;
        }
        case INDIRECT_REF:
            return pointee_path(TREE_OPERAND(node, 0), closure, depth + 1);
        case VAR_DECL:
            // A capture proxy of an arm, and each other variable with a value
            // expression, names the object of that expression.
            if (DECL_HAS_VALUE_EXPR_P(node)) {
                Path path = lvalue_path(DECL_VALUE_EXPR(node), closure, depth + 1);
                path.text = name_text(node);
                return path;
            }
            [[fallthrough]];
        case PARM_DECL:
        case RESULT_DECL: {
            Path path;
            path.root = static_cast<int>(DECL_UID(node));
            path.is_closure = closure != NULL_TREE && node == closure;
            path.text = name_text(node);
            return path;
        }
        default:
            return {};
    }
}

// The path of the object that the pointer or the reference value NODE points
// at, or a path with no root.
Path pointee_path(tree node, tree closure, int depth) {
    node = strip_wrappers(node);
    if (node == NULL_TREE || depth > kTypeDepth) {
        return {};
    }
    if (TREE_CODE(node) == ADDR_EXPR) {
        return lvalue_path(TREE_OPERAND(node, 0), closure, depth + 1);
    }
    // A local reference names the object of its initializer for its whole
    // scope, as the range of a loop over the sum does.
    if (tree initializer = reference_initializer(node)) {
        Path bound = pointee_path(strip_identity_calls(initializer), closure, depth + 1);
        if (bound.root != kNoRoot) {
            return bound;
        }
    }
    Path path = lvalue_path(node, closure, depth + 1);
    if (path.root != kNoRoot) {
        path.steps.push_back(kDereference);
        // A reference names its object with no operator in the source.
        bool is_reference = TREE_TYPE(node) != NULL_TREE && TYPE_REF_P(TREE_TYPE(node));
        path.text += is_reference ? "" : "->";
    }
    return path;
}

// ── The arms ────────────────────────────────────────────────────────────

// True when FUNCTION is the call operator of an arm: a member operator() of a
// class, with at least one parameter (the receiver and the arms).
bool is_arm_operator(tree function) {
    if (DECL_LANG_SPECIFIC(function) == nullptr || !DECL_OVERLOADED_OPERATOR_IS(function, CALL_EXPR)
        || !DECL_FUNCTION_MEMBER_P(function)) {
        return false;
    }
    tree parameter = DECL_ARGUMENTS(function);
    if (parameter != NULL_TREE && DECL_OBJECT_MEMBER_FUNCTION_P(function)) {
        parameter = DECL_CHAIN(parameter);
    }
    return parameter != NULL_TREE;
}

// The state of the walk of the body of one arm.  The instance of a generic arm
// can lose the location of an expression, so a write with no location takes
// the location of the last node before it that has one, such as its
// statement.  The walk visits a node before its operands.
struct ArmWalk {
    tree closure = NULL_TREE;
    std::vector<ArmWrite>* writes = nullptr;
    location_t last_location = UNKNOWN_LOCATION;
};

// Records a write of PATH at NODE, and gives the record, or null for a path with no
// root or a write with no place.
ArmWrite* record_write(ArmWalk& walk, Path path, WriteKind kind, tree node) {
    location_t location = EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : walk.last_location;
    if (path.root == kNoRoot || location == UNKNOWN_LOCATION) {
        return nullptr;
    }
    ArmWrite write;
    write.target = std::move(path);
    write.kind = kind;
    write.location = location;
    walk.writes->push_back(std::move(write));
    return &walk.writes->back();
}

// True when the type TYPE of an object parameter lets the function change the
// object: a pointer or a reference to a type that is not const.
bool is_mutable_object_parameter(tree type) {
    return type != NULL_TREE && (TYPE_PTR_P(type) || TYPE_REF_P(type)) && TREE_TYPE(type) != NULL_TREE
        && !TYPE_READONLY(TREE_TYPE(type));
}

// The argument NODE of a call with each wrapper, address and temporary taken
// off: the CONSTRUCTOR of a lambda expression or of a braced object, or the
// variable that names the arm.
tree arm_object(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = strip_wrappers(node);
        if (TREE_CODE(node) == ADDR_EXPR) {
            node = TREE_OPERAND(node, 0);
            continue;
        }
        if (TREE_CODE(node) == TARGET_EXPR) {
            node = TARGET_EXPR_INITIAL(node);
            continue;
        }
        break;
    }
    return node;
}

// True for a field of a pointer or a reference type.
bool is_indirect_field(tree field) {
    return field != NULL_TREE && TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE
        && (TYPE_PTR_P(TREE_TYPE(field)) || TYPE_REF_P(TREE_TYPE(field)));
}

// Records each object that CALL gives to CALLEE by a reference that is not const or
// by a capture of a closure (the writes).
void record_taken_objects(ArmWalk& walk, tree call, tree callee) {
    if (fndecl_built_in_p(callee)) {
        return;
    }
    bool const is_member = DECL_LANG_SPECIFIC(callee) != nullptr && DECL_OBJECT_MEMBER_FUNCTION_P(callee);
    bool const is_constructor = is_member && DECL_CONSTRUCTOR_P(callee);
    tree parameter = TYPE_ARG_TYPES(TREE_TYPE(callee));
    for (int index = 0; index < argument_count(call); ++index) {
        tree type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
        parameter = parameter != NULL_TREE ? TREE_CHAIN(parameter) : NULL_TREE;
        if (index == 0 && is_constructor) {
            continue;
        }
        tree argument = argument_at(call, index);
        if (is_mutable_object_parameter(type) && !(TYPE_REF_P(type) && TYPE_REF_IS_RVALUE(type))) {
            ArmWrite* write = record_write(walk, pointee_path(argument, walk.closure, 0), WriteKind::ending_call, call);
            if (write != nullptr) {
                write->taking = taking_of_parameter(callee, static_cast<std::size_t>(index), TREE_TYPE(type));
                write->callee = qualified_name(callee);
                write->is_object = index == 0 && is_member;
            }
        }
        tree object = arm_object(argument);
        if (object == NULL_TREE || TREE_CODE(object) != CONSTRUCTOR || !CLASS_TYPE_P(TREE_TYPE(object))
            || !LAMBDA_TYPE_P(TYPE_MAIN_VARIANT(TREE_TYPE(object)))) {
            continue;
        }
        unsigned position = 0;
        tree field = NULL_TREE;
        tree value = NULL_TREE;
        FOR_EACH_CONSTRUCTOR_ELT(CONSTRUCTOR_ELTS(object), position, field, value) {
            if (!is_indirect_field(field)) {
                continue;
            }
            ArmWrite* write = record_write(walk, pointee_path(value, walk.closure, 0), WriteKind::ending_call, call);
            if (write != nullptr) {
                write->taking = taking_of_capture(TYPE_UID(TYPE_MAIN_VARIANT(TREE_TYPE(object))), DECL_UID(field));
                write->callee = qualified_name(callee);
            }
        }
    }
}

// CALL is a CALL_EXPR, or the AGGR_INIT_EXPR of a call whose result is a
// class with a destructor or a copy that is not trivial (the calls).
void record_call_write(ArmWalk& walk, tree call) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || argument_count(call) < 1) {
        return;
    }
    if (is_std_move_cast(callee)) {
        tree result = TREE_TYPE(TREE_TYPE(callee));
        if (result != NULL_TREE && TYPE_REF_P(result) && TYPE_REF_IS_RVALUE(result)) {
            record_write(walk, pointee_path(argument_at(call, 0), walk.closure, 0), WriteKind::move, call);
        }
        return;
    }
    record_taken_objects(walk, call, callee);
    if (DECL_LANG_SPECIFIC(callee) == nullptr || !DECL_OBJECT_MEMBER_FUNCTION_P(callee) || DECL_CONSTRUCTOR_P(callee)) {
        return;
    }
    tree parameters = TYPE_ARG_TYPES(TREE_TYPE(callee));
    if (parameters == NULL_TREE || !is_mutable_object_parameter(TREE_VALUE(parameters))) {
        return;
    }
    WriteKind kind = DECL_ASSIGNMENT_OPERATOR_P(callee) ? WriteKind::assignment : WriteKind::mutating_call;
    record_write(walk, pointee_path(argument_at(call, 0), walk.closure, 0), kind, call);
}

// The callback of cp_walk_tree over the body of an arm (the writes).
tree walk_arm_writes(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<ArmWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last_location = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        case INIT_EXPR:
            record_reference(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
            break;
        case MODIFY_EXPR:
            record_write(walk, lvalue_path(TREE_OPERAND(node, 0), walk.closure, 0), WriteKind::assignment, node);
            break;
        case CALL_EXPR:
        case AGGR_INIT_EXPR:
            record_call_write(walk, node);
            break;
        case NOP_EXPR:
        case CONVERT_EXPR:
        case STATIC_CAST_EXPR: {
            tree type = TREE_TYPE(node);
            if (type != NULL_TREE && TYPE_REF_P(type) && TYPE_REF_IS_RVALUE(type)) {
                record_write(walk, pointee_path(TREE_OPERAND(node, 0), walk.closure, 0), WriteKind::move, node);
            }
            break;
        }
        default:
            break;
    }
    return NULL_TREE;
}

void record_arm(tree function) {
    if (!is_arm_operator(function)) {
        return;
    }
    tree closure_type = DECL_CONTEXT(function);
    std::vector<ArmWrite> writes;
    ArmWalk walk;
    // A static call operator has no object parameter, and its arm captures
    // nothing.
    walk.closure = DECL_OBJECT_MEMBER_FUNCTION_P(function) ? DECL_ARGUMENTS(function) : NULL_TREE;
    walk.writes = &writes;
    walk.last_location = DECL_SOURCE_LOCATION(function);
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_arm_writes, &walk);
    if (!writes.empty()) {
        auto& recorded = arm_writes[TYPE_UID(TYPE_MAIN_VARIANT(closure_type))];
        recorded.insert(recorded.end(), writes.begin(), writes.end());
    }
}

// ── The calls ───────────────────────────────────────────────────────────

// The value of each field of a pointer or a reference type of one arm object:
// the path of the object that the value points at, by the DECL_UID of the
// field (the paths).
using Pointees = std::unordered_map<int, Path>;

// The state of the walk of the calls of one function: the pointees of each
// local variable of a class type, and the receiver of each cursor of a loop
// door (the loop), by the DECL_UID of the variable.  The walk visits an
// initializer before a later call that names the variable.
struct CallWalk {
    std::unordered_map<int, Pointees> named;
    std::unordered_map<int, Receiver> cursors;
};

// The receiver of a call of a borrow match: the object of its first argument,
// the object parameter of a member or the reference parameter of a free
// door.  A receiver that ends with the dereference of a pointer is spelled
// with a star.
Receiver receiver_of(tree call) {
    tree object = strip_identity_calls(argument_at(call, 0));
    Receiver receiver;
    receiver.path = pointee_path(object, NULL_TREE, 0);
    std::string& text = receiver.path.text;
    if (ends_with_arrow(text)) {
        text = "*" + text.substr(0, text.size() - 2);
    }
    tree type = object != NULL_TREE ? TREE_TYPE(object) : NULL_TREE;
    if (receiver.path.root != kNoRoot && is_indirect_type(type)) {
        receiver.owners = owner_key(TREE_TYPE(type));
    }
    return receiver;
}

// The call of a loop door that initializes a cursor: a call of a borrow match
// whose one argument is the receiver (the loop), or null.
tree loop_door_call(tree initializer) {
    for (int depth = 0; initializer != NULL_TREE && depth < kTypeDepth; ++depth) {
        initializer = strip_wrappers(initializer);
        if (TREE_CODE(initializer) != TARGET_EXPR) {
            break;
        }
        initializer = TARGET_EXPR_INITIAL(initializer);
    }
    if (initializer == NULL_TREE || (TREE_CODE(initializer) != CALL_EXPR && TREE_CODE(initializer) != AGGR_INIT_EXPR)) {
        return NULL_TREE;
    }
    bool const is_loop_door =
        argument_count(initializer) == 1 && is_borrow_match(cp_get_callee_fndecl_nofold(initializer));
    return is_loop_door ? initializer : NULL_TREE;
}

// Records the receiver of a cursor that a call of a loop door initializes.
// LEFT is the initialized object and RIGHT its value.
void record_cursor(CallWalk& walk, tree left, tree right) {
    left = strip_wrappers(left);
    if (left == NULL_TREE || !VAR_P(left)) {
        return;
    }
    tree call = loop_door_call(right);
    if (call == NULL_TREE) {
        return;
    }
    Receiver receiver = receiver_of(call);
    if (receiver.path.root != kNoRoot) {
        walk.cursors[static_cast<int>(DECL_UID(left))] = std::move(receiver);
    }
}

// True when TYPE can be the type of an arm: a class.  The join reads only the
// classes whose call operator recorded the writes of an arm (the receiver and
// the arms, the join).
bool is_arm_class(tree type) { return type != NULL_TREE && CLASS_TYPE_P(type); }

// The pointees of the CONSTRUCTOR of an arm object.
Pointees pointees_of(tree constructor) {
    Pointees pointees;
    unsigned position = 0;
    tree field = NULL_TREE;
    tree value = NULL_TREE;
    FOR_EACH_CONSTRUCTOR_ELT(CONSTRUCTOR_ELTS(constructor), position, field, value) {
        if (!is_indirect_field(field)) {
            continue;
        }
        Path pointee = pointee_path(value, NULL_TREE, 0);
        if (pointee.root != kNoRoot) {
            pointees.emplace(static_cast<int>(DECL_UID(field)), pointee);
        }
    }
    return pointees;
}

// Records the initializer of a local variable of an arm class: a braced
// initializer of the whole variable, or a store to one field of a pointer or
// a reference type, as the front end splits an initializer that is no
// constant.  LEFT is the stored object and RIGHT its value.
void record_initializer(CallWalk& walk, tree left, tree right) {
    left = strip_wrappers(left);
    if (left == NULL_TREE) {
        return;
    }
    if (VAR_P(left) && is_arm_class(TREE_TYPE(left))) {
        tree value = arm_object(right);
        if (value != NULL_TREE && TREE_CODE(value) == CONSTRUCTOR) {
            walk.named[static_cast<int>(DECL_UID(left))] = pointees_of(value);
        }
        return;
    }
    if (TREE_CODE(left) != COMPONENT_REF) {
        return;
    }
    tree object = strip_wrappers(TREE_OPERAND(left, 0));
    tree field = TREE_OPERAND(left, 1);
    if (object == NULL_TREE || !VAR_P(object) || !is_arm_class(TREE_TYPE(object)) || !is_indirect_field(field)) {
        return;
    }
    Path pointee = pointee_path(right, NULL_TREE, 0);
    if (pointee.root != kNoRoot) {
        walk.named[static_cast<int>(DECL_UID(object))][static_cast<int>(DECL_UID(field))] = pointee;
    }
}

// CALL is a CALL_EXPR or an AGGR_INIT_EXPR of a borrow match (the calls).
void record_call(const CallWalk& walk, tree call) {
    MatchCall record;
    record.receiver = receiver_of(call);
    if (record.receiver.path.root == kNoRoot) {
        return;
    }
    for (int index = 1; index < argument_count(call); ++index) {
        tree object = arm_object(argument_at(call, index));
        if (object == NULL_TREE || !is_arm_class(TREE_TYPE(object))) {
            continue;
        }
        CallArm arm;
        arm.closure = TYPE_UID(TYPE_MAIN_VARIANT(TREE_TYPE(object)));
        if (TREE_CODE(object) == CONSTRUCTOR) {
            arm.pointees = pointees_of(object);
        } else if (VAR_P(object)) {
            auto found = walk.named.find(static_cast<int>(DECL_UID(object)));
            if (found != walk.named.end()) {
                arm.pointees = found->second;
            } else if (DECL_INITIAL(object) != NULL_TREE && TREE_CODE(DECL_INITIAL(object)) == CONSTRUCTOR) {
                arm.pointees = pointees_of(DECL_INITIAL(object));
            }
        }
        record.arms.push_back(std::move(arm));
    }
    if (!record.arms.empty()) {
        match_calls.push_back(std::move(record));
    }
}

// The callback of cp_walk_tree over the initializer of a loop: it records
// each cursor of a loop door (the loop) and nothing else.
tree walk_cursors(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<CallWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(node) == INIT_EXPR || TREE_CODE(node) == MODIFY_EXPR) {
        record_reference(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
        record_cursor(walk, TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
    } else if (TREE_CODE(node) == DECL_EXPR) {
        if (tree decl = DECL_EXPR_DECL(node); VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
            record_cursor(walk, decl, DECL_INITIAL(decl));
        }
    }
    return NULL_TREE;
}

// The state of the search for a cursor in the increment or the condition of
// a loop: the cursors of the walk, and the receiver of the first one found.
struct CursorSearch {
    const std::unordered_map<int, Receiver>* cursors = nullptr;
    const Receiver* receiver = nullptr;
};

tree find_cursor(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& search = *static_cast<CursorSearch*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (VAR_P(node)) {
        auto found = search.cursors->find(static_cast<int>(DECL_UID(node)));
        if (found != search.cursors->end()) {
            search.receiver = &found->second;
            return node;
        }
    }
    return NULL_TREE;
}

// The receiver of the loop LOOP when its increment or its condition names a
// cursor of a loop door, or null (the loop).
const Receiver* loop_receiver(CallWalk& walk, tree loop) {
    tree init = FOR_INIT_STMT(loop);
    if (init != NULL_TREE) {
        cp_walk_tree_without_duplicates(&init, walk_cursors, &walk);
    }
    if (walk.cursors.empty()) {
        return nullptr;
    }
    CursorSearch search;
    search.cursors = &walk.cursors;
    for (tree part : {FOR_EXPR(loop), FOR_COND(loop)}) {
        if (part != NULL_TREE && search.receiver == nullptr) {
            cp_walk_tree_without_duplicates(&part, find_cursor, &search);
        }
    }
    return search.receiver;
}

bool reaches(const Path& written, const ArmWrite& write, const Path& receiver);

// Records each write of the body of a loop over a sum to that sum (the loop).
// Complexity: linear in the body, times the length of a path.
void record_loop(CallWalk& walk, tree loop) {
    const Receiver* receiver = loop_receiver(walk, loop);
    tree body = FOR_BODY(loop);
    if (receiver == nullptr || body == NULL_TREE) {
        return;
    }
    std::vector<ArmWrite> writes;
    ArmWalk arm_walk;
    arm_walk.writes = &writes;
    arm_walk.last_location = EXPR_LOCATION(loop);
    cp_walk_tree_without_duplicates(&body, walk_arm_writes, &arm_walk);
    for (ArmWrite& write : writes) {
        if (reaches(write.target, write, receiver->path)) {
            loop_writes.push_back(LoopWrite{std::move(write), *receiver});
        }
    }
}

tree walk_calls(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<CallWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    switch (TREE_CODE(node)) {
        case INIT_EXPR:
        case MODIFY_EXPR:
            record_reference(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
            record_initializer(walk, TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
            record_cursor(walk, TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
            break;
        case DECL_EXPR:
            if (tree decl = DECL_EXPR_DECL(node); VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
                record_initializer(walk, decl, DECL_INITIAL(decl));
                record_cursor(walk, decl, DECL_INITIAL(decl));
            }
            break;
        case FOR_STMT:
            record_loop(walk, node);
            break;
        case CALL_EXPR:
        case AGGR_INIT_EXPR:
            if (argument_count(node) >= 2 && is_borrow_match(cp_get_callee_fndecl_nofold(node))) {
                record_call(walk, node);
            }
            break;
        default:
            break;
    }
    return NULL_TREE;
}

// ── The join ────────────────────────────────────────────────────────────

// The path in the frame of the call that the write WRITE of ARM names, or a
// path with no root (the paths).  A closure path starts with the dereference
// of the object parameter and a field of the closure.  Only a dereference of
// a field of a pointer or a reference type reaches an object of the caller.
Path resolve(const ArmWrite& write, const CallArm& arm) {
    const Path& target = write.target;
    if (!target.is_closure) {
        return target;
    }
    if (target.steps.size() < 3 || target.steps[0] != kDereference || target.steps[2] != kDereference) {
        return {};
    }
    auto found = arm.pointees.find(target.steps[1]);
    if (found == arm.pointees.end()) {
        return {};
    }
    Path resolved = found->second;
    resolved.steps.insert(resolved.steps.end(), target.steps.begin() + 3, target.steps.end());
    return resolved;
}

// True when WRITE, which names WRITTEN in the frame of the call, reaches RECEIVER
// (the writes): an assignment or a move of the receiver or of an object that holds
// it, a mutating call of the receiver itself, or a call that takes one of them.  A
// member function of the receiver itself is a mutating call and no ending call.
bool reaches(const Path& written, const ArmWrite& write, const Path& receiver) {
    if (written.root == kNoRoot || written.root != receiver.root || written.steps.size() > receiver.steps.size()) {
        return false;
    }
    bool const is_receiver = written.steps.size() == receiver.steps.size();
    if (write.kind == WriteKind::mutating_call && !is_receiver) {
        return false;
    }
    if (write.kind == WriteKind::ending_call && write.is_object && is_receiver) {
        return false;
    }
    return std::equal(written.steps.begin(), written.steps.end(), receiver.steps.begin());
}

// Adds the finding of WRITE, which reaches RECEIVER, with PREFIX before its entity.
// An ending call is a finding when it ends an owner of the receiver.
void add_write(const ArmWrite& write, const Receiver& receiver, const char* prefix) {
    std::string entity = std::string(prefix) + write_kind_name(write.kind) + " " + receiver.path.text;
    if (write.kind == WriteKind::ending_call) {
        CallEnd const end = call_end(write.taking, receiver.owners);
        if (end == CallEnd::none) {
            return;
        }
        entity += " in " + write.callee;
        if (end == CallEnd::unseen) {
            entity += ", which reaches a function with no body in this unit";
        }
    }
    Place place = place_of(write.location, Scope::quarantine);
    if (place.file != nullptr) {
        add_finding(Kind::borrow_match, nullptr, place, entity);
    }
}

// The join of the arms and the calls (the join).  Complexity: linear in the
// writes of the arms of each call, times the length of a path.
void finish_borrow_matches_now() {
    for (const MatchCall& call : match_calls) {
        for (const CallArm& arm : call.arms) {
            auto found = arm_writes.find(arm.closure);
            if (found == arm_writes.end()) {
                continue;
            }
            for (const ArmWrite& write : found->second) {
                if (reaches(resolve(write, arm), write, call.receiver.path)) {
                    add_write(write, call.receiver, "");
                }
            }
        }
    }
    for (const LoopWrite& loop : loop_writes) {
        add_write(loop.write, loop.receiver, "loop ");
    }
    match_calls.clear();
    arm_writes.clear();
    loop_writes.clear();
}

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    // A clone of a constructor or a destructor gets a second event on a copy
    // of a body that cp_genericize lowered (linear.cpp, on_pre_genericize_linear).
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || !is_body_read_here(function, UNKNOWN_LOCATION)) {
        return;
    }
    record_arm(function);
    CallWalk walk;
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_calls, &walk);
    reference_stores.clear();
}

void on_collection(void*, void*) { borrow_match_answers.clear(); }

}  // namespace

void finish_borrow_matches() { finish_borrow_matches_now(); }

void report_borrow_match(const Finding& finding) {
    const std::string loop_mark = "loop ";
    if (finding.entity.compare(0, loop_mark.size(), loop_mark) == 0) {
        error_at(finding.spelling,
                 "this body of a loop over a sum writes the sum that the loop reads (%s), and the payload that the "
                 "reference of the loop names can end while the body holds it",
                 finding.entity.c_str() + loop_mark.size());
        inform(finding.spelling,
               "the loop gives its body a reference into the sum, and an assignment, a move or a mutating call of "
               "the sum ends or changes the payload that the reference names");
        inform(finding.spelling, "read the payload in a match or in the loop, and change the sum after the end of the "
                                 "loop");
        inform(finding.spelling,
               "a test of the write itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region "
               "of %<foundation/Quarantine.h%>");
        return;
    }
    error_at(finding.spelling,
             "this arm of a borrow match writes the sum that the match reads (%s), and the payload that the arm "
             "borrows can end while the arm holds its reference",
             finding.entity.c_str());
    inform(finding.spelling,
           "the borrow match gives the arm a reference into the sum, and an assignment, a move or a mutating call of "
           "the sum ends or changes the payload that the reference names, and so does a call that ends an owner of "
           "the sum");
    inform(finding.spelling,
           "match the rvalue, %<std::move(sum).match(...)%>, which moves the payload out first, or change the sum "
           "after the match");
    inform(finding.spelling,
           "a test of the write itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

void register_borrow_match(const char* plugin_name) {
    names.linear = get_identifier("linear");
    names.borrow_match = get_identifier("borrow_match");
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
