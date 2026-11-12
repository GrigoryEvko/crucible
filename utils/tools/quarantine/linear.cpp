// The family linear: the rule must-consume.  Each path of a body uses a
// linear value exactly one time (misc/12_10_2026_linear.md, P1).  The rule
// reads the C++ statement tree of each body at PLUGIN_PRE_GENERICIZE, before
// cp_genericize lowers it.  It walks the paths of the body forward, with the
// states dead, live, consumed and maybe for each object, and with a fixed point
// for each loop.  It runs only with the argument must-consume=on of plugin.cpp.
//
// THE LINEAR TYPES
//     The marker of include/foundation/Linear.h decides, as the trait
//     is_linear_type does.  A class or a union is linear when it carries the
//     P3394 annotation foundation::linear::linear.  GCC keeps an annotation as
//     an attribute that annotation_p() accepts, and the value of the attribute
//     has the type of the annotation.  So an instance of linear_when<false>
//     carries not_linear and is not linear by its annotation.  A class that
//     can move and holds a linear base, a linear member or an array of linear
//     elements by value is linear too, also a closure with a linear capture.
//     A reference and a pointer hold no value.  The end of the scope of a value
//     of a class that carries scope_end_door is a door, because the destructor
//     of the class releases its resource.  So is the end of the scope of a
//     value whose linear parts each have such a class.  The rule reads whether
//     a class can move from the constructors that the class declares, and it
//     declares no implicit member and instantiates nothing.
//
// THE OBJECTS
//     The rule tracks a linear local, a structured binding of a linear type, a
//     parameter of a linear class by value and a parameter of an rvalue
//     reference to a linear class.  It does not track a static or a
//     thread-local variable, a capture, the hidden variable of a structured
//     binding, or the explicit object parameter of a member function.  It
//     tracks no object of a move constructor, of a move assignment, or of a
//     constructor or an assignment of a class that carries the annotation,
//     because these functions define the transfer.
//
// THE CONSUMES
//     An object is consumed when it reaches one of these places, through
//     casts, std::move and std::forward only:
//       * an argument of a parameter of an rvalue reference or of a class by
//         value, of a call that is not std::move or std::forward
//       * the object argument of a member function with the qualifier &&
//       * the value of a return
//       * the source of an initialization or an assignment of a linear
//         object, also of a member, and an element of a braced list, such as
//         a capture of a lambda.
//     A call of a function that does not return ends its path, and so does a
//     throw.  The rule reads no exception path, because the tree does not
//     throw (CLAUDE.md section XII).
//
// THE FOLDS
//     The front end replaces an initializer whose value is a constant with
//     that value.  A move of an empty token gives a constant, so the fold
//     removes the consume from the tree.  The fifth patch of GCC
//     (utils/toolchain/gcc/patches/0005-*.patch) gives the event
//     PLUGIN_FOLDED_INITIALIZER with the initializer before the fold, and the
//     rule reads that tree in place of the folded tree.  The front end also
//     replaces a trivial assignment of an empty class with its source, and the
//     event PLUGIN_EMPTY_CLASS_ASSIGNMENT gives the target of the assignment.
//     A source with a side effect stays in the tree, and the rule finds the
//     assignment there.  The front end removes a source with no side effect,
//     and the statement keeps a void constant.  The rule then reads each such
//     event whose location is in the statement: after the start of the
//     statement and before the start of the next statement.  The front end
//     collects no garbage between such an event and the PLUGIN_PRE_GENERICIZE
//     of its function, and each collection clears the records of the events.
//
// THE DOORS
//     A door ends each linear value of its frame (P10 of the plan).  A
//     function that carries the annotation drop_door or detach_door is a door,
//     and so is each function that a linear-door row of the rule table names.
//     The rule gives no finding unconsumed, unconsumed-on-path or
//     discarded-temporary in the body of a door.  It still gives
//     double-consume and assign-over-live there.  It gives the record door for
//     each linear-door row whose function it reads, and the module of the rule
//     refuses a row with no record (the rule table is the roster of the doors).
//
// THE BODIES
//     The rule reads each body of a quarantined file in each unit.  It reads a
//     body of a base file that is no instance of a template only in the
//     sentinel unit of that file (THE SENTINEL of plugin.h), and an instance
//     of a template in each unit that makes it.  It skips a clone of a
//     constructor or a destructor, because GCC gives a clone a second event on
//     a body that cp_genericize lowered.  It skips each body of a unit with an
//     error.  A parse and an object compile read the same trees.
//
// THE KINDS
//     unconsumed           the object leaves its scope at an exit, and no
//                          path to that exit consumes it
//     unconsumed-on-path   the object leaves its scope at an exit, and only
//                          some paths to that exit consume it
//     double-consume       a consume of an object that a path consumed
//     assign-over-live     an assignment to an object that holds a value that
//                          a path did not consume: the old value drops
//     discarded-temporary  a linear temporary that the code discards, or binds
//                          to an lvalue reference and then destroys
//     unanalyzed           a function with a goto or a label, which the rule
//                          does not read
//     door                 no finding: a linear-door row that the unit read
//     The entity is FUNCTION: OBJECT.  FUNCTION has no template argument, so
//     each instance of one template gives the same line, and the key of a
//     ledger row holds no line of an exit.
//
// THE LIMITS
//     The rule reads one function at a time.  A consume through a reference,
//     an accessor or a member call of a door is not visible to it, and a
//     consume that depends on a correlated branch needs a switch or a door.  A
//     template that no unit instantiates with a linear type gives no finding.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// ── The linear types ────────────────────────────────────────────────────

enum class Linearity : std::uint8_t {
    none,
    direct,
    derived,
};

// The facts of one type: its linearity, and whether the end of the scope of a
// value of the type is a door (THE LINEAR TYPES).
struct TypeFacts {
    Linearity linearity = Linearity::none;
    bool ends_at_scope = false;
};

// The identifiers that the family compares, interned one time.
struct LinearNames {
    tree foundation = NULL_TREE;
    tree linear = NULL_TREE;  // the namespace foundation::linear and its class linear
    tree drop_door = NULL_TREE;
    tree detach_door = NULL_TREE;
    tree scope_end_door = NULL_TREE;
    tree move = NULL_TREE;
    tree forward = NULL_TREE;
    tree move_if_noexcept = NULL_TREE;
    tree forward_like = NULL_TREE;
};

LinearNames linear_names;

// The answers of the type walks, keyed by the main variant of a class.  Each
// collection clears them, because a collection can free a tree and reuse its
// address.
struct LinearCaches {
    std::unordered_map<tree, TypeFacts> facts;
    std::unordered_map<tree, bool> can_move;
};

LinearCaches linear_caches;

// THE FOLDS: each folded tree with the object and the initializer before the
// fold, and the target of each trivial assignment of an empty class by its
// source.
struct FoldEntry {
    tree object = NULL_TREE;
    tree original = NULL_TREE;
};

std::unordered_map<tree, std::vector<FoldEntry>> folded_initializers;

struct EmptyAssignment {
    tree target = NULL_TREE;
    tree source = NULL_TREE;
    location_t location = UNKNOWN_LOCATION;
};

std::vector<EmptyAssignment> empty_assignments;
std::unordered_map<tree, std::size_t> empty_assignment_of_source;

tree strip_conversions(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        if (TREE_CODE(node) != NOP_EXPR && TREE_CODE(node) != CONVERT_EXPR && TREE_CODE(node) != NON_LVALUE_EXPR
            && TREE_CODE(node) != VIEW_CONVERT_EXPR) {
            return node;
        }
        node = TREE_OPERAND(node, 0);
    }
    return node;
}

// The class of the annotation ATTRIBUTE, or null when ATTRIBUTE is no
// annotation.  The value of an annotation is a tree with the type of the
// annotation, with a const qualifier.
tree annotation_class(tree attribute) {
    if (!annotation_p(attribute)) {
        return NULL_TREE;
    }
    tree value = TREE_VALUE(attribute);
    if (value == NULL_TREE || TREE_CODE(value) != TREE_LIST || TREE_VALUE(value) == NULL_TREE) {
        return NULL_TREE;
    }
    tree type = TREE_TYPE(TREE_VALUE(value));
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type)) {
        return NULL_TREE;
    }
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type) ? type : NULL_TREE;
}

// True when TYPE is the class foundation::linear::NAME of the vocabulary of
// include/foundation/Linear.h.
bool is_vocabulary_class(tree type, tree name) {
    tree decl = TYPE_NAME(type);
    if (decl == NULL_TREE || TREE_CODE(decl) != TYPE_DECL || DECL_NAME(decl) != name) {
        return false;
    }
    tree scope = CP_DECL_CONTEXT(decl);
    if (scope == NULL_TREE || TREE_CODE(scope) != NAMESPACE_DECL || DECL_NAME(scope) != linear_names.linear) {
        return false;
    }
    tree top = CP_DECL_CONTEXT(scope);
    return top != NULL_TREE && TREE_CODE(top) == NAMESPACE_DECL && DECL_NAME(top) == linear_names.foundation
        && CP_DECL_CONTEXT(top) == global_namespace;
}

// True when ATTRIBUTES hold an annotation of the class foundation::linear::NAME.
bool carries_annotation(tree attributes, tree name) {
    for (tree attribute = attributes; attribute != NULL_TREE; attribute = TREE_CHAIN(attribute)) {
        tree annotation = annotation_class(attribute);
        if (annotation != NULL_TREE && is_vocabulary_class(annotation, name)) {
            return true;
        }
    }
    return false;
}

tree element_type_of(tree type) {
    while (type != NULL_TREE && TREE_CODE(type) == ARRAY_TYPE) {
        type = TREE_TYPE(type);
    }
    return type;
}

// True when a value of the complete class TYPE can move: its declared move
// constructor is not deleted, or the implicit move constructor can move each
// base and each member.  With no move constructor, a declared copy
// constructor that is not deleted moves it.  The walk declares no implicit
// member, so it changes no tree.  A class that holds a linear part has no
// implicit copy constructor that is not deleted, so a class with no move
// constructor and no declared copy constructor cannot move.  Complexity: the
// walk visits each class one time.
bool can_move(tree type, int depth) {
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || depth > kTypeDepth) {
        return true;
    }
    auto found = linear_caches.can_move.find(type);
    if (found != linear_caches.can_move.end()) {
        return found->second;
    }
    // The answer while the walk is open, so that a cycle through a member
    // ends.
    linear_caches.can_move.emplace(type, true);
    bool result = false;
    bool is_decided = false;
    for (ovl_iterator constructor(CLASSTYPE_CONSTRUCTORS(type)); constructor; ++constructor) {
        tree function = *constructor;
        if (TREE_CODE(function) == FUNCTION_DECL && move_fn_p(function)) {
            result = !DECL_DELETED_FN(function);
            is_decided = true;
            break;
        }
    }
    if (!is_decided && CLASSTYPE_LAZY_MOVE_CTOR(type)) {
        result = true;
        if (tree binfo = TYPE_BINFO(type)) {
            for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo) && result; ++index) {
                result = can_move(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
            }
        }
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE && result; field = DECL_CHAIN(field)) {
            if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
                && !TYPE_REF_P(TREE_TYPE(field))) {
                result = can_move(TREE_TYPE(field), depth + 1);
            }
        }
        is_decided = true;
    }
    if (!is_decided) {
        for (ovl_iterator constructor(CLASSTYPE_CONSTRUCTORS(type)); constructor; ++constructor) {
            tree function = *constructor;
            if (TREE_CODE(function) == FUNCTION_DECL && copy_fn_p(function) > 0) {
                result = !DECL_DELETED_FN(function);
                break;
            }
        }
    }
    linear_caches.can_move[type] = result;
    return result;
}

// The facts of a type (THE LINEAR TYPES).  An array has the facts of its
// element.  Complexity: the walk visits each class one time.
TypeFacts facts_of(tree type, int depth = 0) {
    if (type == NULL_TREE || type == error_mark_node || depth > kTypeDepth || !TYPE_P(type)) {
        return {};
    }
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type)) {
        return {};
    }
    auto found = linear_caches.facts.find(type);
    if (found != linear_caches.facts.end()) {
        return found->second;
    }
    // A class that holds itself through a member is no type, and the answer
    // keeps the walk finite while the answer is open.
    linear_caches.facts.emplace(type, TypeFacts{});
    TypeFacts result;
    tree attributes = TYPE_ATTRIBUTES(type);
    bool is_scope_door = carries_annotation(attributes, linear_names.scope_end_door);
    if (carries_annotation(attributes, linear_names.linear)) {
        result.linearity = Linearity::direct;
        result.ends_at_scope = is_scope_door;
    } else {
        bool holds_linear = false;
        bool each_part_ends = true;
        auto add_part = [&](tree part) {
            TypeFacts part_facts = facts_of(part, depth + 1);
            if (part_facts.linearity != Linearity::none) {
                holds_linear = true;
                each_part_ends = each_part_ends && part_facts.ends_at_scope;
            }
        };
        if (tree binfo = TYPE_BINFO(type)) {
            for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
                add_part(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)));
            }
        }
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
            if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
                && !TYPE_REF_P(TREE_TYPE(field))) {
                add_part(TREE_TYPE(field));
            }
        }
        if (holds_linear && can_move(type, 0)) {
            result.linearity = Linearity::derived;
            result.ends_at_scope = is_scope_door || each_part_ends;
        }
    }
    linear_caches.facts[type] = result;
    return result;
}

Linearity linearity_of(tree type) { return facts_of(type).linearity; }

bool is_linear_type(tree type) { return linearity_of(type) != Linearity::none; }

// True when a temporary of TYPE drops a linear value at its end: TYPE is
// linear, and the end of its scope is no door.
bool is_dropped_temporary_type(tree type) {
    TypeFacts facts = facts_of(type);
    return facts.linearity != Linearity::none && !facts.ends_at_scope;
}

// True when TYPE is a linear class or a reference to one.
bool is_linear_or_reference(tree type) {
    if (type != NULL_TREE && TYPE_P(type) && TYPE_REF_P(type)) {
        type = TREE_TYPE(type);
    }
    return is_linear_type(type);
}

// True for std::move, std::forward, std::move_if_noexcept and
// std::forward_like: a cast whose value goes on to the caller.
bool is_move_cast(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    tree name = DECL_NAME(function);
    if (name != linear_names.move && name != linear_names.forward && name != linear_names.move_if_noexcept
        && name != linear_names.forward_like) {
        return false;
    }
    tree scope = CP_DECL_CONTEXT(function);
    return scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && DECL_NAMESPACE_STD_P(scope);
}

// The type of the function that a call names, or null.
tree function_type_of(tree call, tree callee) {
    if (callee != NULL_TREE) {
        return TREE_TYPE(callee);
    }
    if (TREE_CODE(call) != CALL_EXPR || CALL_EXPR_FN(call) == NULL_TREE) {
        return NULL_TREE;
    }
    tree type = TREE_TYPE(CALL_EXPR_FN(call));
    if (type != NULL_TREE && POINTER_TYPE_P(type)) {
        type = TREE_TYPE(type);
    }
    return type != NULL_TREE && FUNC_OR_METHOD_TYPE_P(type) ? type : NULL_TREE;
}

// The name of a function in an entity, with no template argument.  A
// constructor and a destructor have the name of their class.
std::string function_text(tree function) {
    tree scope = DECL_CONTEXT(function);
    bool is_special = DECL_CONSTRUCTOR_P(function) || DECL_DESTRUCTOR_P(function);
    if (!is_special || scope == NULL_TREE || !TYPE_P(scope) || TYPE_NAME(scope) == NULL_TREE) {
        return qualified_name(function);
    }
    std::string name = qualified_name(TYPE_NAME(scope));
    std::string last = simple_name(TYPE_NAME(scope));
    return name + (DECL_DESTRUCTOR_P(function) ? "::~" : "::") + last;
}

// ── The analysis of one function ────────────────────────────────────────

// The status of one tracked object on the paths that reach a point.
enum class Status : std::uint8_t {
    dead,  // not declared yet, or out of scope
    live,  // each path holds the value
    consumed,  // each path consumed it
    maybe,  // some paths consumed it
};

struct FlowState {
    bool is_reachable = true;
    std::vector<Status> status;

    [[nodiscard]] Status get(std::size_t slot) const { return slot < status.size() ? status[slot] : Status::dead; }
    void set(std::size_t slot, Status value) {
        if (slot >= status.size()) {
            status.resize(slot + 1, Status::dead);
        }
        status[slot] = value;
    }
};

FlowState unreachable_state() {
    FlowState state_of_no_path;
    state_of_no_path.is_reachable = false;
    return state_of_no_path;
}

// The join of two states at a point that two paths reach.  The join is
// monotone, so a fixed point of a loop stays above each state of its passes.
FlowState join(const FlowState& left, const FlowState& right) {
    if (!left.is_reachable) {
        return right;
    }
    if (!right.is_reachable) {
        return left;
    }
    FlowState joined;
    std::size_t size = std::max(left.status.size(), right.status.size());
    joined.status.resize(size, Status::dead);
    for (std::size_t slot = 0; slot < size; ++slot) {
        Status one = left.get(slot);
        Status other = right.get(slot);
        if (one == other || other == Status::dead) {
            joined.status[slot] = one;
        } else if (one == Status::dead) {
            joined.status[slot] = other;
        } else {
            joined.status[slot] = Status::maybe;
        }
    }
    return joined;
}

bool is_same_state(const FlowState& left, const FlowState& right) {
    if (left.is_reachable != right.is_reachable) {
        return false;
    }
    std::size_t size = std::max(left.status.size(), right.status.size());
    for (std::size_t slot = 0; slot < size; ++slot) {
        if (left.get(slot) != right.get(slot)) {
            return false;
        }
    }
    return true;
}

struct Slot {
    tree decl = NULL_TREE;
    // The end of the scope of the object is a door (THE LINEAR TYPES).
    bool ends_at_scope = false;
    bool has_live_exit = false;
    bool has_maybe_exit = false;
    bool was_double_reported = false;
    bool was_assign_reported = false;
};

// The target of a break or a continue: a loop or a switch.
struct Target {
    std::size_t scope_depth = 0;
    bool is_loop = false;
    std::vector<FlowState> breaks;
    std::vector<FlowState> continues;
};

struct SwitchContext {
    FlowState entry;
    bool has_default = false;
};

struct PendingFinding {
    Kind kind;
    location_t location;
    std::string entity;
};

// The bound of the passes of the fixed point of one loop.  The lattice of one
// object has the height 3, so the head of a loop stops changing after a few
// passes.  A loop that reaches the bound keeps the state of its last pass.
constexpr int kLoopPasses = 6;

class Analysis {
public:
    Analysis(tree function, bool is_door) : function_(function), is_door_(is_door) {}

    void run() {
        scopes_.emplace_back();
        FlowState entry;
        if (!is_exempt_function()) {
            for (tree parameter = DECL_ARGUMENTS(function_); parameter != NULL_TREE;
                 parameter = DECL_CHAIN(parameter)) {
                std::size_t slot = 0;
                if (track(parameter, slot)) {
                    scopes_.back().push_back(slot);
                    entry.set(slot, Status::live);
                }
            }
        }
        walk_statement(DECL_SAVED_TREE(function_), entry);
        if (entry.is_reachable) {
            exit_scopes(entry, 0, true);
        }
        report();
    }

private:
    tree function_;
    bool is_door_;
    std::vector<Slot> slots_;
    std::unordered_map<tree, std::size_t> slot_of_;
    // Each lvalue reference local that borrows a tracked object, and that
    // object.  A move through the reference consumes the object.
    std::unordered_map<tree, tree> referent_of_;
    std::vector<std::vector<std::size_t>> scopes_;
    std::vector<Target*> targets_;
    std::vector<SwitchContext*> switches_;
    std::vector<PendingFinding> pending_;
    // The originals of THE FOLDS that the walk reads at this time.  A folded
    // tree can be a subtree of its original, so the walk does not read an
    // original inside itself.
    std::vector<tree> open_originals_;
    // The sources of the empty-class assignments that the walk reads at this
    // time (THE FOLDS), so that the walk of a source does not read its
    // assignment again.
    std::vector<tree> open_assignments_;
    // The location of the statement after the one that the walk reads, or
    // UNKNOWN_LOCATION at the end of the body.
    location_t next_statement_ = UNKNOWN_LOCATION;
    bool is_unanalyzed_ = false;

    bool is_exempt_function() const {
        if (DECL_LANG_SPECIFIC(function_) != nullptr && move_fn_p(function_)) {
            return true;
        }
        tree scope = DECL_CONTEXT(function_);
        bool is_special = DECL_CONSTRUCTOR_P(function_) || DECL_ASSIGNMENT_OPERATOR_P(function_);
        return is_special && scope != NULL_TREE && TYPE_P(scope) && linearity_of(scope) == Linearity::direct;
    }

    // Gives DECL a slot when it is a tracked object (THE OBJECTS).
    bool track(tree decl, std::size_t& slot) {
        auto found = slot_of_.find(decl);
        if (found != slot_of_.end()) {
            slot = found->second;
            return true;
        }
        tree type = TREE_TYPE(decl);
        if (type == NULL_TREE || type == error_mark_node) {
            return false;
        }
        if (VAR_P(decl)) {
            bool is_binding = DECL_DECOMPOSITION_P(decl) && !DECL_DECOMP_IS_BASE(decl);
            if (TREE_STATIC(decl) || DECL_EXTERNAL(decl) || DECL_THREAD_LOCAL_P(decl)
                || (DECL_HAS_VALUE_EXPR_P(decl) && !is_binding) || (DECL_ARTIFICIAL(decl) && !is_binding)) {
                return false;
            }
            if (TYPE_REF_P(type)) {
                if (!is_binding) {
                    return false;
                }
                type = TREE_TYPE(type);
            }
        } else if (TREE_CODE(decl) == PARM_DECL) {
            if (DECL_ARTIFICIAL(decl) || is_object_parameter(decl)) {
                return false;
            }
            if (TYPE_REF_P(type)) {
                if (!TYPE_REF_IS_RVALUE(type)) {
                    return false;
                }
                type = TREE_TYPE(type);
            }
        } else {
            return false;
        }
        TypeFacts facts = facts_of(type);
        if (facts.linearity == Linearity::none) {
            return false;
        }
        slot = slots_.size();
        Slot entry;
        entry.decl = decl;
        entry.ends_at_scope = facts.ends_at_scope;
        slots_.push_back(entry);
        slot_of_.emplace(decl, slot);
        return true;
    }

    // True when PARAMETER is the object parameter of the function: the
    // implicit this, or the explicit object parameter.
    bool is_object_parameter(tree parameter) const {
        if (parameter != DECL_ARGUMENTS(function_)) {
            return false;
        }
        return DECL_IOBJ_MEMBER_FUNCTION_P(function_) || DECL_XOBJ_MEMBER_FUNCTION_P(function_);
    }

    // The initializer before the fold of NODE, or NODE (THE FOLDS).  OBJECT is
    // the object that NODE initializes, or null.  An entry of that object comes
    // first.
    tree unfolded(tree node, tree object) const {
        if (node == NULL_TREE) {
            return node;
        }
        auto found = folded_initializers.find(node);
        if (found == folded_initializers.end() || found->second.empty()) {
            return node;
        }
        tree original = found->second.front().original;
        for (const FoldEntry& entry : found->second) {
            if (object != NULL_TREE && entry.object == object) {
                original = entry.original;
                break;
            }
        }
        if (std::find(open_originals_.begin(), open_originals_.end(), original) != open_originals_.end()) {
            return node;
        }
        return original;
    }

    // The tracked object that NODE names through the casts of THE CONSUMES, or
    // null.  SAW_RVALUE is true when a cast to an rvalue reference or std::move
    // is on the way.
    tree strip_to_tracked(tree node, bool& saw_rvalue) const {
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            node = unfolded(node, NULL_TREE);
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case IMPLICIT_CONV_EXPR:
                case STATIC_CAST_EXPR:
                    if (TREE_TYPE(node) != NULL_TREE && TYPE_REF_P(TREE_TYPE(node))
                        && TYPE_REF_IS_RVALUE(TREE_TYPE(node))) {
                        saw_rvalue = true;
                    }
                    node = TREE_OPERAND(node, 0);
                    continue;
                case ADDR_EXPR:
                case INDIRECT_REF:
                case CLEANUP_POINT_EXPR:
                case SAVE_EXPR:
                case PAREN_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case COMPOUND_EXPR: {
                    // An argument of an empty class by value is the argument
                    // and an EMPTY_CLASS_EXPR after it.
                    bool is_empty_argument =
                        TREE_CODE(tree_strip_any_location_wrapper(TREE_OPERAND(node, 1))) == EMPTY_CLASS_EXPR;
                    node = TREE_OPERAND(node, is_empty_argument ? 0 : 1);
                    continue;
                }
                case TARGET_EXPR:
                    node = TARGET_EXPR_INITIAL(node);
                    continue;
                case CALL_EXPR: {
                    tree callee = cp_get_callee_fndecl_nofold(node);
                    if (is_move_cast(callee) && call_expr_nargs(node) >= 1) {
                        saw_rvalue = saw_rvalue || DECL_NAME(callee) != linear_names.forward;
                        node = CALL_EXPR_ARG(node, 0);
                        continue;
                    }
                    return NULL_TREE;
                }
                case VAR_DECL:
                case PARM_DECL: {
                    if (slot_of_.count(node) != 0) {
                        return node;
                    }
                    auto referent = referent_of_.find(node);
                    return referent != referent_of_.end() ? referent->second : NULL_TREE;
                }
                default:
                    return NULL_TREE;
            }
        }
        return NULL_TREE;
    }

    // True when NODE is a linear temporary through the casts, whose end is no
    // door.
    bool is_linear_temporary(tree node) const {
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case ADDR_EXPR:
                case CLEANUP_POINT_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case TARGET_EXPR:
                    return is_dropped_temporary_type(TREE_TYPE(node));
                default:
                    return false;
            }
        }
        return false;
    }

    std::string entity_of(tree decl) const { return function_text(function_) + ": " + simple_name(decl); }

    void add_pending(Kind kind, location_t location, const std::string& entity) {
        pending_.push_back(PendingFinding{kind, location, entity});
    }

    void consume(tree decl, FlowState& state, location_t location) {
        if (!state.is_reachable) {
            return;
        }
        std::size_t slot = slot_of_.at(decl);
        Status status = state.get(slot);
        if ((status == Status::consumed || status == Status::maybe) && !slots_[slot].was_double_reported) {
            slots_[slot].was_double_reported = true;
            add_pending(Kind::linear_double_consume, location, entity_of(decl));
        }
        if (status != Status::dead) {
            state.set(slot, Status::consumed);
        }
    }

    void assign_over(tree decl, FlowState& state, location_t location) {
        if (!state.is_reachable) {
            return;
        }
        std::size_t slot = slot_of_.at(decl);
        Status status = state.get(slot);
        if ((status == Status::live || status == Status::maybe) && !slots_[slot].was_assign_reported) {
            slots_[slot].was_assign_reported = true;
            add_pending(Kind::linear_assign_over_live, location, entity_of(decl));
        }
        state.set(slot, Status::live);
    }

    // Notes the status of each object of the scopes from DEPTH on at an exit.
    // KILL makes them dead in STATE.
    void exit_scopes(FlowState& state, std::size_t depth, bool kill) {
        if (!state.is_reachable) {
            return;
        }
        for (std::size_t scope = depth; scope < scopes_.size(); ++scope) {
            for (std::size_t slot : scopes_[scope]) {
                Status status = state.get(slot);
                if (slots_[slot].ends_at_scope) {
                    // The end of the scope is the door of the object.
                } else if (status == Status::live) {
                    slots_[slot].has_live_exit = true;
                } else if (status == Status::maybe) {
                    slots_[slot].has_maybe_exit = true;
                }
                if (kill) {
                    state.set(slot, Status::dead);
                }
            }
        }
    }

    location_t location_or(tree node, location_t fallback) const {
        return node != NULL_TREE && EXPR_P(node) && EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : fallback;
    }

    location_t function_location() const { return DECL_SOURCE_LOCATION(function_); }

    // ── Statements ──

    void walk_statement(tree node, FlowState& state) {
        if (node == NULL_TREE || node == error_mark_node) {
            return;
        }
        switch (TREE_CODE(node)) {
            case STATEMENT_LIST: {
                location_t outer_next = next_statement_;
                for (tree_stmt_iterator item = tsi_start(node); !tsi_end_p(item); tsi_next(&item)) {
                    location_t next = outer_next;
                    tree_stmt_iterator ahead = item;
                    for (tsi_next(&ahead); !tsi_end_p(ahead); tsi_next(&ahead)) {
                        tree statement = tsi_stmt(ahead);
                        if (statement != NULL_TREE && EXPR_P(statement) && EXPR_HAS_LOCATION(statement)) {
                            next = EXPR_LOCATION(statement);
                            break;
                        }
                    }
                    next_statement_ = next;
                    walk_statement(tsi_stmt(item), state);
                }
                next_statement_ = outer_next;
                return;
            }
            case BIND_EXPR:
                scopes_.emplace_back();
                for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                    std::size_t slot = 0;
                    if (track(variable, slot)) {
                        scopes_.back().push_back(slot);
                    }
                }
                walk_statement(BIND_EXPR_BODY(node), state);
                exit_scopes(state, scopes_.size() - 1, true);
                scopes_.pop_back();
                return;
            case DECL_EXPR:
                walk_declaration(DECL_EXPR_DECL(node), state);
                return;
            case CLEANUP_STMT:
                // The cleanup runs at each exit of the body.  It is no consume.
                walk_statement(CLEANUP_BODY(node), state);
                return;
            case IF_STMT: {
                walk_statement(IF_COND(node), state);
                tree condition = strip_conversions(IF_COND(node));
                if (IF_STMT_CONSTEXPR_P(node) && condition != NULL_TREE && TREE_CODE(condition) == INTEGER_CST) {
                    walk_statement(integer_zerop(condition) ? ELSE_CLAUSE(node) : THEN_CLAUSE(node), state);
                    return;
                }
                FlowState then_state = state;
                FlowState else_state = state;
                walk_statement(THEN_CLAUSE(node), then_state);
                walk_statement(ELSE_CLAUSE(node), else_state);
                state = join(then_state, else_state);
                return;
            }
            case WHILE_STMT:
                walk_loop(WHILE_COND(node), WHILE_BODY(node), NULL_TREE, false, false, state);
                return;
            case FOR_STMT:
                walk_statement(FOR_INIT_STMT(node), state);
                walk_loop(FOR_COND(node), FOR_BODY(node), FOR_EXPR(node), false, false, state);
                return;
            case DO_STMT:
                walk_loop(DO_COND(node), DO_BODY(node), NULL_TREE, true, false, state);
                return;
            case RANGE_FOR_STMT:
                walk_expression(RANGE_FOR_EXPR(node), state);
                walk_loop(boolean_true_node, RANGE_FOR_BODY(node), NULL_TREE, false, true, state);
                return;
            case SWITCH_STMT:
                walk_switch(node, state);
                return;
            case CASE_LABEL_EXPR:
                if (!switches_.empty()) {
                    state = join(state, switches_.back()->entry);
                    if (CASE_LOW(node) == NULL_TREE) {
                        switches_.back()->has_default = true;
                    }
                }
                return;
            case BREAK_STMT:
                if (!targets_.empty() && state.is_reachable) {
                    Target* target = targets_.back();
                    FlowState broken = state;
                    exit_scopes(broken, target->scope_depth, true);
                    target->breaks.push_back(broken);
                }
                state.is_reachable = false;
                return;
            case CONTINUE_STMT:
                for (auto target = targets_.rbegin(); target != targets_.rend(); ++target) {
                    if ((*target)->is_loop) {
                        if (state.is_reachable) {
                            FlowState continued = state;
                            exit_scopes(continued, (*target)->scope_depth, true);
                            (*target)->continues.push_back(continued);
                        }
                        break;
                    }
                }
                state.is_reachable = false;
                return;
            case RETURN_EXPR:
                walk_expression(TREE_OPERAND(node, 0), state);
                exit_scopes(state, 0, false);
                state.is_reachable = false;
                return;
            case GOTO_EXPR:
            case LABEL_EXPR:
                is_unanalyzed_ = true;
                return;
            case TRY_BLOCK:
                walk_try(node, state);
                return;
            case EH_SPEC_BLOCK:
                walk_statement(EH_SPEC_STMTS(node), state);
                return;
            case USING_STMT:
                return;
            case EXPR_STMT:
                if (is_void_constant(EXPR_STMT_EXPR(node))) {
                    walk_removed_assignments(node, state);
                    return;
                }
                check_discarded(EXPR_STMT_EXPR(node));
                walk_expression(EXPR_STMT_EXPR(node), state);
                return;
            default:
                break;
        }
        if (STATEMENT_CLASS_P(node)) {
            // A statement that the rule does not name: each operand in order.
            int length = TREE_OPERAND_LENGTH(node);
            for (int index = 0; index < length; ++index) {
                walk_statement(TREE_OPERAND(node, index), state);
            }
            return;
        }
        check_discarded(node);
        walk_expression(node, state);
    }

    // A try block: a handler starts from the state before the block.
    void walk_try(tree node, FlowState& state) {
        FlowState entry = state;
        walk_statement(TRY_STMTS(node), state);
        auto walk_handler = [&](tree handler) {
            if (handler != NULL_TREE && TREE_CODE(handler) == HANDLER) {
                FlowState caught = entry;
                walk_statement(HANDLER_BODY(handler), caught);
                state = join(state, caught);
            }
        };
        tree handlers = TRY_HANDLERS(node);
        if (handlers != NULL_TREE && TREE_CODE(handlers) == STATEMENT_LIST) {
            for (tree_stmt_iterator item = tsi_start(handlers); !tsi_end_p(item); tsi_next(&item)) {
                walk_handler(tsi_stmt(item));
            }
        } else {
            walk_handler(handlers);
        }
    }

    void walk_declaration(tree decl, FlowState& state) {
        if (decl == NULL_TREE || !VAR_P(decl) || TREE_STATIC(decl) || DECL_EXTERNAL(decl)) {
            return;
        }
        tree initial = DECL_INITIAL(decl);
        if (initial != NULL_TREE && initial != error_mark_node) {
            initial = unfolded(initial, decl);
            open_originals_.push_back(initial);
            walk_expression(initial, state);
            bool saw_rvalue = false;
            tree source = strip_to_tracked(initial, saw_rvalue);
            open_originals_.pop_back();
            // An lvalue reference local borrows its object, and a move through
            // it consumes the object.  The base of a structured binding by
            // reference gives the object to its bindings.
            tree decl_type = TREE_TYPE(decl);
            bool is_borrow = decl_type != NULL_TREE && TYPE_REF_P(decl_type) && !TYPE_REF_IS_RVALUE(decl_type)
                          && !(DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl));
            if (source != NULL_TREE && source != decl && is_borrow) {
                referent_of_[decl] = source;
            } else if (source != NULL_TREE && source != decl && (saw_rvalue || is_linear_or_reference(decl_type))) {
                consume(source, state, location_or(initial, DECL_SOURCE_LOCATION(decl)));
            }
        }
        std::size_t slot = 0;
        bool is_tracked = slot_of_.count(decl) != 0;
        if (!is_tracked && track(decl, slot)) {
            // A tracked object with no BIND_EXPR joins the innermost scope.
            scopes_.back().push_back(slot);
            is_tracked = true;
        }
        if (is_tracked && state.is_reachable) {
            state.set(slot_of_.at(decl), Status::live);
        }
        // The bindings of a structured binding, which have no DECL_EXPR of
        // their own.
        if (DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl) && state.is_reachable) {
            for (const auto& [tracked, tracked_slot] : slot_of_) {
                if (VAR_P(tracked) && DECL_DECOMPOSITION_P(tracked) && !DECL_DECOMP_IS_BASE(tracked)
                    && DECL_DECOMP_BASE(tracked) == decl) {
                    state.set(tracked_slot, Status::live);
                }
            }
        }
    }

    // A loop: a fixed point of the state at its head.  MAY_SKIP is true for a
    // range-based loop, whose body can run zero times.
    void walk_loop(tree condition, tree body, tree step, bool is_do, bool may_skip, FlowState& state) {
        tree stripped = strip_conversions(condition);
        bool is_constant = stripped != NULL_TREE && TREE_CODE(stripped) == INTEGER_CST;
        bool is_always_true = !may_skip && (condition == NULL_TREE || (is_constant && !integer_zerop(stripped)));
        bool is_always_false = is_constant && integer_zerop(stripped);
        FlowState entry = state;
        FlowState head = entry;
        FlowState exit = unreachable_state();
        for (int pass = 0; pass < kLoopPasses; ++pass) {
            Target target;
            target.scope_depth = scopes_.size();
            target.is_loop = true;
            targets_.push_back(&target);
            FlowState current = head;
            FlowState leaving = unreachable_state();
            if (!is_do) {
                if (!may_skip) {
                    walk_statement(condition, current);
                }
                if (!is_always_true) {
                    leaving = join(leaving, current);
                }
                if (is_always_false) {
                    current.is_reachable = false;
                }
                walk_statement(body, current);
                for (const FlowState& continued : target.continues) {
                    current = join(current, continued);
                }
                walk_statement(step, current);
            } else {
                walk_statement(body, current);
                for (const FlowState& continued : target.continues) {
                    current = join(current, continued);
                }
                walk_statement(condition, current);
                if (!is_always_true) {
                    leaving = join(leaving, current);
                }
                if (is_always_false) {
                    current.is_reachable = false;
                }
            }
            for (const FlowState& broken : target.breaks) {
                leaving = join(leaving, broken);
            }
            targets_.pop_back();
            exit = leaving;
            FlowState next_head = join(entry, current);
            if (is_same_state(next_head, head)) {
                break;
            }
            head = next_head;
        }
        state = exit;
    }

    void walk_switch(tree node, FlowState& state) {
        walk_statement(SWITCH_STMT_COND(node), state);
        SwitchContext context;
        context.entry = state;
        Target target;
        target.scope_depth = scopes_.size();
        target.is_loop = false;
        targets_.push_back(&target);
        switches_.push_back(&context);
        FlowState current = unreachable_state();
        walk_statement(SWITCH_STMT_BODY(node), current);
        switches_.pop_back();
        targets_.pop_back();
        FlowState exit = current;
        for (const FlowState& broken : target.breaks) {
            exit = join(exit, broken);
        }
        if (!context.has_default) {
            exit = join(exit, context.entry);
        }
        state = exit;
    }

    // ── Expressions ──

    void walk_expression(tree node, FlowState& state) {
        if (node == NULL_TREE || node == error_mark_node) {
            return;
        }
        if (DECL_P(node) || TYPE_P(node) || CONSTANT_CLASS_P(node) || TREE_CODE(node) == IDENTIFIER_NODE) {
            return;
        }
        if (tree original = unfolded(node, NULL_TREE); original != node) {
            open_originals_.push_back(original);
            walk_expression(original, state);
            open_originals_.pop_back();
            return;
        }
        if (walk_kept_assignment(node, state)) {
            return;
        }
        switch (TREE_CODE(node)) {
            case COND_EXPR: {
                walk_expression(TREE_OPERAND(node, 0), state);
                FlowState one = state;
                FlowState other = state;
                walk_expression(TREE_OPERAND(node, 1), one);
                walk_expression(TREE_OPERAND(node, 2), other);
                state = join(one, other);
                return;
            }
            case TRUTH_ANDIF_EXPR:
            case TRUTH_ORIF_EXPR: {
                walk_expression(TREE_OPERAND(node, 0), state);
                FlowState taken = state;
                walk_expression(TREE_OPERAND(node, 1), taken);
                state = join(state, taken);
                return;
            }
            case STMT_EXPR:
                walk_statement(STMT_EXPR_STMT(node), state);
                return;
            case BIND_EXPR:
            case STATEMENT_LIST:
            case DECL_EXPR:
            case RETURN_EXPR:
            case IF_STMT:
            case CLEANUP_STMT:
                walk_statement(node, state);
                return;
            case TARGET_EXPR:
                walk_expression(TARGET_EXPR_INITIAL(node), state);
                return;
            case CALL_EXPR:
            case AGGR_INIT_EXPR:
                walk_call(node, state);
                return;
            case INIT_EXPR:
            case MODIFY_EXPR:
                walk_assignment(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1), TREE_CODE(node) == INIT_EXPR,
                                location_or(node, function_location()), state);
                return;
            case CONSTRUCTOR:
                walk_constructor(node, state);
                return;
            case THROW_EXPR:
                walk_expression(TREE_OPERAND(node, 0), state);
                state.is_reachable = false;
                return;
            case SIZEOF_EXPR:
            case ALIGNOF_EXPR:
            case NOEXCEPT_EXPR:
            case REQUIRES_EXPR:
            case LAMBDA_EXPR:
            case REFLECT_EXPR:
                return;
            case TREE_LIST:
                for (tree item = node; item != NULL_TREE; item = TREE_CHAIN(item)) {
                    walk_expression(TREE_VALUE(item), state);
                }
                return;
            default:
                break;
        }
        if (EXPR_P(node) || STATEMENT_CLASS_P(node)) {
            int length = TREE_OPERAND_LENGTH(node);
            for (int index = 0; index < length; ++index) {
                walk_expression(TREE_OPERAND(node, index), state);
            }
        }
    }

    // A trivial assignment of an empty class whose source has a side effect,
    // and stays in the tree as the first operand of a comma expression (THE
    // FOLDS).  The event gave its target.  Returns false for each other node.
    bool walk_kept_assignment(tree node, FlowState& state) {
        auto found = empty_assignment_of_source.find(node);
        if (found == empty_assignment_of_source.end()
            || std::find(open_assignments_.begin(), open_assignments_.end(), node) != open_assignments_.end()) {
            return false;
        }
        const EmptyAssignment& assignment = empty_assignments[found->second];
        open_assignments_.push_back(node);
        walk_assignment(assignment.target, node, false, location_or(node, assignment.location), state);
        open_assignments_.pop_back();
        return true;
    }

    // A statement whose expression the front end replaced with a void
    // constant: each trivial assignment of an empty class whose source had no
    // side effect, at a location in the statement (THE FOLDS).
    void walk_removed_assignments(tree statement, FlowState& state) {
        if (!EXPR_HAS_LOCATION(statement)) {
            return;
        }
        location_t start = expansion_point(EXPR_LOCATION(statement));
        location_t end = next_statement_ != UNKNOWN_LOCATION ? expansion_point(next_statement_) : UNKNOWN_LOCATION;
        for (std::size_t index = 0; index < empty_assignments.size(); ++index) {
            const EmptyAssignment& assignment = empty_assignments[index];
            location_t where = expansion_point(assignment.location);
            bool is_after_start = linemap_compare_locations(line_table, start, where) >= 0;
            bool is_before_end = end == UNKNOWN_LOCATION || linemap_compare_locations(line_table, where, end) > 0;
            if (is_after_start && is_before_end) {
                open_assignments_.push_back(assignment.source);
                walk_assignment(assignment.target, assignment.source, false, assignment.location, state);
                open_assignments_.pop_back();
            }
        }
    }

    static location_t expansion_point(location_t location) {
        return linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr);
    }

    static bool is_void_constant(tree node) {
        node = node != NULL_TREE ? tree_strip_any_location_wrapper(node) : node;
        return node != NULL_TREE && TREE_CODE(node) == VOID_CST;
    }

    void walk_call(tree call, FlowState& state) {
        bool is_aggregate = TREE_CODE(call) == AGGR_INIT_EXPR;
        tree callee = cp_get_callee_fndecl_nofold(call);
        if (callee != NULL_TREE && TREE_CODE(callee) != FUNCTION_DECL) {
            callee = NULL_TREE;
        }
        if (!is_aggregate) {
            walk_expression(CALL_EXPR_FN(call), state);
        }
        int count = argument_count(call);
        for (int index = 0; index < count; ++index) {
            walk_expression(argument_at(call, index), state);
        }
        if (is_move_cast(callee)) {
            return;
        }
        location_t location = location_or(call, function_location());
        tree type = function_type_of(call, callee);
        tree parameter = type != NULL_TREE ? TYPE_ARG_TYPES(type) : NULL_TREE;
        bool is_method = type != NULL_TREE && TREE_CODE(type) == METHOD_TYPE;
        bool is_constructor = callee != NULL_TREE && DECL_CONSTRUCTOR_P(callee);
        bool is_assignment = callee != NULL_TREE && DECL_ASSIGNMENT_OPERATOR_P(callee);
        for (int index = 0; index < count; ++index) {
            tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
            if (parameter != NULL_TREE) {
                parameter = TREE_CHAIN(parameter);
            }
            tree argument = argument_at(call, index);
            bool saw_rvalue = false;
            tree decl = strip_to_tracked(argument, saw_rvalue);
            if (is_method && index == 0) {
                walk_object_argument(decl, argument, type, callee, is_constructor, is_assignment, location, state);
                continue;
            }
            if (parameter_type == NULL_TREE) {
                continue;
            }
            if (decl == NULL_TREE) {
                if (TYPE_REF_P(parameter_type) && !TYPE_REF_IS_RVALUE(parameter_type)
                    && is_linear_temporary(argument)) {
                    record_discarded(location);
                }
                continue;
            }
            bool is_rvalue_reference = TYPE_REF_P(parameter_type) && TYPE_REF_IS_RVALUE(parameter_type);
            if (is_rvalue_reference || (!TYPE_REF_P(parameter_type) && CLASS_TYPE_P(parameter_type))) {
                consume(decl, state, location);
            }
        }
        if (callee != NULL_TREE && TREE_THIS_VOLATILE(callee)) {
            state.is_reachable = false;
        }
    }

    // The object argument of a call of a member function: an assignment
    // assigns over it, and a member function with the qualifier && consumes
    // it.
    void walk_object_argument(tree decl, tree argument, tree type, tree callee, bool is_constructor, bool is_assignment,
                              location_t location, FlowState& state) {
        if (is_constructor) {
            return;
        }
        if (decl == NULL_TREE) {
            if (!FUNCTION_RVALUE_QUALIFIED(type) && is_linear_temporary(argument)) {
                record_discarded(location);
            }
            return;
        }
        if (is_assignment) {
            assign_over(decl, state, location);
        } else if (FUNCTION_RVALUE_QUALIFIED(type) || (callee != NULL_TREE && DECL_DESTRUCTOR_P(callee))) {
            consume(decl, state, location);
        }
    }

    // An initialization (IS_INIT) or an assignment of TARGET from VALUE.
    void walk_assignment(tree target, tree value, bool is_init, location_t location, FlowState& state) {
        tree stripped_target = target;
        while (stripped_target != NULL_TREE && TREE_CODE(stripped_target) == INDIRECT_REF) {
            stripped_target = TREE_OPERAND(stripped_target, 0);
        }
        tree tracked_target =
            stripped_target != NULL_TREE && slot_of_.count(stripped_target) != 0 ? stripped_target : NULL_TREE;
        value = unfolded(value, stripped_target);
        open_originals_.push_back(value);
        walk_expression(value, state);
        if (tracked_target == NULL_TREE) {
            walk_expression(target, state);
        }
        bool saw_rvalue = false;
        tree source = strip_to_tracked(value, saw_rvalue);
        open_originals_.pop_back();
        bool is_linear_target =
            target != NULL_TREE && TREE_TYPE(target) != NULL_TREE && is_linear_or_reference(TREE_TYPE(target));
        if (source != NULL_TREE && source != tracked_target && (saw_rvalue || is_linear_target)) {
            consume(source, state, location);
        }
        if (tracked_target != NULL_TREE && state.is_reachable) {
            if (is_init) {
                state.set(slot_of_.at(tracked_target), Status::live);
            } else {
                assign_over(tracked_target, state, location);
            }
        }
    }

    void walk_constructor(tree node, FlowState& state) {
        vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(node);
        for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
            walk_expression((*elements)[index].value, state);
        }
        for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
            tree value = (*elements)[index].value;
            tree field = (*elements)[index].index;
            if (value == NULL_TREE) {
                continue;
            }
            if (field != NULL_TREE && TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE
                && TYPE_REF_P(TREE_TYPE(field)) && !TYPE_REF_IS_RVALUE(TREE_TYPE(field))) {
                continue;
            }
            // An address or a pointer in an element is no transfer.
            tree value_type = TREE_TYPE(value);
            if (value_type == NULL_TREE || TREE_CODE(value_type) == POINTER_TYPE) {
                continue;
            }
            bool saw_rvalue = false;
            tree source = strip_to_tracked(value, saw_rvalue);
            if (source != NULL_TREE && (saw_rvalue || is_linear_type(value_type))) {
                consume(source, state, location_or(value, location_or(node, function_location())));
            }
        }
    }

    // ── Discarded temporaries ──

    void record_discarded(location_t location) {
        add_pending(Kind::linear_discarded_temporary, location, function_text(function_) + ": temporary");
    }

    void check_discarded(tree node) {
        tree current = node;
        for (int depth = 0; current != NULL_TREE && depth < kTypeDepth; ++depth) {
            current = tree_strip_any_location_wrapper(current);
            if (TREE_CODE(current) == CLEANUP_POINT_EXPR || TREE_CODE(current) == EXPR_STMT) {
                current = TREE_OPERAND(current, 0);
                continue;
            }
            if ((TREE_CODE(current) == CONVERT_EXPR || TREE_CODE(current) == NOP_EXPR)
                && VOID_TYPE_P(TREE_TYPE(current))) {
                current = TREE_OPERAND(current, 0);
                continue;
            }
            break;
        }
        if (current == NULL_TREE) {
            return;
        }
        tree_code code = TREE_CODE(current);
        if ((code == CALL_EXPR || code == AGGR_INIT_EXPR || code == TARGET_EXPR)
            && is_dropped_temporary_type(TREE_TYPE(current))) {
            record_discarded(location_or(current, function_location()));
        }
    }

    // ── The report ──

    // The kinds that the body of a door does not give (THE DOORS).
    bool is_ended_by_door(Kind kind) const {
        return is_door_
            && (kind == Kind::linear_unconsumed || kind == Kind::linear_unconsumed_on_path
                || kind == Kind::linear_discarded_temporary);
    }

    void report() {
        if (is_unanalyzed_) {
            if (!slots_.empty()) {
                record(Kind::linear_unanalyzed, function_location(), function_text(function_));
            }
            return;
        }
        for (const Slot& entry : slots_) {
            if (!entry.has_live_exit && !entry.has_maybe_exit) {
                continue;
            }
            Kind kind = entry.has_live_exit ? Kind::linear_unconsumed : Kind::linear_unconsumed_on_path;
            record(kind, DECL_SOURCE_LOCATION(entry.decl), entity_of(entry.decl));
        }
        for (const PendingFinding& finding : pending_) {
            record(finding.kind, finding.location, finding.entity);
        }
    }

    void record(Kind kind, location_t location, const std::string& entity) {
        if (is_ended_by_door(kind)) {
            return;
        }
        Place place = place_of(location, Scope::quarantine);
        if (place.file == nullptr) {
            return;
        }
        add_finding(kind, nullptr, place, entity);
    }
};

// ── The bodies ──────────────────────────────────────────────────────────

// True when a body holds a linear object or a linear value, from a quick walk.
// Most bodies hold none, and the analysis then does not run.
tree find_linear(tree* node_pointer, int* walk_subtrees, void*) {
    tree node = *node_pointer;
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node) || DECL_P(node)) {
        *walk_subtrees = 0;
        if (VAR_P(node) && !TREE_STATIC(node) && is_linear_or_reference(TREE_TYPE(node))) {
            return node;
        }
        return NULL_TREE;
    }
    if (TREE_CODE(node) == BIND_EXPR) {
        for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
            if (VAR_P(variable) && !TREE_STATIC(variable) && is_linear_or_reference(TREE_TYPE(variable))) {
                return variable;
            }
        }
    }
    if ((EXPR_P(node) || TREE_CODE(node) == CONSTRUCTOR) && TREE_TYPE(node) != NULL_TREE
        && is_linear_type(TREE_TYPE(node))) {
        return node;
    }
    return NULL_TREE;
}

bool has_linear_parameter(tree function) {
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        tree type = TREE_TYPE(parameter);
        if (type != NULL_TREE && TYPE_REF_P(type) && !TYPE_REF_IS_RVALUE(type)) {
            continue;
        }
        if (is_linear_or_reference(type)) {
            return true;
        }
    }
    return false;
}

// True when FUNCTION is a door (THE DOORS).  A linear-door row that names
// FUNCTION gives its record at the place of FUNCTION.
bool is_door(tree function, const Place& place) {
    tree attributes = DECL_ATTRIBUTES(function);
    if (carries_annotation(attributes, linear_names.drop_door)
        || carries_annotation(attributes, linear_names.detach_door)) {
        return true;
    }
    if (state.table.linear_doors.empty()) {
        return false;
    }
    std::string name = qualified_name(function);
    for (const LinearDoorRow& row : state.table.linear_doors) {
        if (row.function == name) {
            add_finding(Kind::linear_door, nullptr, place, row.function);
            return true;
        }
    }
    return false;
}

// True when the rule reads the body of FUNCTION in this unit (THE BODIES).
bool is_read_here(tree function, Place& place) {
    place = place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine);
    if (place.file == nullptr) {
        return false;
    }
    if (is_in_instantiation(function)) {
        return place.file_class == FileClass::base || place.file_class == FileClass::quarantined;
    }
    return is_walked_file(place);
}

void on_pre_genericize_linear(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    // A clone of a constructor or a destructor gets a second event on a copy
    // of the body that cp_genericize lowered (cp/optimize.cc,
    // maybe_clone_body).  The abstract function had its own event.
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || seen_error()) {
        return;
    }
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    if (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function)) {
        return;
    }
    Place place;
    if (!is_read_here(function, place)) {
        return;
    }
    tree body = DECL_SAVED_TREE(function);
    if (!has_linear_parameter(function) && cp_walk_tree_without_duplicates(&body, find_linear, nullptr) == NULL_TREE) {
        return;
    }
    Analysis analysis(function, is_door(function, place));
    analysis.run();
}

// PLUGIN_FOLDED_INITIALIZER (THE FOLDS).
void on_folded_initializer(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_folded_initializer_data*>(gcc_data);
    if (data == nullptr || data->original == NULL_TREE || data->folded == NULL_TREE || data->original == data->folded
        || data->original == error_mark_node) {
        return;
    }
    folded_initializers[data->folded].push_back(FoldEntry{data->object, data->original});
}

// PLUGIN_EMPTY_CLASS_ASSIGNMENT (THE FOLDS).
void on_empty_class_assignment(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_empty_class_assignment_data*>(gcc_data);
    if (data == nullptr || data->source == NULL_TREE || data->target == NULL_TREE || data->source == error_mark_node) {
        return;
    }
    empty_assignment_of_source[data->source] = empty_assignments.size();
    empty_assignments.push_back(EmptyAssignment{data->target, data->source, data->location});
}

void on_linear_collection(void*, void*) {
    linear_caches.facts.clear();
    linear_caches.can_move.clear();
    folded_initializers.clear();
    empty_assignments.clear();
    empty_assignment_of_source.clear();
}

}  // namespace

// Registers the rule must-consume (plugin.cpp, the argument must-consume=on).
void register_linear(const char* plugin_name) {
    linear_names.foundation = get_identifier("foundation");
    linear_names.linear = get_identifier("linear");
    linear_names.drop_door = get_identifier("drop_door");
    linear_names.detach_door = get_identifier("detach_door");
    linear_names.scope_end_door = get_identifier("scope_end_door");
    linear_names.move = get_identifier("move");
    linear_names.forward = get_identifier("forward");
    linear_names.move_if_noexcept = get_identifier("move_if_noexcept");
    linear_names.forward_like = get_identifier("forward_like");
    register_callback(plugin_name, PLUGIN_FOLDED_INITIALIZER, on_folded_initializer, nullptr);
    register_callback(plugin_name, PLUGIN_EMPTY_CLASS_ASSIGNMENT, on_empty_class_assignment, nullptr);
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_linear, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_linear_collection, nullptr);
}

}  // namespace crucible_plugin
