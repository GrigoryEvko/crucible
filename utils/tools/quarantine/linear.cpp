// The family linear: the rule must-consume and the six rules of THE SHAPES.
// Each path of a body uses a linear value exactly one time
// (misc/12_10_2026_linear.md, P1).  The rule must-consume reads the C++
// statement tree of each body at PLUGIN_PRE_GENERICIZE, before cp_genericize
// lowers it.  It walks the paths of the body forward, with the states dead,
// live, consumed and maybe for each object, and with a fixed point for each
// loop.  It runs only with the argument must-consume=on of plugin.cpp, and the
// rules of THE SHAPES run only with the argument linear-shapes=on.
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
// THE DROPPED ELEMENTS
//     The front end folds an element of a braced list that moves an empty
//     token to an empty constant.  It then drops the initialization of an
//     empty field from the CONSTRUCTOR, so the tree keeps no sign of the move,
//     as in Record tokens{std::move(first), std::move(second)} of two tokens.
//     The event of the fold of an element gives the move.  An element whose
//     folded tree the body, with the originals of its folds, does not hold is
//     dropped.  The rule gives the move of a dropped element to one statement
//     of the body: the declaration, the expression statement or the return
//     with the last start at or before the location of the move.
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
// THE FIELDS
//     An object of a class that is linear by its parts gets one slot for each
//     linear field.  The fields of a base that is linear by its parts count as
//     fields of the object, as the parts of a Record in the cells of its bases
//     do.  The object gets no field slot when a base carries the annotation
//     linear, or when two paths of bases reach one field.  A move of a field
//     consumes its slot, and the move of the last field consumes the object.
//     The field of a move is the innermost field that is no base on the way
//     from the move to the object.  A move of the whole after a move of a
//     field, or of a field after a move of the whole, is a second consume.  A
//     reference to a field borrows it.
//
// THE ACCESSORS
//     After a move cast, the walk to the tracked object goes through a member
//     function with no qualifier && that gives an lvalue reference to a linear
//     value, such as operator* or value() of std::optional.  The rule reads
//     the return statements of the body of each such accessor at its own
//     PLUGIN_PRE_GENERICIZE.  When each one gives the same field of *this, the
//     move of the reference consumes that field only.  Else the move consumes
//     the object.  An accessor whose body the unit genericizes after the
//     caller, such as a member template with a declared return type, has no
//     such fact, and its move consumes the object.
//
// THE CAPTURES
//     A lambda initializes a capture by reference with the address of the
//     object.  The closure hides each later consume, so the object gives no
//     finding, and the rule linear-capture reports the capture.
//
// THE LIMITS
//     The rule reads one function at a time.  A consume through a reference or
//     a member call of a door is not visible to it, and a consume that depends
//     on a correlated branch, such as a test of has_value(), needs a switch or
//     a door.  A template that no unit instantiates with a linear type gives no
//     finding.
//
// THE SHAPES
//     The rules of the shapes read each class, each signature and each body
//     of the bodies that must-consume reads (THE BODIES), with the same places.
//     A class is read at PLUGIN_FINISH_TYPE, and an instance of a template
//     where a declaration or a body names it.  The rules:
//       linear-containment  a class that holds a linear part, cannot move and
//                           has no door at the end of its scope (pinned-holder),
//                           a class that holds a linear part and has a
//                           user-provided copy (copyable-holder), and a
//                           declaration whose type holds a class of the library
//                           with a linear argument (library-holder).  Such a
//                           class is no aggregate, and it is linear by its parts
//                           or it has a destructor that is not trivial
//       linear-capture      a capture by reference of a linear object
//       drop-door           an assignment over a linear object that is no
//                           tracked object of must-consume, and a destructor
//                           call or a clobber of such an object.  A door, a
//                           destructor and a transfer function read none
//       linear-forge        a union that holds a sealed type, a linear type or
//                           a type that the checked lifetime start of
//                           include/foundation/Lifetime.h refuses, and a class
//                           with the annotation linear that it admits
//       brand-escape        a fresh brand, the closure type of a lambda with no
//                           capture and no parameter in a template argument,
//                           that leaves its scope: a return type, a member, a
//                           static object, an alias outside a body, and the type
//                           of a local container of the library or function
//                           pointer.  A brand that a template argument of the
//                           function or of its class gave is bound (THE BRANDS)
//       brand-reentry       a call with an argument of a brand that the caller
//                           made, whose callee can reach the caller again in the
//                           call graph of the bodies of the unit
//     The front end gives no PLUGIN_FINISH_DECL for an alias, so the walk of
//     the declarations of declaration.cpp gives each alias at namespace scope
//     (check_linear_alias).  The rule brand-reentry reports at the end of the
//     walks of the unit (finish_linear_rules).
//
// THE BRANDS
//     GCC gives the closure of a lambda in a default template argument the
//     translation unit as its context, so no closure tells which function made
//     it.  A brand is bound at a declaration when a template argument of the
//     declaration, or of a class or a function that holds it, holds the brand.
//     The type of a value argument holds a brand too, such as the parameter of
//     the function of a stage.  Each other brand came from a mint call or a
//     decltype in the scope itself.

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
    tree linear = NULL_TREE;  // the namespace foundation::linear and its class linear
    tree lifetime = NULL_TREE;  // the namespace foundation::lifetime
    tree no_start_over_bytes = NULL_TREE;
    tree brand_type = NULL_TREE;
    tree drop_door = NULL_TREE;
    tree detach_door = NULL_TREE;
    tree scope_end_door = NULL_TREE;
    tree forward = NULL_TREE;
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

// THE FOLDS: a fold of an element of a braced list that moves a linear local
// or parameter.  The front end drops the folded element of an empty field from
// its CONSTRUCTOR, so the tree can lose the move (THE DROPPED ELEMENTS).
struct ElementFold {
    tree original = NULL_TREE;
    tree folded = NULL_TREE;
    tree moved = NULL_TREE;  // the local or the parameter that ORIGINAL moves
    location_t location = UNKNOWN_LOCATION;
};

std::vector<ElementFold> element_folds;

// THE ACCESSORS: the DECL_UID of the field of *this that the return statements
// of each accessor give, by the DECL_UID of the accessor, or kWholeObject.  No
// collection reuses a DECL_UID, so the facts stay for the unit.
constexpr int kWholeObject = -1;
std::unordered_map<int, int> accessor_fields;

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

// True when ATTRIBUTES hold an annotation of the class foundation::SPACE::NAME
// of the vocabulary of include/foundation/Linear.h, or of
// include/foundation/ByteSeal.h when SPACE is lifetime, with foundation::linear
// as the default namespace.
bool carries_annotation(tree attributes, tree name, tree space = NULL_TREE) {
    return carries_base_annotation(attributes, name, space != NULL_TREE ? space : linear_names.linear);
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

// Adds each linear field of the class TYPE to FIELDS: its own fields, and the
// fields of each base that is linear by its parts (THE FIELDS).  Returns false,
// which fails closed, when a base carries the annotation linear, when two
// paths of bases reach one field, or when the walk reaches its bound.
// Complexity: linear in the bases and the fields, and quadratic in the linear
// fields for the test of a second path.
bool collect_linear_fields(tree type, std::vector<tree>& fields, int depth) {
    if (depth > kTypeDepth) {
        return false;
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            tree base = TYPE_MAIN_VARIANT(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)));
            Linearity base_linearity = linearity_of(base);
            if (base_linearity == Linearity::direct) {
                return false;
            }
            if (base_linearity == Linearity::derived && !collect_linear_fields(base, fields, depth + 1)) {
                return false;
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) != FIELD_DECL || DECL_FIELD_IS_BASE(field) || TREE_TYPE(field) == NULL_TREE
            || TYPE_REF_P(TREE_TYPE(field)) || !is_linear_type(TREE_TYPE(field))) {
            continue;
        }
        if (std::find(fields.begin(), fields.end(), field) != fields.end()) {
            return false;
        }
        fields.push_back(field);
    }
    return true;
}

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

// True for an accessor: a member function with an implicit object parameter
// and no qualifier &&, that gives an lvalue reference to a linear value, such
// as operator* or value() of std::optional (THE ACCESSORS).  A member function
// with the qualifier && consumes its object by itself.
bool is_accessor(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_LANG_SPECIFIC(function) == nullptr
        || !DECL_IOBJ_MEMBER_FUNCTION_P(function) || DECL_CONSTRUCTOR_P(function) || DECL_DESTRUCTOR_P(function)
        || DECL_ASSIGNMENT_OPERATOR_P(function) || FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(function))) {
        return false;
    }
    tree result = TREE_TYPE(TREE_TYPE(function));
    return result != NULL_TREE && TYPE_REF_P(result) && !TYPE_REF_IS_RVALUE(result)
        && is_linear_type(TREE_TYPE(result));
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

// The bound of the slots of one object: the object and its fields (THE
// FIELDS).
constexpr std::size_t kNoParent = SIZE_MAX;

struct Slot {
    tree decl = NULL_TREE;
    // The field of a slot of one field of the object DECL, or null for the
    // slot of the whole object (THE FIELDS).
    tree field = NULL_TREE;
    std::size_t parent = kNoParent;
    std::vector<std::size_t> fields;
    // The end of the scope of the object is a door (THE LINEAR TYPES).
    bool ends_at_scope = false;
    // A closure captured the object by reference, so the rule cannot see its
    // consume, and the rule linear-capture reports the capture (THE CAPTURES).
    bool is_escaped = false;
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

// The state of the walk of a body for THE DROPPED ELEMENTS: each node of the
// body and of the originals of its folds, and each statement that can own the
// move of a dropped element.
struct PresenceWalk {
    hash_set<tree> visited;
    std::vector<tree> statements;
};

tree walk_presence(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    auto& walk = *static_cast<PresenceWalk*>(data);
    tree_code code = TREE_CODE(node);
    if ((code == DECL_EXPR || code == EXPR_STMT || code == RETURN_EXPR) && EXPR_HAS_LOCATION(node)) {
        walk.statements.push_back(node);
    }
    auto found = folded_initializers.find(node);
    if (found != folded_initializers.end()) {
        for (const FoldEntry& entry : found->second) {
            tree original = entry.original;
            cp_walk_tree(&original, walk_presence, data, &walk.visited);
        }
    }
    return NULL_TREE;
}

// True when the special member FUNCTION moves or transfers a linear value by
// its contract: a move constructor or a move assignment, or a constructor or
// an assignment of a class that carries the annotation linear (THE OBJECTS).
bool is_transfer_function(tree function) {
    if (DECL_LANG_SPECIFIC(function) != nullptr && move_fn_p(function)) {
        return true;
    }
    tree scope = DECL_CONTEXT(function);
    bool is_special = DECL_CONSTRUCTOR_P(function) || DECL_ASSIGNMENT_OPERATOR_P(function);
    return is_special && scope != NULL_TREE && TYPE_P(scope) && linearity_of(scope) == Linearity::direct;
}

class Analysis {
public:
    Analysis(tree function, bool is_door) : function_(function), is_door_(is_door) {}

    void run() {
        collect_orphans();
        scopes_.emplace_back();
        FlowState entry;
        if (!is_exempt_function()) {
            for (tree parameter = DECL_ARGUMENTS(function_); parameter != NULL_TREE;
                 parameter = DECL_CHAIN(parameter)) {
                std::size_t slot = 0;
                if (track(parameter, slot)) {
                    scopes_.back().push_back(slot);
                    set_object(entry, slot, Status::live);
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
    // THE DROPPED ELEMENTS: the original of each dropped element of this
    // body, and the statement that owns its move.
    struct Orphan {
        tree original = NULL_TREE;
        tree statement = NULL_TREE;
        location_t location = UNKNOWN_LOCATION;
    };
    std::vector<Orphan> orphans_;

    bool is_exempt_function() const { return is_transfer_function(function_); }

    // THE DROPPED ELEMENTS: each fold of an element that moves a local or a
    // parameter of this function, whose folded tree the body and the originals
    // of its folds do not hold, with the statement that owns the move.  The
    // walk takes the folds of this function from the list.  Complexity: linear
    // in the body, and in the folds times the statements.
    void collect_orphans() {
        auto is_of_function = [this](const ElementFold& fold) { return DECL_CONTEXT(fold.moved) == function_; };
        std::vector<ElementFold> folds;
        for (const ElementFold& fold : element_folds) {
            if (is_of_function(fold)) {
                folds.push_back(fold);
            }
        }
        if (folds.empty()) {
            return;
        }
        element_folds.erase(std::remove_if(element_folds.begin(), element_folds.end(), is_of_function),
                            element_folds.end());
        PresenceWalk walk;
        tree body = DECL_SAVED_TREE(function_);
        cp_walk_tree(&body, walk_presence, &walk, &walk.visited);
        for (const ElementFold& fold : folds) {
            if (walk.visited.contains(fold.folded)) {
                continue;
            }
            location_t where = expansion_point(fold.location);
            tree owner = NULL_TREE;
            location_t owner_start = UNKNOWN_LOCATION;
            for (tree statement : walk.statements) {
                location_t start = expansion_point(EXPR_LOCATION(statement));
                bool is_at_or_before = linemap_compare_locations(line_table, start, where) >= 0;
                if (is_at_or_before
                    && (owner == NULL_TREE || linemap_compare_locations(line_table, owner_start, start) >= 0)) {
                    owner = statement;
                    owner_start = start;
                }
            }
            if (owner != NULL_TREE) {
                orphans_.push_back(Orphan{fold.original, owner, fold.location});
            }
        }
    }

    // The moves of the dropped elements that the statement STATEMENT owns.
    void resolve_orphans(tree statement, FlowState& state) {
        for (const Orphan& orphan : orphans_) {
            if (orphan.statement != statement) {
                continue;
            }
            bool saw_rvalue = false;
            tree field = NULL_TREE;
            tree source = strip_to_tracked(orphan.original, saw_rvalue, &field);
            if (source != NULL_TREE && saw_rvalue) {
                consume(source, field, state, orphan.location);
            }
        }
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
        add_field_slots(slot, type);
        return true;
    }

    // THE FIELDS: an object of a class that is linear by its parts gets one
    // slot for each linear field, also of each base that is linear by its
    // parts.  A move of a field consumes its slot, and the move of the last
    // linear field consumes the object.  A class that carries the annotation
    // hides its fields.  Complexity: linear in the bases and the fields.
    void add_field_slots(std::size_t slot, tree type) {
        type = TYPE_MAIN_VARIANT(type);
        if (!CLASS_TYPE_P(type) || linearity_of(type) != Linearity::derived) {
            return;
        }
        std::vector<tree> linear_fields;
        if (!collect_linear_fields(type, linear_fields, 0)) {
            return;
        }
        std::vector<std::size_t> fields;
        for (tree field : linear_fields) {
            Slot part;
            part.decl = slots_[slot].decl;
            part.field = field;
            part.parent = slot;
            fields.push_back(slots_.size());
            slots_.push_back(part);
        }
        slots_[slot].fields = std::move(fields);
    }

    // The slot of the field FIELD of the tracked object DECL, or the slot of
    // DECL when FIELD is null.  Returns kNoParent when the object has no slot
    // for the field.
    std::size_t slot_for(tree decl, tree field) const {
        std::size_t slot = slot_of_.at(decl);
        if (field == NULL_TREE) {
            return slot;
        }
        for (std::size_t part : slots_[slot].fields) {
            if (slots_[part].field == field) {
                return part;
            }
        }
        return kNoParent;
    }

    // Sets the object of SLOT and each of its fields to STATUS.
    void set_object(FlowState& state, std::size_t slot, Status status) {
        state.set(slot, status);
        for (std::size_t part : slots_[slot].fields) {
            state.set(part, status);
        }
    }

    void mark_escaped(tree decl) {
        std::size_t slot = slot_of_.at(decl);
        slots_[slot].is_escaped = true;
        for (std::size_t part : slots_[slot].fields) {
            slots_[part].is_escaped = true;
        }
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

    // The slot field of the tracked object DECL whose DECL_UID is FIELD_UID, or
    // null when the object has no slot for it (THE ACCESSORS).
    tree field_by_uid(tree decl, int field_uid) const {
        if (field_uid == kWholeObject) {
            return NULL_TREE;
        }
        for (std::size_t part : slots_[slot_of_.at(decl)].fields) {
            if (static_cast<int>(DECL_UID(slots_[part].field)) == field_uid) {
                return slots_[part].field;
            }
        }
        return NULL_TREE;
    }

    // The tracked object that NODE names through the casts of THE CONSUMES, or
    // null.  SAW_RVALUE is true when a cast to an rvalue reference or std::move
    // is on the way.  With FIELD, the walk also goes through a member access
    // to a linear field of the object (THE FIELDS), and FIELD gets that field,
    // or null for the whole object.  After a move cast, it goes through an
    // accessor: a member function that gives a reference to a linear value of
    // its object, such as operator* or value() of std::optional.  The move of
    // that reference consumes the field that the accessor gives, or the object
    // (THE ACCESSORS).
    tree strip_to_tracked(tree node, bool& saw_rvalue, tree* field = nullptr) const {
        if (field != nullptr) {
            *field = NULL_TREE;
        }
        // The innermost field that is no base on the way, and whether a base
        // of the object comes with no such field after it.
        tree leaf = NULL_TREE;
        bool is_base_only = false;
        // The DECL_UID of the field that the last accessor on the way gives.
        int accessor_field = kWholeObject;
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
                case COMPONENT_REF: {
                    tree member = TREE_OPERAND(node, 1);
                    if (field == nullptr || TREE_CODE(member) != FIELD_DECL) {
                        return NULL_TREE;
                    }
                    if (!DECL_FIELD_IS_BASE(member)) {
                        leaf = member;
                        is_base_only = false;
                    } else if (leaf == NULL_TREE) {
                        is_base_only = true;
                    }
                    accessor_field = kWholeObject;
                    node = TREE_OPERAND(node, 0);
                    continue;
                }
                case CALL_EXPR: {
                    tree callee = cp_get_callee_fndecl_nofold(node);
                    if (is_std_move_cast(callee) && call_expr_nargs(node) >= 1) {
                        saw_rvalue = saw_rvalue || DECL_NAME(callee) != linear_names.forward;
                        node = CALL_EXPR_ARG(node, 0);
                        continue;
                    }
                    if (saw_rvalue && is_accessor(callee) && call_expr_nargs(node) >= 1) {
                        auto summary = accessor_fields.find(static_cast<int>(DECL_UID(callee)));
                        accessor_field = summary != accessor_fields.end() ? summary->second : kWholeObject;
                        leaf = NULL_TREE;
                        is_base_only = false;
                        node = CALL_EXPR_ARG(node, 0);
                        continue;
                    }
                    return NULL_TREE;
                }
                case VAR_DECL:
                case PARM_DECL: {
                    tree found = node;
                    if (slot_of_.count(node) == 0) {
                        auto referent = referent_of_.find(node);
                        found = referent != referent_of_.end() ? referent->second : NULL_TREE;
                    }
                    if (found == NULL_TREE || field == nullptr) {
                        return found;
                    }
                    // A base of the object with no field after it is no
                    // field slot, and neither is a field with no slot.
                    if (is_base_only || (leaf != NULL_TREE && slot_for(found, leaf) == kNoParent)) {
                        return NULL_TREE;
                    }
                    *field = leaf != NULL_TREE ? leaf : field_by_uid(found, accessor_field);
                    return found;
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

    static bool is_used(Status status) { return status == Status::consumed || status == Status::maybe; }

    // A consume of the object DECL, or of its field FIELD (THE FIELDS).  A
    // consume of the whole after a consume of a field, or of a field after a
    // consume of the whole, is a second consume.
    void consume(tree decl, tree field, FlowState& state, location_t location) {
        if (!state.is_reachable) {
            return;
        }
        std::size_t slot = slot_for(decl, field);
        if (slot == kNoParent) {
            return;
        }
        std::size_t whole = slot_of_.at(decl);
        Status status = state.get(slot);
        bool was_used = is_used(status);
        if (field == NULL_TREE) {
            for (std::size_t part : slots_[whole].fields) {
                was_used = was_used || is_used(state.get(part));
            }
        } else {
            was_used = was_used || is_used(state.get(whole));
        }
        if (was_used && !slots_[whole].was_double_reported && !slots_[whole].is_escaped) {
            slots_[whole].was_double_reported = true;
            add_pending(Kind::linear_double_consume, location, entity_of(decl));
        }
        if (status == Status::dead) {
            return;
        }
        if (field == NULL_TREE) {
            set_object(state, whole, Status::consumed);
            return;
        }
        state.set(slot, Status::consumed);
        bool is_each_field_consumed = true;
        for (std::size_t part : slots_[whole].fields) {
            is_each_field_consumed = is_each_field_consumed && state.get(part) == Status::consumed;
        }
        if (is_each_field_consumed) {
            state.set(whole, Status::consumed);
        }
    }

    void assign_over(tree decl, FlowState& state, location_t location) {
        if (!state.is_reachable) {
            return;
        }
        std::size_t slot = slot_of_.at(decl);
        Status status = state.get(slot);
        if ((status == Status::live || status == Status::maybe) && !slots_[slot].was_assign_reported
            && !slots_[slot].is_escaped) {
            slots_[slot].was_assign_reported = true;
            add_pending(Kind::linear_assign_over_live, location, entity_of(decl));
        }
        set_object(state, slot, Status::live);
    }

    // True when a live object drops a value at an exit on each path: it has no
    // field slot, or a field that each path holds (THE FIELDS).  An object
    // whose fields are consumed on some paths only drops a value on a path.
    bool is_live_at_exit(const FlowState& state, std::size_t slot) const {
        if (slots_[slot].fields.empty()) {
            return true;
        }
        for (std::size_t part : slots_[slot].fields) {
            if (state.get(part) == Status::live) {
                return true;
            }
        }
        return false;
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
                if (slots_[slot].ends_at_scope || slots_[slot].is_escaped) {
                    // The end of the scope is the door of the object, or a
                    // capture by reference hides the consume (THE CAPTURES).
                } else if (status == Status::live && is_live_at_exit(state, slot)) {
                    slots_[slot].has_live_exit = true;
                } else if (status == Status::live || status == Status::maybe) {
                    slots_[slot].has_maybe_exit = true;
                }
                if (kill) {
                    set_object(state, slot, Status::dead);
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
                resolve_orphans(node, state);
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
                resolve_orphans(node, state);
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
                } else {
                    check_discarded(EXPR_STMT_EXPR(node));
                    walk_expression(EXPR_STMT_EXPR(node), state);
                }
                resolve_orphans(node, state);
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
            tree field = NULL_TREE;
            tree source = strip_to_tracked(initial, saw_rvalue, &field);
            open_originals_.pop_back();
            // An lvalue reference local borrows its object, and a move through
            // it consumes the object.  The base of a structured binding by
            // reference gives the object to its bindings.
            tree decl_type = TREE_TYPE(decl);
            bool is_borrow = decl_type != NULL_TREE && TYPE_REF_P(decl_type) && !TYPE_REF_IS_RVALUE(decl_type)
                          && !(DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl));
            // A reference to a field, also the base of a structured binding,
            // borrows the field and consumes nothing (THE FIELDS).
            bool is_field_borrow =
                field != NULL_TREE && decl_type != NULL_TREE && TYPE_REF_P(decl_type) && !TYPE_REF_IS_RVALUE(decl_type);
            if (source != NULL_TREE && source != decl && is_borrow && field == NULL_TREE) {
                referent_of_[decl] = source;
            } else if (source != NULL_TREE && source != decl && !is_borrow && !is_field_borrow
                       && (saw_rvalue || is_linear_or_reference(decl_type))) {
                consume(source, field, state, location_or(initial, DECL_SOURCE_LOCATION(decl)));
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
            set_object(state, slot_of_.at(decl), Status::live);
        }
        // The bindings of a structured binding, which have no DECL_EXPR of
        // their own.
        if (DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl) && state.is_reachable) {
            for (const auto& [tracked, tracked_slot] : slot_of_) {
                if (VAR_P(tracked) && DECL_DECOMPOSITION_P(tracked) && !DECL_DECOMP_IS_BASE(tracked)
                    && DECL_DECOMP_BASE(tracked) == decl) {
                    set_object(state, tracked_slot, Status::live);
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
            case EXPR_STMT:
                // An expression statement inside a CLEANUP_POINT_EXPR.
                walk_expression(EXPR_STMT_EXPR(node), state);
                resolve_orphans(node, state);
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
        if (is_std_move_cast(callee)) {
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
            tree field = NULL_TREE;
            tree decl = strip_to_tracked(argument, saw_rvalue, &field);
            if (is_method && index == 0) {
                walk_object_argument(decl, field, argument, type, callee, is_constructor, is_assignment, location,
                                     state);
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
                consume(decl, field, state, location);
            }
        }
        if (callee != NULL_TREE && TREE_THIS_VOLATILE(callee)) {
            state.is_reachable = false;
        }
    }

    // The object argument of a call of a member function: an assignment
    // assigns over it, and a member function with the qualifier && consumes
    // it.
    void walk_object_argument(tree decl, tree field, tree argument, tree type, tree callee, bool is_constructor,
                              bool is_assignment, location_t location, FlowState& state) {
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
            // The rule drop-door reads an assignment to a field (THE SHAPES).
            if (field == NULL_TREE) {
                assign_over(decl, state, location);
            }
        } else if (FUNCTION_RVALUE_QUALIFIED(type) || (callee != NULL_TREE && DECL_DESTRUCTOR_P(callee))) {
            consume(decl, field, state, location);
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
        tree field = NULL_TREE;
        tree source = strip_to_tracked(value, saw_rvalue, &field);
        open_originals_.pop_back();
        bool is_linear_target =
            target != NULL_TREE && TREE_TYPE(target) != NULL_TREE && is_linear_or_reference(TREE_TYPE(target));
        // THE CAPTURES: a lambda initializes a capture by reference with the
        // address of the object, and the closure hides each later consume.
        bool is_reference_capture = target != NULL_TREE && TREE_CODE(target) == COMPONENT_REF
                                 && LAMBDA_TYPE_P(TREE_TYPE(TREE_OPERAND(target, 0))) && TREE_TYPE(target) != NULL_TREE
                                 && TYPE_REF_P(TREE_TYPE(target));
        if (source != NULL_TREE && is_reference_capture) {
            mark_escaped(source);
        } else if (source != NULL_TREE && source != tracked_target && (saw_rvalue || is_linear_target)) {
            consume(source, field, state, location);
        }
        if (tracked_target != NULL_TREE && state.is_reachable) {
            if (is_init) {
                set_object(state, slot_of_.at(tracked_target), Status::live);
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
                // THE CAPTURES: a closure that captures a tracked object by
                // reference hides each later consume of it.
                bool saw_reference = false;
                tree captured = LAMBDA_TYPE_P(TREE_TYPE(node)) ? strip_to_tracked(value, saw_reference) : NULL_TREE;
                if (captured != NULL_TREE) {
                    mark_escaped(captured);
                }
                continue;
            }
            // An address or a pointer in an element is no transfer.
            tree value_type = TREE_TYPE(value);
            if (value_type == NULL_TREE || TREE_CODE(value_type) == POINTER_TYPE) {
                continue;
            }
            bool saw_rvalue = false;
            tree source_field = NULL_TREE;
            tree source = strip_to_tracked(value, saw_rvalue, &source_field);
            if (source != NULL_TREE && (saw_rvalue || is_linear_type(value_type))) {
                consume(source, source_field, state, location_or(value, location_or(node, function_location())));
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

// ── The shapes (THE SHAPES) ─────────────────────────────────────────────

// The facts of the rules of the shapes, keyed by the main variant of a type.
// Each collection clears them, as it clears linear_caches.
struct ShapeCaches {
    std::unordered_map<tree, bool> refuses_lifetime;
    std::unordered_map<tree, tree> library_holder;
    std::unordered_map<tree, std::vector<tree>> brands;
};

ShapeCaches shape_caches;

// The classes that the class rules read in this unit, by TYPE_UID, which no
// collection reuses.
std::unordered_set<int> checked_classes;

// The call edges of the bodies of this unit, and each call with a branded
// argument, by DECL_UID (the rule brand-reentry).
struct BrandedCall {
    int caller = 0;
    int callee = 0;
    location_t location = UNKNOWN_LOCATION;
    std::string entity;
};

struct CallGraph {
    std::unordered_map<int, std::vector<int>> callees;
    std::vector<BrandedCall> branded_calls;
    bool was_reported = false;
};

CallGraph call_graph;

// Records a finding of a rule of the family at LOCATION.
void record_shape(Kind kind, location_t location, const std::string& entity) {
    Place place = place_of(location, Scope::quarantine);
    if (place.file != nullptr) {
        add_finding(kind, nullptr, place, entity);
    }
}

// True when a finding about ENTITY, a declaration or a class, counts in this
// unit: in a quarantined file, in a base file only in its sentinel unit, and in
// each unit that makes an instance of a base template (THE BODIES).
bool is_reported_for(tree entity) {
    tree decl = TYPE_P(entity) ? TYPE_MAIN_DECL(entity) : entity;
    if (decl == NULL_TREE) {
        return false;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(decl), Scope::quarantine);
    if (place.file == nullptr) {
        return false;
    }
    if (is_in_instantiation(entity)) {
        return place.file_class == FileClass::base || place.file_class == FileClass::quarantined;
    }
    return is_walked_file(place);
}

// True when TYPE carries the annotation no_start_over_bytes of
// include/foundation/ByteSeal.h, or holds a part that carries it, or holds a
// reference member: the checked lifetime start of include/foundation/Lifetime.h
// refuses it.  Complexity: the walk visits each class one time.
bool refuses_lifetime(tree type, int depth = 0) {
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || depth > kTypeDepth) {
        return false;
    }
    auto found = shape_caches.refuses_lifetime.find(type);
    if (found != shape_caches.refuses_lifetime.end()) {
        return found->second;
    }
    shape_caches.refuses_lifetime.emplace(type, false);
    bool result = carries_annotation(TYPE_ATTRIBUTES(type), linear_names.no_start_over_bytes, linear_names.lifetime);
    if (tree binfo = TYPE_BINFO(type); binfo != NULL_TREE && !result) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo) && !result; ++index) {
            result = refuses_lifetime(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE && !result; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE) {
            result = TYPE_REF_P(TREE_TYPE(field)) || refuses_lifetime(TREE_TYPE(field), depth + 1);
        }
    }
    shape_caches.refuses_lifetime[type] = result;
    return result;
}

// A sealed type: a linear type, or a type that the checked lifetime start
// refuses.  No route of bytes can build one.
bool is_sealed_type(tree type) { return is_linear_type(type) || refuses_lifetime(type); }

// True when TYPE is a class of namespace std.
bool is_library_class(tree type) {
    tree decl = TYPE_MAIN_DECL(type);
    return decl != NULL_TREE && decl_in_std_namespace_p(decl);
}

// Calls VISIT for each type argument of the class TYPE, at each level and
// inside each argument pack.
template <typename Visit>
void each_type_argument(tree arguments, Visit visit, int depth = 0) {
    if (arguments == NULL_TREE || depth > kTypeDepth) {
        return;
    }
    if (TREE_CODE(arguments) != TREE_VEC) {
        if (ARGUMENT_PACK_P(arguments)) {
            each_type_argument(ARGUMENT_PACK_ARGS(arguments), visit, depth + 1);
        } else {
            visit(arguments);
        }
        return;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
        each_type_argument(TREE_VEC_ELT(arguments, index), visit, depth + 1);
    }
}

// The class of the library that TYPE holds by value with a linear type
// argument, or null.  Such a class is no aggregate, and it is linear by its
// parts or it has a destructor that is not trivial: its operations copy or
// drop a value, and they carry no door.  A view, such as std::span, has a
// trivial destructor and holds no value.  A pointer and a reference hold no
// value.  Complexity: the walk visits each class one time.
tree library_holder_of(tree type, int depth = 0) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(element_type_of(type)) : NULL_TREE;
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || LAMBDA_TYPE_P(type) || depth > kTypeDepth
        || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE || uses_template_parms(type)) {
        return NULL_TREE;
    }
    auto found = shape_caches.library_holder.find(type);
    if (found != shape_caches.library_holder.end()) {
        return found->second;
    }
    shape_caches.library_holder.emplace(type, NULL_TREE);
    tree result = NULL_TREE;
    bool is_owner = is_library_class(type) && COMPLETE_TYPE_P(type) && !CP_AGGREGATE_TYPE_P(type)
                 && (is_linear_type(type) || TYPE_HAS_NONTRIVIAL_DESTRUCTOR(type));
    each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
        if (result != NULL_TREE || !TYPE_P(argument)) {
            return;
        }
        if (is_owner && is_linear_type(argument)) {
            result = type;
        } else {
            result = library_holder_of(argument, depth + 1);
        }
    });
    shape_caches.library_holder[type] = result;
    return result;
}

// The macro of include/foundation/Brand.h that writes the fresh brand.
constexpr const char* kFreshBrandMacro = "CRUCIBLE_FRESH_BRAND";

// True when TYPE has the shape of a brand: the closure type of a lambda with
// no capture, no template parameter and no parameter.
bool has_brand_shape(tree type) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!LAMBDA_TYPE_P(type)) {
        return false;
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL) {
            return false;
        }
    }
    tree call = lambda_function(type);
    return call != NULL_TREE && TREE_CODE(call) == FUNCTION_DECL && !generic_lambda_fn_p(call)
        && FUNCTION_FIRST_USER_PARMTYPE(call) == void_list_node;
}

// True when the expansion of the macro CRUCIBLE_FRESH_BRAND wrote the lambda
// of the closure TYPE.
bool is_written_by_fresh_brand(tree type) {
    tree decl = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(type));
    location_t location = decl != NULL_TREE ? DECL_SOURCE_LOCATION(decl) : UNKNOWN_LOCATION;
    for (int depth = 0; depth < kMacroDepth && linemap_location_from_macro_expansion_p(line_table, location); ++depth) {
        const line_map* map = linemap_lookup(line_table, location);
        if (map == nullptr || !linemap_macro_expansion_map_p(map)) {
            return false;
        }
        const char* macro = linemap_map_get_macro_name(linemap_check_macro(map));
        if (macro != nullptr && std::strcmp(macro, kFreshBrandMacro) == 0) {
            return true;
        }
        location = linemap_unwind_toward_expansion(line_table, location, &map);
    }
    return false;
}

// True when TYPE is a fresh brand: a closure with the shape of a brand that
// CRUCIBLE_FRESH_BRAND of include/foundation/Brand.h writes.  A class that
// names a closure as its member brand_type gives a fresh brand too
// (collect_brands).  A closure with the shape of a brand that no class names
// as its brand, such as a label of a session, is no brand.
bool is_brand_closure(tree type) { return has_brand_shape(type) && is_written_by_fresh_brand(type); }

// The closure that the class TYPE or one of its bases names as its member
// brand_type, or null.
tree named_brand_of(tree type, int depth = 0) {
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || depth > kTypeDepth) {
        return NULL_TREE;
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == TYPE_DECL && DECL_NAME(member) == linear_names.brand_type
            && has_brand_shape(TREE_TYPE(member))) {
            return TYPE_MAIN_VARIANT(TREE_TYPE(member));
        }
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (tree brand = named_brand_of(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1)) {
                return brand;
            }
        }
    }
    return NULL_TREE;
}

// Adds each fresh brand that TYPE holds to OUT: a brand closure, a brand in a
// template argument or in the type of a value argument such as a function
// pointer, and a brand in a capture or a field of a closure or of a local
// class.  Complexity: the walk visits each class one time.
void collect_brands(tree type, std::vector<tree>& out, int depth = 0) {
    if (type == NULL_TREE || !TYPE_P(type) || depth > kTypeDepth) {
        return;
    }
    switch (TREE_CODE(type)) {
        case REFERENCE_TYPE:
        case POINTER_TYPE:
        case ARRAY_TYPE:
            collect_brands(TREE_TYPE(type), out, depth + 1);
            return;
        case OFFSET_TYPE:
            collect_brands(TREE_TYPE(type), out, depth + 1);
            collect_brands(TYPE_OFFSET_BASETYPE(type), out, depth + 1);
            return;
        case FUNCTION_TYPE:
        case METHOD_TYPE: {
            collect_brands(TREE_TYPE(type), out, depth + 1);
            tree parameter = TYPE_ARG_TYPES(type);
            if (TREE_CODE(type) == METHOD_TYPE && parameter != NULL_TREE) {
                parameter = TREE_CHAIN(parameter);
            }
            for (; parameter != NULL_TREE && parameter != void_list_node; parameter = TREE_CHAIN(parameter)) {
                collect_brands(TREE_VALUE(parameter), out, depth + 1);
            }
            return;
        }
        default:
            break;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return;
    }
    if (is_brand_closure(type)) {
        out.push_back(type);
        return;
    }
    auto found = shape_caches.brands.find(type);
    if (found == shape_caches.brands.end()) {
        shape_caches.brands.emplace(type, std::vector<tree>{});
        std::vector<tree> held;
        if (CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE) {
            each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
                if (TYPE_P(argument)) {
                    collect_brands(argument, held, depth + 1);
                } else if (TREE_TYPE(argument) != NULL_TREE) {
                    collect_brands(TREE_TYPE(argument), held, depth + 1);
                }
            });
        }
        if (tree named = named_brand_of(type)) {
            held.push_back(named);
        }
        tree decl = TYPE_MAIN_DECL(type);
        if (LAMBDA_TYPE_P(type) || (decl != NULL_TREE && decl_function_context(decl) != NULL_TREE)) {
            for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
                if (TREE_CODE(field) == FIELD_DECL) {
                    collect_brands(TREE_TYPE(field), held, depth + 1);
                }
            }
        }
        found = shape_caches.brands.find(type);
        found->second = std::move(held);
    }
    out.insert(out.end(), found->second.begin(), found->second.end());
}

// True when BRAND is held by a template argument of SCOPE or of a class or a
// function that holds SCOPE: the brand came in from outside, so SCOPE did not
// make it (THE BRANDS).
bool is_bound_brand(tree brand, tree scope) {
    for (int depth = 0; scope != NULL_TREE && depth < kTypeDepth; ++depth) {
        tree info = NULL_TREE;
        tree next = NULL_TREE;
        if (TYPE_P(scope)) {
            scope = TYPE_MAIN_VARIANT(scope);
            info = CLASS_TYPE_P(scope) ? CLASSTYPE_TEMPLATE_INFO(scope) : NULL_TREE;
            next = CP_TYPE_CONTEXT(scope);
        } else if (TREE_CODE(scope) == FUNCTION_DECL) {
            info = DECL_LANG_SPECIFIC(scope) != nullptr ? DECL_TEMPLATE_INFO(scope) : NULL_TREE;
            next = CP_DECL_CONTEXT(scope);
        } else if (TREE_CODE(scope) == NAMESPACE_DECL) {
            return false;
        } else if (DECL_P(scope)) {
            next = CP_DECL_CONTEXT(scope);
        } else {
            return false;
        }
        if (info != NULL_TREE && TI_ARGS(info) != NULL_TREE) {
            bool is_held = false;
            each_type_argument(TI_ARGS(info), [&](tree argument) {
                std::vector<tree> held;
                if (TYPE_P(argument) && TYPE_MAIN_VARIANT(argument) == brand) {
                    held.push_back(brand);
                } else if (TYPE_P(argument)) {
                    collect_brands(argument, held);
                } else if (TREE_TYPE(argument) != NULL_TREE && has_brand_shape(TREE_TYPE(argument))) {
                    held.push_back(TYPE_MAIN_VARIANT(TREE_TYPE(argument)));
                } else if (TREE_TYPE(argument) != NULL_TREE) {
                    collect_brands(TREE_TYPE(argument), held);
                }
                is_held = is_held || std::find(held.begin(), held.end(), brand) != held.end();
            });
            if (is_held) {
                return true;
            }
        }
        scope = next;
    }
    return false;
}

// The first brand of TYPE that SCOPE did not get from outside, or null.
tree unbound_brand(tree type, tree scope) {
    std::vector<tree> held;
    collect_brands(type, held);
    for (tree brand : held) {
        if (!is_bound_brand(brand, scope)) {
            return brand;
        }
    }
    return NULL_TREE;
}

// The name of a brand in an entity: the place of its lambda, because GCC
// prints each brand as <lambda()>.
std::string brand_text(tree brand) {
    tree decl = TYPE_MAIN_DECL(brand);
    Place place = decl != NULL_TREE ? place_of(DECL_SOURCE_LOCATION(decl), Scope::tree) : Place{};
    if (place.file == nullptr) {
        return "a brand of a library";
    }
    return "the brand of " + place.file->relative + ":" + std::to_string(place.line);
}

// True when the end of the scope of a value of TYPE is a door: the class
// carries scope_end_door, or its destructor is a door.
bool has_scope_door(tree type);

// True when the class TYPE has a user-provided copy constructor or copy
// assignment that is not deleted.
bool has_user_copy(tree type) {
    // A copy whose constraints an instance does not satisfy is no copy of it.
    auto is_user_copy = [](tree function) {
        return TREE_CODE(function) == FUNCTION_DECL && copy_fn_p(function) > 0 && !DECL_DELETED_FN(function)
            && user_provided_p(function) && constraints_satisfied_p(function);
    };
    for (ovl_iterator constructor(CLASSTYPE_CONSTRUCTORS(type)); constructor; ++constructor) {
        if (is_user_copy(*constructor)) {
            return true;
        }
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == FUNCTION_DECL && DECL_ASSIGNMENT_OPERATOR_P(member) && is_user_copy(member)) {
            return true;
        }
    }
    return false;
}

// True when TYPE holds a linear base or a linear field by value.
bool holds_linear_part(tree type) {
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (is_linear_type(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)))) {
                return true;
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
            && !TYPE_REF_P(TREE_TYPE(field)) && is_linear_type(TREE_TYPE(field))) {
            return true;
        }
    }
    return false;
}

void check_class(tree type, int depth = 0);

// The checks of an object with static or thread storage duration: a brand in
// it escapes each run of its mint site, and a class of the library with a
// linear argument drops its values.
void check_static_object(tree decl, const std::string& owner) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || uses_template_parms(type)) {
        return;
    }
    std::string entity = owner + ": " + simple_name(decl);
    std::vector<tree> held;
    collect_brands(type, held);
    if (!held.empty()) {
        record_shape(Kind::brand_escaped_static, DECL_SOURCE_LOCATION(decl), entity + ", " + brand_text(held.front()));
    }
    if (tree holder = library_holder_of(type)) {
        record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(decl),
                     entity + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
    }
    check_class(type);
}

// An alias outside a body: a brand in its type escapes each run of its mint
// site, unless a template argument of its class gave the brand.
void check_alias(tree decl, tree scope, const std::string& owner) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || uses_template_parms(type)) {
        return;
    }
    if (tree brand = unbound_brand(type, scope)) {
        record_shape(Kind::brand_escaped_alias, DECL_SOURCE_LOCATION(decl),
                     owner + ": " + simple_name(decl) + ", " + brand_text(brand));
    }
}

// The class rules: linear-containment, linear-forge and the members of
// brand-escape, for one complete class that is no pattern (THE SHAPES).  The
// walk goes on into the class of each base and each field.  Complexity: each
// class one time in each unit.
void check_class(tree type, int depth) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(element_type_of(type)) : NULL_TREE;
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || LAMBDA_TYPE_P(type) || depth > kTypeDepth
        || uses_template_parms(type) || !checked_classes.insert(TYPE_UID(type)).second) {
        return;
    }
    tree decl = TYPE_MAIN_DECL(type);
    if (decl == NULL_TREE || in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            check_class(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE) {
            check_class(TREE_TYPE(field), depth + 1);
        }
    }
    if (!is_reported_for(type)) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    std::string name = qualified_name(decl);
    TypeFacts facts = facts_of(type);
    // A union ends no member at its end, and its owner chooses the active
    // member: the rule linear-forge reads it.
    if (TREE_CODE(type) != UNION_TYPE && holds_linear_part(type)) {
        if (facts.linearity == Linearity::none && !has_scope_door(type)) {
            record_shape(Kind::linear_pinned_holder, location, name);
        }
        if (has_user_copy(type)) {
            record_shape(Kind::linear_copyable_holder, location, name);
        }
    }
    if (facts.linearity == Linearity::direct && !refuses_lifetime(type)) {
        record_shape(Kind::linear_unsealed_lifetime, location, name);
    }
    bool is_union = TREE_CODE(type) == UNION_TYPE;
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree member_type = TREE_TYPE(member);
        if (member_type == NULL_TREE || member_type == error_mark_node) {
            continue;
        }
        if (TREE_CODE(member) == FIELD_DECL && !DECL_FIELD_IS_BASE(member)) {
            std::string entity = name + ": " + simple_name(member);
            if (is_union && is_sealed_type(member_type)) {
                record_shape(Kind::linear_sealed_union, DECL_SOURCE_LOCATION(member), entity);
            }
            if (tree holder = library_holder_of(member_type)) {
                record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(member),
                             entity + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
            }
            if (tree brand = unbound_brand(member_type, type)) {
                record_shape(Kind::brand_escaped_member, DECL_SOURCE_LOCATION(member),
                             entity + ", " + brand_text(brand));
            }
        } else if (VAR_P(member)) {
            check_static_object(member, name);
        } else if (TREE_CODE(member) == TYPE_DECL && !DECL_ARTIFICIAL(member) && !DECL_IMPLICIT_TYPEDEF_P(member)
                   && !DECL_SELF_REFERENCE_P(member)) {
            check_alias(member, type, name);
        }
    }
}

// The checks of the signature of a function whose body the unit reads: a
// return value and a parameter of a class of the library with a linear
// argument, and a fresh brand that the function made and returns.
void check_signature(tree function) {
    std::string name = function_text(function);
    location_t location = DECL_SOURCE_LOCATION(function);
    tree result = TREE_TYPE(TREE_TYPE(function));
    if (!DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function) && result != NULL_TREE
        && !uses_template_parms(result)) {
        if (tree holder = library_holder_of(result)) {
            record_shape(Kind::linear_library_holder, location,
                         name + ": the return value, " + qualified_name(TYPE_MAIN_DECL(holder)));
        }
        if (tree brand = unbound_brand(result, function)) {
            record_shape(Kind::brand_escaped_return, location, name + ": " + brand_text(brand));
        }
        check_class(result);
    }
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        tree type = TREE_TYPE(parameter);
        if (DECL_ARTIFICIAL(parameter) || type == NULL_TREE || uses_template_parms(type)) {
            continue;
        }
        check_class(type);
        if (tree holder = library_holder_of(type)) {
            record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(parameter),
                         name + ": " + simple_name(parameter) + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
        }
    }
}

// The walk of one body for the rules of the shapes.  It also tells whether
// the body holds a linear object or a linear value, so that must-consume runs
// only on such a body.
struct ShapeWalk {
    tree function = NULL_TREE;
    std::string name;
    // A door, a transfer and a destructor end or move the linear values of
    // their frame by their contract, so drop-door reads none of them.
    bool is_exempt_from_drops = false;
    bool has_linear = false;
    // The location of the last node with a location, for a node that the
    // front end builds with none, such as the clobber of a trivial destructor.
    location_t last_location = UNKNOWN_LOCATION;
    // The locals of the body, and each local that a call of the same type
    // initializes (check_local).
    std::vector<tree> locals;
    std::unordered_set<tree> from_call;
    hash_set<tree> visited;

    location_t location_of(tree node) const {
        if (node != NULL_TREE && EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
            return EXPR_LOCATION(node);
        }
        return last_location != UNKNOWN_LOCATION ? last_location : DECL_SOURCE_LOCATION(function);
    }
};

// The call or the aggregate initialization that INITIAL is, through a
// temporary and its casts, or null.
tree initializing_call(tree initial) {
    for (int depth = 0; initial != NULL_TREE && depth < kTypeDepth; ++depth) {
        initial = tree_strip_any_location_wrapper(initial);
        if (TREE_CODE(initial) == TARGET_EXPR) {
            initial = TARGET_EXPR_INITIAL(initial);
        } else if (TREE_CODE(initial) == NOP_EXPR || TREE_CODE(initial) == CLEANUP_POINT_EXPR) {
            initial = TREE_OPERAND(initial, 0);
        } else {
            break;
        }
    }
    if (initial == NULL_TREE || (TREE_CODE(initial) != CALL_EXPR && TREE_CODE(initial) != AGGR_INIT_EXPR)) {
        return NULL_TREE;
    }
    tree callee = cp_get_callee_fndecl_nofold(initial);
    if (callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL && DECL_CONSTRUCTOR_P(callee)) {
        return NULL_TREE;
    }
    return initial;
}

// True when INITIAL is a call that gives a value of TYPE: the local then takes
// the type of the callee (check_local).
bool is_call_of_type(tree initial, tree type) {
    tree call = initializing_call(initial);
    if (call == NULL_TREE || type == NULL_TREE) {
        return false;
    }
    // An AGGR_INIT_EXPR has the type void, and its callee gives the type.
    tree callee = cp_get_callee_fndecl_nofold(call);
    tree result =
        callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL ? TREE_TYPE(TREE_TYPE(callee)) : TREE_TYPE(call);
    return result != NULL_TREE && TYPE_MAIN_VARIANT(result) == TYPE_MAIN_VARIANT(type);
}

// The tracked object of must-consume that NODE names: a local, a parameter
// by value or by rvalue reference, or a structured binding.  An assignment to
// it is the business of must-consume.
bool is_frame_object(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case VIEW_CONVERT_EXPR:
            case ADDR_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case INDIRECT_REF: {
                tree pointer = tree_strip_any_location_wrapper(TREE_OPERAND(node, 0));
                return TREE_CODE(pointer) == PARM_DECL && TYPE_REF_P(TREE_TYPE(pointer))
                    && TYPE_REF_IS_RVALUE(TREE_TYPE(pointer));
            }
            case TARGET_EXPR:
                return true;
            case VAR_DECL:
                return !TREE_STATIC(node) && !DECL_EXTERNAL(node)
                    && (!TYPE_REF_P(TREE_TYPE(node)) || DECL_DECOMPOSITION_P(node));
            case PARM_DECL:
                return !TYPE_REF_P(TREE_TYPE(node));
            default:
                return false;
        }
    }
    return false;
}

// True when FIELD of the class CLOSURE is a capture by reference of a linear
// object.
bool is_linear_reference_capture(tree closure, tree field) {
    return closure != NULL_TREE && LAMBDA_TYPE_P(TYPE_MAIN_VARIANT(closure)) && field != NULL_TREE
        && TREE_CODE(field) == FIELD_DECL && TYPE_REF_P(TREE_TYPE(field))
        && is_linear_type(TREE_TYPE(TREE_TYPE(field)));
}

// One capture by reference of a linear object, the field FIELD of the closure
// CLOSURE that VALUE initializes (the rule linear-capture).
void record_capture(tree closure, tree field, tree value, const ShapeWalk& walk) {
    tree lambda = CLASSTYPE_LAMBDA_EXPR(TYPE_MAIN_VARIANT(closure));
    location_t fallback = lambda != NULL_TREE ? LAMBDA_EXPR_LOCATION(lambda) : DECL_SOURCE_LOCATION(walk.function);
    tree captured = value;
    for (int depth = 0; captured != NULL_TREE && depth < kTypeDepth && !DECL_P(captured); ++depth) {
        captured = tree_strip_any_location_wrapper(captured);
        if (!EXPR_P(captured) || TREE_OPERAND_LENGTH(captured) == 0) {
            break;
        }
        captured = TREE_OPERAND(captured, 0);
    }
    std::string object = captured != NULL_TREE && DECL_P(captured) ? simple_name(captured) : simple_name(field);
    location_t location =
        value != NULL_TREE && EXPR_P(value) && EXPR_HAS_LOCATION(value) ? EXPR_LOCATION(value) : fallback;
    record_shape(Kind::linear_reference_capture, location, walk.name + ": " + object);
}

// The captures of a closure that a braced list builds.
void check_captures(tree constructor, const ShapeWalk& walk) {
    tree closure = TREE_TYPE(constructor);
    vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(constructor);
    for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
        if (is_linear_reference_capture(closure, (*elements)[index].index)) {
            record_capture(closure, (*elements)[index].index, (*elements)[index].value, walk);
        }
    }
}

// The rule drop-door at an assignment: an assignment over a linear object
// that is no tracked object of must-consume drops its old value.
void check_assignment_target(tree target, location_t location, const ShapeWalk& walk) {
    if (walk.is_exempt_from_drops || target == NULL_TREE || is_frame_object(target)) {
        return;
    }
    tree type = TREE_TYPE(target);
    if (type != NULL_TREE && TYPE_PTR_P(type)) {
        type = TREE_TYPE(type);
    }
    if (type == NULL_TREE || !is_linear_type(type)) {
        return;
    }
    tree stripped = target;
    for (int depth = 0; stripped != NULL_TREE && depth < kTypeDepth; ++depth) {
        stripped = tree_strip_any_location_wrapper(stripped);
        if (TREE_CODE(stripped) != ADDR_EXPR && TREE_CODE(stripped) != NOP_EXPR) {
            break;
        }
        stripped = TREE_OPERAND(stripped, 0);
    }
    std::string object = "an object behind a reference";
    if (stripped != NULL_TREE && TREE_CODE(stripped) == COMPONENT_REF) {
        object = simple_name(TREE_OPERAND(stripped, 1));
    } else if (stripped != NULL_TREE && TREE_CODE(stripped) == ARRAY_REF) {
        object = "an element";
    } else if (stripped != NULL_TREE && DECL_P(stripped)) {
        object = simple_name(stripped);
    }
    record_shape(Kind::linear_untracked_assignment, location, walk.name + ": " + object);
}

// The rules at a call: drop-door, and the call graph of brand-reentry.
void check_call(tree call, ShapeWalk& walk) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
        return;
    }
    location_t location = walk.location_of(call);
    int count = argument_count(call);
    call_graph.callees[DECL_UID(walk.function)].push_back(DECL_UID(callee));
    for (int index = 0; index < count; ++index) {
        tree argument = argument_at(call, index);
        tree type = argument != NULL_TREE ? TREE_TYPE(argument) : NULL_TREE;
        if (type == NULL_TREE || (index == 0 && DECL_IOBJ_MEMBER_FUNCTION_P(callee))) {
            continue;
        }
        if (tree brand = unbound_brand(type, walk.function)) {
            call_graph.branded_calls.push_back(
                BrandedCall{DECL_UID(walk.function), DECL_UID(callee), location,
                            walk.name + ": " + function_text(callee) + ", " + brand_text(brand)});
            break;
        }
    }
    if (walk.is_exempt_from_drops || count < 1 || TREE_CODE(call) != CALL_EXPR) {
        return;
    }
    tree first = argument_at(call, 0);
    if (DECL_ASSIGNMENT_OPERATOR_P(callee) && DECL_CONTEXT(callee) != NULL_TREE && TYPE_P(DECL_CONTEXT(callee))
        && is_linear_type(DECL_CONTEXT(callee))) {
        check_assignment_target(first, location, walk);
    } else if (DECL_DESTRUCTOR_P(callee) && DECL_CONTEXT(callee) != NULL_TREE && TYPE_P(DECL_CONTEXT(callee))
               && is_linear_type(DECL_CONTEXT(callee)) && !is_frame_object(first)) {
        // A delete-expression calls the destructor too, or clobbers the
        // object of a trivial destructor (walk_shapes).
        record_shape(Kind::linear_destroy, location, walk.name + ": " + function_text(callee));
    }
}

// A local of the body: a class of the library with a linear argument, a
// container that holds a fresh brand, and a function pointer whose type holds
// one.  A local whose initializer is a call that gives the same type takes the
// type of that callee, so the signature of the callee holds the finding.
void check_local(tree decl, const ShapeWalk& walk) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || DECL_ARTIFICIAL(decl) || uses_template_parms(type)) {
        return;
    }
    check_class(type);
    bool is_from_call = walk.from_call.count(decl) != 0 || is_call_of_type(DECL_INITIAL(decl), type);
    std::string entity = walk.name + ": " + simple_name(decl);
    tree holder = library_holder_of(type);
    if (holder != NULL_TREE && !is_from_call) {
        record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(decl),
                     entity + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
    }
    tree brand = unbound_brand(type, walk.function);
    if (brand == NULL_TREE) {
        return;
    }
    tree bare = TYPE_MAIN_VARIANT(type);
    if (TYPE_PTR_P(bare) && FUNC_OR_METHOD_TYPE_P(TREE_TYPE(bare))) {
        record_shape(Kind::brand_escaped_function_pointer, DECL_SOURCE_LOCATION(decl),
                     entity + ", " + brand_text(brand));
    } else if (CLASS_TYPE_P(bare) && is_library_class(bare) && !CP_AGGREGATE_TYPE_P(bare) && !is_from_call) {
        record_shape(Kind::brand_escaped_container, DECL_SOURCE_LOCATION(decl), entity + ", " + brand_text(brand));
    }
}

void note_linear_variable(tree variable, ShapeWalk& walk) {
    if (VAR_P(variable) && !TREE_STATIC(variable) && is_linear_or_reference(TREE_TYPE(variable))) {
        walk.has_linear = true;
    }
}

tree walk_shapes(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<ShapeWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node) || DECL_P(node)) {
        *walk_subtrees = 0;
        note_linear_variable(node, walk);
        return NULL_TREE;
    }
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last_location = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                note_linear_variable(variable, walk);
                if (VAR_P(variable) && TREE_STATIC(variable) && !DECL_EXTERNAL(variable)) {
                    check_static_object(variable, walk.name);
                }
            }
            break;
        case DECL_EXPR: {
            tree decl = DECL_EXPR_DECL(node);
            if (decl != NULL_TREE && VAR_P(decl) && !TREE_STATIC(decl) && !DECL_EXTERNAL(decl)) {
                walk.locals.push_back(decl);
                // The walk stops at a declaration, so it reads the
                // initializer here, such as the closure of a lambda.
                if (DECL_INITIAL(decl) != NULL_TREE && DECL_INITIAL(decl) != error_mark_node) {
                    cp_walk_tree(&DECL_INITIAL(decl), walk_shapes, data, &walk.visited);
                }
            }
            break;
        }
        case INIT_EXPR: {
            tree target = tree_strip_any_location_wrapper(TREE_OPERAND(node, 0));
            if (VAR_P(target) && is_call_of_type(TREE_OPERAND(node, 1), TREE_TYPE(target))) {
                walk.from_call.insert(target);
            }
            // A lambda initializes each capture of its closure with one
            // initialization of a field.
            if (TREE_CODE(target) == COMPONENT_REF) {
                tree closure = TREE_TYPE(TREE_OPERAND(target, 0));
                if (is_linear_reference_capture(closure, TREE_OPERAND(target, 1))) {
                    record_capture(closure, TREE_OPERAND(target, 1), TREE_OPERAND(node, 1), walk);
                }
            }
            break;
        }
        case CLEANUP_STMT:
            // The cleanup of a local is its end of scope, which must-consume
            // reads.
            cp_walk_tree(&CLEANUP_BODY(node), walk_shapes, data, &walk.visited);
            *walk_subtrees = 0;
            return NULL_TREE;
        case TARGET_EXPR:
            cp_walk_tree(&TARGET_EXPR_INITIAL(node), walk_shapes, data, &walk.visited);
            *walk_subtrees = 0;
            if (is_linear_type(TREE_TYPE(node))) {
                walk.has_linear = true;
            }
            return NULL_TREE;
        case CONSTRUCTOR:
            if (LAMBDA_TYPE_P(TREE_TYPE(node))) {
                check_captures(node, walk);
            }
            break;
        case AGGR_INIT_EXPR: {
            // A call that builds its value in a local, with no copy.
            tree slot = AGGR_INIT_EXPR_SLOT(node);
            if (slot != NULL_TREE && VAR_P(slot) && !AGGR_INIT_VIA_CTOR_P(node)
                && is_call_of_type(node, TREE_TYPE(slot))) {
                walk.from_call.insert(slot);
            }
            check_call(node, walk);
            break;
        }
        case CALL_EXPR:
            check_call(node, walk);
            break;
        case MODIFY_EXPR:
            // An explicit call of a trivial destructor is a clobber of its
            // object.
            if (TREE_CLOBBER_P(TREE_OPERAND(node, 1))) {
                tree target = TREE_OPERAND(node, 0);
                if (!walk.is_exempt_from_drops && TREE_TYPE(target) != NULL_TREE && is_linear_type(TREE_TYPE(target))
                    && !is_frame_object(target)) {
                    record_shape(Kind::linear_destroy, walk.location_of(node), walk.name + ": the end of a lifetime");
                }
            } else {
                check_assignment_target(TREE_OPERAND(node, 0), walk.location_of(node), walk);
            }
            break;
        default:
            break;
    }
    if ((EXPR_P(node) || TREE_CODE(node) == CONSTRUCTOR) && TREE_TYPE(node) != NULL_TREE
        && is_linear_type(TREE_TYPE(node))) {
        walk.has_linear = true;
    }
    return NULL_TREE;
}

// The rule brand-reentry at the end of the unit: a call with a branded
// argument whose callee can reach the caller again.  Complexity: one search
// of the call graph for each branded call, O(V + E) each.
void report_reentries() {
    for (const BrandedCall& call : call_graph.branded_calls) {
        std::unordered_set<int> seen;
        std::vector<int> pending{call.callee};
        bool reaches = false;
        while (!pending.empty() && !reaches) {
            int current = pending.back();
            pending.pop_back();
            if (current == call.caller) {
                reaches = true;
            } else if (seen.insert(current).second) {
                auto found = call_graph.callees.find(current);
                if (found != call_graph.callees.end()) {
                    pending.insert(pending.end(), found->second.begin(), found->second.end());
                }
            }
        }
        if (reaches) {
            record_shape(Kind::brand_reentry, call.location, call.entity);
        }
    }
}

// ── The bodies ──────────────────────────────────────────────────────────

// The rules that the arguments of the unit run (register_linear).
struct LinearMode {
    bool checks_must_consume = false;
    bool checks_shapes = false;
};

LinearMode linear_mode;

// True when a body holds a linear object or a linear value, from a quick walk,
// in a unit that runs must-consume and not the shapes.  Most bodies hold none,
// and the analysis then does not run.
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

// True when FUNCTION carries the annotation drop_door or detach_door.
bool carries_door_annotation(tree function) {
    tree attributes = DECL_ATTRIBUTES(function);
    return carries_annotation(attributes, linear_names.drop_door)
        || carries_annotation(attributes, linear_names.detach_door);
}

// The linear-door row that names FUNCTION, or null.
const LinearDoorRow* door_row_of(tree function) {
    if (state.table.linear_doors.empty()) {
        return nullptr;
    }
    std::string name = qualified_name(function);
    for (const LinearDoorRow& row : state.table.linear_doors) {
        if (row.function == name) {
            return &row;
        }
    }
    return nullptr;
}

// True when FUNCTION is a door (THE DOORS).  A linear-door row that names
// FUNCTION gives its record at the place of FUNCTION.
bool is_door(tree function, const Place& place) {
    if (carries_door_annotation(function)) {
        return true;
    }
    const LinearDoorRow* row = door_row_of(function);
    if (row != nullptr) {
        add_finding(Kind::linear_door, nullptr, place, row->function);
    }
    return row != nullptr;
}

bool has_scope_door(tree type) {
    if (carries_annotation(TYPE_ATTRIBUTES(type), linear_names.scope_end_door)) {
        return true;
    }
    tree destructor = CLASSTYPE_DESTRUCTOR(type);
    return destructor != NULL_TREE && TREE_CODE(destructor) == FUNCTION_DECL
        && (carries_door_annotation(destructor) || door_row_of(destructor) != nullptr);
}

// The DECL_UID of the field of *this that the expression NODE names through
// casts and member accesses, the innermost field that is no base, or
// kWholeObject when NODE names *this itself or no member of *this (THE
// ACCESSORS).  OBJECT is the object parameter of the function.
int member_field_of(tree node, tree object) {
    tree leaf = NULL_TREE;
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case ADDR_EXPR:
            case INDIRECT_REF:
            case SAVE_EXPR:
            case CLEANUP_POINT_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case COMPONENT_REF:
                if (TREE_CODE(TREE_OPERAND(node, 1)) == FIELD_DECL && !DECL_FIELD_IS_BASE(TREE_OPERAND(node, 1))) {
                    leaf = TREE_OPERAND(node, 1);
                }
                node = TREE_OPERAND(node, 0);
                continue;
            case PARM_DECL:
                return node == object && leaf != NULL_TREE ? static_cast<int>(DECL_UID(leaf)) : kWholeObject;
            default:
                return kWholeObject;
        }
    }
    return kWholeObject;
}

// The state of the walk of the return statements of one accessor.
struct ReturnWalk {
    tree object = NULL_TREE;
    int field = kWholeObject;
    bool saw_return = false;
};

tree walk_returns(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node) || TREE_CODE(node) == LAMBDA_EXPR) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(node) != RETURN_EXPR) {
        return NULL_TREE;
    }
    auto& walk = *static_cast<ReturnWalk*>(data);
    tree value = TREE_OPERAND(node, 0);
    if (value != NULL_TREE && (TREE_CODE(value) == INIT_EXPR || TREE_CODE(value) == MODIFY_EXPR)) {
        value = TREE_OPERAND(value, 1);
    }
    int field = member_field_of(value, walk.object);
    walk.field = !walk.saw_return || walk.field == field ? field : kWholeObject;
    walk.saw_return = true;
    return NULL_TREE;
}

// Keeps the field of *this that the accessor FUNCTION gives (THE ACCESSORS):
// the field of each of its return statements when they agree, else the
// object.  Complexity: linear in the body.
void note_accessor_field(tree function) {
    ReturnWalk walk;
    walk.object = DECL_ARGUMENTS(function);
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_returns, &walk);
    accessor_fields[static_cast<int>(DECL_UID(function))] = walk.saw_return ? walk.field : kWholeObject;
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
    // The fact of an accessor serves each caller in this unit, also when the
    // rule does not read the body of the accessor here (THE ACCESSORS).
    if (linear_mode.checks_must_consume && is_accessor(function)) {
        note_accessor_field(function);
    }
    Place place;
    if (!is_read_here(function, place)) {
        return;
    }
    tree body = DECL_SAVED_TREE(function);
    bool has_linear = has_linear_parameter(function);
    if (linear_mode.checks_shapes) {
        check_signature(function);
        ShapeWalk walk;
        walk.function = function;
        walk.name = function_text(function);
        bool is_door_function = carries_door_annotation(function) || door_row_of(function) != nullptr;
        walk.is_exempt_from_drops = is_door_function || is_transfer_function(function) || DECL_DESTRUCTOR_P(function);
        cp_walk_tree(&body, walk_shapes, &walk, &walk.visited);
        for (tree local : walk.locals) {
            check_local(local, walk);
        }
        has_linear = has_linear || walk.has_linear;
    } else if (!has_linear) {
        has_linear = cp_walk_tree_without_duplicates(&body, find_linear, nullptr) != NULL_TREE;
    }
    if (!linear_mode.checks_must_consume || !has_linear) {
        return;
    }
    Analysis analysis(function, is_door(function, place));
    analysis.run();
}

// PLUGIN_FINISH_TYPE: the class rules for each class that the unit defines
// and that is no pattern (THE SHAPES).
void on_finish_type_linear(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type) || seen_error()) {
        return;
    }
    check_class(type);
}

// PLUGIN_FINISH_DECL: a variable with static storage duration at namespace
// scope (THE SHAPES).  The walk of a body reads each local, and the walk of
// the declarations reads each alias (check_linear_alias).
void on_finish_decl_linear(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl == NULL_TREE || decl == error_mark_node || !VAR_P(decl) || seen_error()
        || TREE_CODE(CP_DECL_CONTEXT(decl)) != NAMESPACE_DECL || is_erroneous_decl(decl)) {
        return;
    }
    bool is_static_object = (TREE_STATIC(decl) || DECL_THREAD_LOCAL_P(decl)) && !DECL_EXTERNAL(decl);
    if (is_static_object && is_reported_for(decl)) {
        check_static_object(decl, qualified_name(CP_DECL_CONTEXT(decl)));
    }
}

// The linear local or parameter that NODE moves through casts and std::move,
// or null (THE DROPPED ELEMENTS).
tree moved_linear_decl(tree node) {
    bool saw_rvalue = false;
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
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
            case TARGET_EXPR:
                node = TARGET_EXPR_INITIAL(node);
                continue;
            case CALL_EXPR: {
                tree callee = cp_get_callee_fndecl_nofold(node);
                if (!is_std_move_cast(callee) || call_expr_nargs(node) < 1) {
                    return NULL_TREE;
                }
                saw_rvalue = saw_rvalue || DECL_NAME(callee) != linear_names.forward;
                node = CALL_EXPR_ARG(node, 0);
                continue;
            }
            case VAR_DECL:
            case PARM_DECL:
                return saw_rvalue && !TREE_STATIC(node) && is_linear_or_reference(TREE_TYPE(node)) ? node : NULL_TREE;
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

tree find_located(tree* node_pointer, int* walk_subtrees, void*) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    return EXPR_P(node) && EXPR_HAS_LOCATION(node) ? node : NULL_TREE;
}

// PLUGIN_FOLDED_INITIALIZER (THE FOLDS).  The fold of an element that moves a
// linear local or parameter is kept for THE DROPPED ELEMENTS, at the location
// of the first node of the original with one.
void on_folded_initializer(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_folded_initializer_data*>(gcc_data);
    if (data == nullptr || data->original == NULL_TREE || data->folded == NULL_TREE || data->original == data->folded
        || data->original == error_mark_node) {
        return;
    }
    folded_initializers[data->folded].push_back(FoldEntry{data->object, data->original});
    if (data->kind != PLUGIN_FOLDED_ELEMENT && data->kind != PLUGIN_FOLDED_ARRAY_ELEMENT) {
        return;
    }
    tree moved = moved_linear_decl(data->original);
    if (moved == NULL_TREE) {
        return;
    }
    tree original = data->original;
    tree located = cp_walk_tree_without_duplicates(&original, find_located, nullptr);
    location_t location = located != NULL_TREE ? EXPR_LOCATION(located) : data->location;
    element_folds.push_back(ElementFold{data->original, data->folded, moved, location});
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
    shape_caches.refuses_lifetime.clear();
    shape_caches.library_holder.clear();
    shape_caches.brands.clear();
    folded_initializers.clear();
    empty_assignments.clear();
    empty_assignment_of_source.clear();
    element_folds.clear();
}

}  // namespace

// An alias at namespace scope, from the walk of the declarations
// (declaration.cpp), because the front end gives no event PLUGIN_FINISH_DECL
// for an alias (THE SHAPES).
void check_linear_alias(tree decl) {
    if (!linear_mode.checks_shapes || decl == NULL_TREE || TREE_CODE(decl) != TYPE_DECL
        || TREE_CODE(CP_DECL_CONTEXT(decl)) != NAMESPACE_DECL || !is_reported_for(decl)) {
        return;
    }
    check_alias(decl, CP_DECL_CONTEXT(decl), qualified_name(CP_DECL_CONTEXT(decl)));
}

// The rule brand-reentry at the end of the unit (plugin.cpp, walk_unit): each
// body of the unit gave its edges of the call graph by then.
void finish_linear_rules() {
    if (linear_mode.checks_shapes && !call_graph.was_reported && !seen_error()) {
        call_graph.was_reported = true;
        report_reentries();
    }
}

// Registers the rules of the family linear (plugin.cpp, the arguments
// must-consume=on and linear-shapes=on).
void register_linear(const char* plugin_name, bool checks_must_consume, bool checks_shapes) {
    linear_mode.checks_must_consume = checks_must_consume;
    linear_mode.checks_shapes = checks_shapes;
    linear_names.linear = get_identifier("linear");
    linear_names.lifetime = get_identifier("lifetime");
    linear_names.no_start_over_bytes = get_identifier("no_start_over_bytes");
    linear_names.brand_type = get_identifier("brand_type");
    linear_names.drop_door = get_identifier("drop_door");
    linear_names.detach_door = get_identifier("detach_door");
    linear_names.scope_end_door = get_identifier("scope_end_door");
    linear_names.forward = get_identifier("forward");
    if (checks_must_consume) {
        register_callback(plugin_name, PLUGIN_FOLDED_INITIALIZER, on_folded_initializer, nullptr);
        register_callback(plugin_name, PLUGIN_EMPTY_CLASS_ASSIGNMENT, on_empty_class_assignment, nullptr);
    }
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_linear, nullptr);
    if (checks_shapes) {
        register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_type_linear, nullptr);
        register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl_linear, nullptr);
    }
    register_callback(plugin_name, PLUGIN_GGC_START, on_linear_collection, nullptr);
}

}  // namespace crucible_plugin
