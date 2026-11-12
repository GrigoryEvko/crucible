// The family borrow match: the rule borrow-match.  The borrow match of an
// Option, a Result or a OneOf gives an arm a reference into the sum.  An arm
// that assigns the sum, moves it, or calls a member of it that can change it
// ends or changes the payload while the arm holds that reference: a use after
// free in code that compiles.  The rule refuses each such write.
//
// THE BORROW MATCH
//     A borrow match is a member function that carries the P3394 annotation
//     foundation::linear::borrow_match of include/foundation/Linear.h, also an
//     instance of a member template whose pattern carries it.  The match of a
//     const lvalue of Option, Result and OneOf carries it.  The rule reads the
//     annotation, and it names no function.
//
// THE RECEIVER AND THE ARMS
//     The receiver is the object of a call of a borrow match.  An arm is a
//     lambda argument of the call whose call operator has a parameter of a
//     reference type, because only such an arm holds a reference into the
//     payload.  The arm on_none of an Option gets no reference, so a write
//     there is no finding.
//
// THE PATHS
//     A path names an object: a variable or a parameter, and then a field or a
//     dereference for each step.  The receiver is a path.  In the body of an
//     arm, the use of a capture names a field of the closure, and the call
//     names the value that initializes each field: the address of an object
//     for a capture by reference, and the pointer for a capture of this.  A
//     dereference of such a field is the path of the object that its value
//     points at.  A capture by copy of an object names the copy, so a write to
//     it is no finding.  A local reference names the object of its
//     initializer, as the range of a loop over the sum does.  A static
//     variable is the root of its own path.
//
// THE WRITES
//     In the body of an arm, these writes are findings:
//       assignment     an assignment of the receiver or of an object that holds
//                      it, also through an assignment operator
//       move           std::move, std::forward to an rvalue, or a cast to an
//                      rvalue reference, of the receiver or of an object that
//                      holds it
//       mutating-call  a call of a member function of the receiver whose object
//                      parameter is not const
//     The entity of a finding is the kind of the write and the receiver.
//
// THE JOIN
//     PLUGIN_PRE_GENERICIZE gives the body of each arm before the body of the
//     function with the call, and the instance of a generic arm can come after
//     it.  So the rule records the writes of each arm at the event of its call
//     operator, and the receiver and the arms of each call at the event of its
//     function, by the TYPE_UID of each closure.  It joins the two after the
//     parse of the unit, when the front end made each instance.  A record holds
//     only identifiers, locations and text, so no collection can free a tree
//     that it holds.
//
// THE BODIES
//     The rule reads the bodies that the rule binding-hook reads (binding.cpp,
//     THE BODIES), for the arms and for the calls.  The place of a finding is
//     the place of the write.
//
// THE LIMITS
//     The rule reads the body of the arm only.  A write in a function that the
//     arm calls, such as a free function that takes the sum by a reference that
//     is not const, a write through a pointer or a second reference parameter
//     to the sum, and an arm that is no lambda argument of the call, such as a
//     named closure, give no finding.  The control control-known-gaps of utils/plants/borrow-match/
//     pins each of them.
//
// THE ERRORS
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

// A path (THE PATHS).  ROOT is the DECL_UID of the variable or the parameter,
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
};

const char* write_kind_name(WriteKind kind) {
    switch (kind) {
        case WriteKind::assignment:
            return "assignment";
        case WriteKind::move:
            return "move";
        case WriteKind::mutating_call:
            return "mutating-call";
    }
    return "write";
}

struct ArmWrite {
    Path target;
    WriteKind kind = WriteKind::assignment;
    location_t location = UNKNOWN_LOCATION;
};

// The arm of one call: the TYPE_UID of its closure, and the path of the object
// that the value of each field of a pointer or a reference type points at, by
// the DECL_UID of the field.
struct CallArm {
    unsigned closure = 0;
    std::unordered_map<int, Path> pointees;
};

struct MatchCall {
    Path receiver;
    std::vector<CallArm> arms;
};

// The identifiers that the family compares, interned one time.
struct Names {
    tree linear = NULL_TREE;
    tree borrow_match = NULL_TREE;
};

Names names;

// The writes of each arm that holds a reference, by the TYPE_UID of its
// closure, and each call of a borrow match (THE JOIN).
std::unordered_map<unsigned, std::vector<ArmWrite>> arm_writes;
std::vector<MatchCall> match_calls;

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

// True when the text of a path ends with the dereference of a pointer.
bool ends_with_arrow(const std::string& text) {
    return text.size() >= 2 && text.compare(text.size() - 2, 2, "->") == 0;
}

// The text of DECL in a path.
std::string name_text(tree decl) {
    return decl != NULL_TREE && DECL_P(decl) && DECL_NAME(decl) != NULL_TREE ? IDENTIFIER_POINTER(DECL_NAME(decl))
                                                                             : std::string("<anonymous>");
}

Path pointee_path(tree node, tree closure, int depth);

// The path of the lvalue NODE (THE PATHS), or a path with no root.  CLOSURE is
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
    if (VAR_P(node) && TREE_TYPE(node) != NULL_TREE && TYPE_REF_P(TREE_TYPE(node)) && !DECL_HAS_VALUE_EXPR_P(node)
        && !TREE_STATIC(node) && DECL_INITIAL(node) != NULL_TREE && DECL_INITIAL(node) != error_mark_node) {
        Path bound = pointee_path(DECL_INITIAL(node), closure, depth + 1);
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

// True when the call operator FUNCTION of an arm has a parameter of a
// reference type (THE RECEIVER AND THE ARMS).
bool takes_reference(tree function) {
    tree parameter = DECL_ARGUMENTS(function);
    if (parameter != NULL_TREE && DECL_OBJECT_MEMBER_FUNCTION_P(function)) {
        parameter = DECL_CHAIN(parameter);
    }
    for (; parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        if (TREE_TYPE(parameter) != NULL_TREE && TYPE_REF_P(TREE_TYPE(parameter))) {
            return true;
        }
    }
    return false;
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

void record_write(ArmWalk& walk, Path path, WriteKind kind, tree node) {
    location_t location = EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : walk.last_location;
    if (path.root == kNoRoot || location == UNKNOWN_LOCATION) {
        return;
    }
    walk.writes->push_back(ArmWrite{std::move(path), kind, location});
}

// True when the type TYPE of an object parameter lets the function change the
// object: a pointer or a reference to a type that is not const.
bool is_mutable_object_parameter(tree type) {
    return type != NULL_TREE && (TYPE_PTR_P(type) || TYPE_REF_P(type)) && TREE_TYPE(type) != NULL_TREE
        && !TYPE_READONLY(TREE_TYPE(type));
}

void record_call_write(ArmWalk& walk, tree call) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || call_expr_nargs(call) < 1) {
        return;
    }
    if (is_std_move_cast(callee)) {
        tree result = TREE_TYPE(TREE_TYPE(callee));
        if (result != NULL_TREE && TYPE_REF_P(result) && TYPE_REF_IS_RVALUE(result)) {
            record_write(walk, pointee_path(CALL_EXPR_ARG(call, 0), walk.closure, 0), WriteKind::move, call);
        }
        return;
    }
    if (DECL_LANG_SPECIFIC(callee) == nullptr || !DECL_OBJECT_MEMBER_FUNCTION_P(callee) || DECL_CONSTRUCTOR_P(callee)) {
        return;
    }
    tree parameters = TYPE_ARG_TYPES(TREE_TYPE(callee));
    if (parameters == NULL_TREE || !is_mutable_object_parameter(TREE_VALUE(parameters))) {
        return;
    }
    WriteKind kind = DECL_ASSIGNMENT_OPERATOR_P(callee) ? WriteKind::assignment : WriteKind::mutating_call;
    record_write(walk, pointee_path(CALL_EXPR_ARG(call, 0), walk.closure, 0), kind, call);
}

// The callback of cp_walk_tree over the body of an arm (THE WRITES).
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
        case MODIFY_EXPR:
            record_write(walk, lvalue_path(TREE_OPERAND(node, 0), walk.closure, 0), WriteKind::assignment, node);
            break;
        case CALL_EXPR:
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
    if (!takes_reference(function)) {
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

// The closure object of the argument NODE of a call: the CONSTRUCTOR of a
// lambda expression, or null.
tree closure_constructor(tree node) {
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
    if (node == NULL_TREE || TREE_CODE(node) != CONSTRUCTOR || TREE_TYPE(node) == NULL_TREE
        || !LAMBDA_TYPE_P(TREE_TYPE(node))) {
        return NULL_TREE;
    }
    return node;
}

void record_call(tree call) {
    MatchCall record;
    record.receiver = pointee_path(CALL_EXPR_ARG(call, 0), NULL_TREE, 0);
    if (record.receiver.root == kNoRoot) {
        return;
    }
    if (ends_with_arrow(record.receiver.text)) {
        record.receiver.text = "*" + record.receiver.text.substr(0, record.receiver.text.size() - 2);
    }
    for (int index = 1; index < call_expr_nargs(call); ++index) {
        tree constructor = closure_constructor(CALL_EXPR_ARG(call, index));
        if (constructor == NULL_TREE) {
            continue;
        }
        CallArm arm;
        arm.closure = TYPE_UID(TYPE_MAIN_VARIANT(TREE_TYPE(constructor)));
        unsigned position = 0;
        tree field = NULL_TREE;
        tree value = NULL_TREE;
        FOR_EACH_CONSTRUCTOR_ELT(CONSTRUCTOR_ELTS(constructor), position, field, value) {
            if (field == NULL_TREE || TREE_CODE(field) != FIELD_DECL || TREE_TYPE(field) == NULL_TREE
                || !(TYPE_PTR_P(TREE_TYPE(field)) || TYPE_REF_P(TREE_TYPE(field)))) {
                continue;
            }
            Path pointee = pointee_path(value, NULL_TREE, 0);
            if (pointee.root != kNoRoot) {
                arm.pointees.emplace(static_cast<int>(DECL_UID(field)), pointee);
            }
        }
        record.arms.push_back(std::move(arm));
    }
    if (!record.arms.empty()) {
        match_calls.push_back(std::move(record));
    }
}

tree walk_calls(tree* node_pointer, int* walk_subtrees, void*) {
    tree node = *node_pointer;
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(node) == CALL_EXPR && call_expr_nargs(node) >= 2
        && is_borrow_match(cp_get_callee_fndecl_nofold(node))) {
        record_call(node);
    }
    return NULL_TREE;
}

// ── The join ────────────────────────────────────────────────────────────

// The path in the frame of the call that the write WRITE of ARM names, or a
// path with no root (THE PATHS).  A closure path starts with the dereference
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

// True when the write of the kind KIND to WRITTEN reaches RECEIVER (THE
// WRITES): an assignment or a move of the receiver or of an object that holds
// it, or a mutating call of the receiver itself.
bool reaches(const Path& written, WriteKind kind, const Path& receiver) {
    if (written.root == kNoRoot || written.root != receiver.root || written.steps.size() > receiver.steps.size()) {
        return false;
    }
    if (kind == WriteKind::mutating_call && written.steps.size() != receiver.steps.size()) {
        return false;
    }
    return std::equal(written.steps.begin(), written.steps.end(), receiver.steps.begin());
}

// The join of the arms and the calls (THE JOIN).  Complexity: linear in the
// writes of the arms of each call, times the length of a path.
void finish_borrow_matches_now() {
    for (const MatchCall& call : match_calls) {
        for (const CallArm& arm : call.arms) {
            auto found = arm_writes.find(arm.closure);
            if (found == arm_writes.end()) {
                continue;
            }
            for (const ArmWrite& write : found->second) {
                if (!reaches(resolve(write, arm), write.kind, call.receiver)) {
                    continue;
                }
                Place place = place_of(write.location, Scope::quarantine);
                if (place.file != nullptr) {
                    add_finding(Kind::borrow_match, nullptr, place,
                                std::string(write_kind_name(write.kind)) + " " + call.receiver.text);
                }
            }
        }
    }
    match_calls.clear();
    arm_writes.clear();
}

// True when the rule reads the code of FUNCTION in this unit (THE BODIES).
bool is_read_here(tree function) {
    if (is_in_instantiation(function)) {
        Place place = place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine);
        return place.file_class == FileClass::base || place.file_class == FileClass::quarantined;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine);
    return place.file != nullptr && is_walked_file(place);
}

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    // A clone of a constructor or a destructor gets a second event on a copy
    // of a body that cp_genericize lowered (linear.cpp, on_pre_genericize_linear).
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || !is_read_here(function)) {
        return;
    }
    if (LAMBDA_FUNCTION_P(function)) {
        record_arm(function);
    }
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_calls, nullptr);
}

void on_collection(void*, void*) { borrow_match_answers.clear(); }

}  // namespace

void finish_borrow_matches() { finish_borrow_matches_now(); }

void report_borrow_match(const Finding& finding) {
    error_at(finding.spelling,
             "this arm of a borrow match writes the sum that the match reads (%s), and the payload that the arm "
             "borrows can end while the arm holds its reference",
             finding.entity.c_str());
    inform(finding.spelling,
           "the borrow match gives the arm a reference into the sum, and an assignment, a move or a mutating call of "
           "the sum ends or changes the payload that the reference names");
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
