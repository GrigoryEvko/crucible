// The family live borrow: the kind stale-borrow of the rule view-escape.  A name
// of a function holds a borrow, such as the View of a HeapArray.  Then a move, an
// assignment, a call or the end of a scope ends the borrowed object, and a later
// use of the name reads dead storage.  The rule refuses that use.
//
// Live borrows
//     A value holds a borrow by the facts of its type (escape.cpp, the borrow).
//     The walk gives each such value a set of loans.  A loan has a place, a route
//     and targets.  The place is the borrowed object.  The route is the owners
//     that have the borrowed object behind them, and the targets are the borrowed
//     classes with an end (route.cpp).  The address of an object and the initializer
//     of a reference give a loan with no route.  A call gives its result a loan of
//     each argument that the result reaches into, and the loans that the argument
//     holds.  So no list names the doors: the type of the result and the type of
//     the parameter give the loan.  A closure holds a loan of each object that it
//     captures by reference, and the loans of each captured value.
//
// Places
//     A place is a root and a path.  The root is a local, a parameter or the slot
//     of a temporary.  A step is a field, a dereference or an element, and each
//     element of an array is one place.  A base class is the same object.  Two
//     places overlap when one path starts with the other one.  A reference local
//     and a pointer local name the place of their initializer, until a different
//     store.  std::move, std::forward and std::as_const give their argument back.
//
// Ends
//     These events end a loan:
//       the end of the scope of its root
//       an assignment of an object that has an owner of the route in its bytes, or
//           a target in a member of a union
//       a move of an object that has a target in its bytes
//       a call that takes such an object by a move, when its result cannot hold
//           the owner and its body ends it
//       a call that takes the place by a reference that is not const, when its
//           body ends an owner of the route or a target
//       a call that consumes an owner and takes a closure with a loan of it
//     A write through a borrow does not end that borrow.  A call through a
//     reference does not end the object itself, and a target is no such object.
//
// Transfer
//     A move of an owner keeps the storage behind it, so each loan whose route has
//     a class in the bytes of the moved object goes to the new object.  A call
//     whose result can hold the owner gives the loan to the object of the result.
//     When a different argument can keep the owner too, such as the array of an
//     exchange, the loan goes to no place, and only the body of the call ends it.
//
// Effects
//     The effect of a body on a parameter is the set of the classes with an end
//     that the body assigns, destroys, moves away or replaces by a store to a
//     pointer of their state, and the calls that take an object of the parameter.
//     A capture of a closure has an effect too.  A body that moves an owner of its
//     caller to a local, and then back, ends nothing.  The walk of each body gives
//     its effects, and the end of the unit joins them along the calls.  A function
//     with no body in the unit can end each owner that the type of its parameter
//     owns, apart from a class that keeps its blocks and that no code can move or
//     assign (route.cpp, the owners and ends).
//
// Order
//     The walk reads the statements in their order.  A condition joins the state
//     of its branches, and a loop gets two passes, with the states of each break
//     and each continue.  A loan ends when it ends on one path.  A local reads as
//     moved, and an owner as given back, when that is so on each path.
//
// Use
//     A use is each evaluation of the name, apart from a store to it and a cast to
//     void.  The hidden cursor of a range loop is a use where the loop copies an
//     element.  The finding has the place of the use, and its entity names the
//     place, the name and the end.
//
// Records
//     The front end gives a body before the body of a closure in it, and an
//     instance after its use.  So the walk records each use that a call can make
//     stale with its ends, and the end of the unit judges it.  A record holds
//     locations, identifiers and text, so no collection can free a tree of it.  A
//     body that names an incomplete class waits for the end of the unit (the kept
//     bodies of plugin.h), and only its last reading gives records.
//
// Limits
//     A join takes each end of its branches.  Two elements of one array are one
//     place.  A door of a class gives a loan of its whole object, so a change of a
//     different member of the same type is a finding.  A swap in a callee reads as
//     the end of the two owners, and an exchange as the end of the old owner, also
//     when a local keeps it.  The plant plant-live-limits of
//     utils/plants/view-escape/ pins these findings.
//     A call that keeps a borrow in a different argument or in a member gives no
//     loan, and neither does a container of borrows.  A closure with a declared
//     result that holds a borrow gives its result the loans of its captures, and a
//     generic closure gives none.  A call that consumes an owner beside a plain
//     argument that borrows it is no end: the product gives an arena and a pointer
//     into it to one object (Vigil.h, keep_load_), and the split mints take a Box and
//     the Ref of its object (SpscEnds.h).  The place-based checker owns that form.
//     The control control-live-limits pins each form.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// ── The places ──────────────────────────────────────────────────────────

constexpr int kNoPlace = -1;

enum class StepKind : std::uint8_t {
    root,
    field,
    dereference,
    element,
};

// The places of one body (the places).  A place is an index, and each node knows
// the place that it extends.
class Places {
public:
    int root(tree decl) { return intern(kNoPlace, StepKind::root, decl); }

    int field(int place, tree field) {
        return DECL_FIELD_IS_BASE(field) ? place : intern_step(place, StepKind::field, field);
    }

    int dereference(int place) { return intern_step(place, StepKind::dereference, NULL_TREE); }
    int element(int place) { return intern_step(place, StepKind::element, NULL_TREE); }

    // The root of PLACE, as a declaration.
    tree root_decl(int place) const {
        while (nodes_[place].parent != kNoPlace) {
            place = nodes_[place].parent;
        }
        return nodes_[place].decl;
    }

    // The place of DEPTH steps on the way to PLACE, which has at least as many.
    int ancestor(int place, int depth) const {
        while (nodes_[place].depth > depth) {
            place = nodes_[place].parent;
        }
        return place;
    }

    int depth(int place) const { return nodes_[place].depth; }
    StepKind kind(int place) const { return nodes_[place].kind; }
    tree decl(int place) const { return nodes_[place].decl; }
    int parent(int place) const { return nodes_[place].parent; }

    // True when the path of LONGER starts with the path of SHORTER.
    bool holds(int shorter, int longer) const {
        return shorter != kNoPlace && longer != kNoPlace && nodes_[shorter].depth <= nodes_[longer].depth
            && ancestor(longer, nodes_[shorter].depth) == shorter;
    }

    bool overlaps(int one, int other) const { return holds(one, other) || holds(other, one); }

    // True when a step of PLACE is a dereference.
    bool has_dereference(int place) const {
        for (; place != kNoPlace; place = nodes_[place].parent) {
            if (nodes_[place].kind == StepKind::dereference) {
                return true;
            }
        }
        return false;
    }

    // The place TO with the steps of PLACE below FROM.
    int rebase(int place, int from, int to) {
        if (place == from) {
            return to;
        }
        Node const node = nodes_[place];
        return intern(rebase(node.parent, from, to), node.kind, node.decl);
    }

    // The type of the object at PLACE, or null.
    tree type(int place) const {
        if (place == kNoPlace) {
            return NULL_TREE;
        }
        const Node& node = nodes_[place];
        if (node.decl != NULL_TREE) {
            return TREE_TYPE(node.decl);
        }
        tree held = type(node.parent);
        if (held == NULL_TREE) {
            return NULL_TREE;
        }
        if (is_indirect_type(held)) {
            return TREE_TYPE(held);
        }
        return node.kind == StepKind::element && TREE_CODE(held) == ARRAY_TYPE ? TREE_TYPE(held) : NULL_TREE;
    }

    // The spelling of PLACE.  A capture has the name of the captured object.
    std::string text(int place) const {
        if (place == kNoPlace) {
            return "an object";
        }
        const Node& node = nodes_[place];
        switch (node.kind) {
            case StepKind::root:
                if (DECL_NAME(node.decl) != NULL_TREE && !DECL_ARTIFICIAL(node.decl)) {
                    return identifier_text(DECL_NAME(node.decl));
                }
                return is_this_parameter(node.decl) ? "this" : "a temporary";
            case StepKind::field: {
                std::string const name =
                    DECL_NAME(node.decl) != NULL_TREE ? identifier_text(DECL_NAME(node.decl)) : std::string("a member");
                if (is_closure_member(place)) {
                    return name.compare(0, 2, "__") == 0 ? name.substr(2) : name;
                }
                std::string const base = text(node.parent);
                return base.compare(0, 1, "*") == 0 && base.size() > 1 ? base.substr(1) + "->" + name
                                                                       : base + "." + name;
            }
            case StepKind::dereference: {
                std::string const base = text(node.parent);
                tree pointer = type(node.parent);
                return pointer != NULL_TREE && TREE_CODE(pointer) == REFERENCE_TYPE ? base : "*" + base;
            }
            case StepKind::element:
                return text(node.parent) + "[]";
        }
        return "an object";
    }

private:
    struct Node {
        int parent = kNoPlace;
        StepKind kind = StepKind::root;
        tree decl = NULL_TREE;
        int depth = 0;
    };

    int intern_step(int place, StepKind kind, tree decl) {
        return place == kNoPlace ? kNoPlace : intern(place, kind, decl);
    }

    int intern(int parent, StepKind kind, tree decl) {
        std::uint64_t const key = (static_cast<std::uint64_t>(parent + 1) << 34)
                                | (static_cast<std::uint64_t>(decl != NULL_TREE ? DECL_UID(decl) : 0U) << 2)
                                | static_cast<std::uint64_t>(kind);
        auto [found, is_new] = index_.emplace(key, static_cast<int>(nodes_.size()));
        if (is_new) {
            nodes_.push_back(Node{parent, kind, decl, parent == kNoPlace ? 0 : nodes_[parent].depth + 1});
        }
        return found->second;
    }

    static bool is_this_parameter(tree decl) {
        return TREE_CODE(decl) == PARM_DECL && DECL_NAME(decl) == this_identifier;
    }

    // True for a capture: a member of the object of a call operator.
    bool is_closure_member(int place) const {
        tree context = DECL_CONTEXT(nodes_[place].decl);
        return context != NULL_TREE && TYPE_P(context) && LAMBDA_TYPE_P(context);
    }

    std::vector<Node> nodes_;
    std::unordered_map<std::uint64_t, int> index_;
};

// ── The records of the effects and of the uses ──────────────────────────

// The key of a parameter of a function, and of a capture of a closure: the
// DECL_UID of the function and the index of the parameter, or the TYPE_UID of the
// closure and the DECL_UID of the field.
std::uint64_t parameter_key(tree function, int index) {
    return (static_cast<std::uint64_t>(DECL_UID(function)) << 32) | static_cast<std::uint32_t>(index);
}

std::uint64_t capture_key(unsigned closure, unsigned field) {
    return (std::uint64_t{1} << 63) | (static_cast<std::uint64_t>(closure) << 32) | field;
}

// A call that takes an object: the parameter that it gives, and the owners that
// the type of the parameter owns, which a call with no body can end.
struct Taking {
    std::uint64_t key = 0;
    unsigned callee = 0;  // the DECL_UID of the function, or 0 for a closure or an unknown function
    bool is_capture = false;
    unsigned type = ~0U;  // the key of taken_owners

    bool operator<(const Taking& other) const {
        return std::tie(key, callee, is_capture, type)
             < std::tie(other.key, other.callee, other.is_capture, other.type);
    }
};

// The effect of a body on a parameter or on a capture (the effects).
struct Effect {
    TypeSet ended;
    std::set<Taking> takings;

    bool operator==(const Effect& other) const {
        return ended == other.ended
            && !std::lexicographical_compare(takings.begin(), takings.end(), other.takings.begin(), other.takings.end())
            && !std::lexicographical_compare(other.takings.begin(), other.takings.end(), takings.begin(),
                                             takings.end());
    }
};

enum class EndKind : std::uint8_t {
    scope,
    assignment,
    move,
    call,
};

// One end of a loan that a use reads, with no tree.
struct JudgedEnd {
    EndKind kind = EndKind::scope;
    location_t location = UNKNOWN_LOCATION;
    std::string text;
    Taking taking;
    TypeSet types;  // the owners of the route and the targets that the call must end
};

// A use of a name whose loan has an end (the use).
struct StaleUse {
    location_t location = UNKNOWN_LOCATION;
    std::string borrow;
    std::vector<JudgedEnd> ends;
};

// What one reading of a body gives.
struct Reading {
    std::unordered_map<std::uint64_t, Effect> effects;
    std::vector<StaleUse> uses;
};

// The effects of the unit, the functions with a body, the uses, and the owners of
// each type that a call takes, by its TYPE_UID.
std::unordered_map<std::uint64_t, Effect> unit_effects;
std::unordered_set<unsigned> read_functions;
std::vector<StaleUse> stale_uses;
std::unordered_map<unsigned, TypeSet> taken_owners;

// The end of the borrow of each finding, by the place of its use.
std::unordered_map<location_t, location_t> end_locations;

// The key of a type that owns nothing.  No TYPE_UID has this value.
constexpr unsigned kOwnsNothing = ~0U;

// Keeps the owners of the class HELD that a call can end.  Returns false when the
// walk could not read a part of a class.
bool keep_taken_owners(tree held) {
    TypeSet owners;
    bool const is_open = is_open_question([&] {
        for (const OwnerClass& owner : owner_classes(held)) {
            if (!owner.is_fixed) {
                add_types(owners, TypeSet{owner.id});
            }
        }
    });
    taken_owners.insert_or_assign(TYPE_UID(held), std::move(owners));
    return !is_open;
}

// Reads the owners of the class HELD at the end of the unit.  A class that the unit
// never completes owns itself.
void close_taken_owners(tree held) {
    begin_route_reading(false);
    if (!keep_taken_owners(held)) {
        begin_route_reading(true);
        keep_taken_owners(held);
        begin_route_reading(false);
    }
}

// The key of the owners of the object type HELD that a call takes.
unsigned taken_type(tree held) {
    held = element_class(held);
    if (held == NULL_TREE || !RECORD_OR_UNION_TYPE_P(held)) {
        return kOwnsNothing;
    }
    unsigned const id = TYPE_UID(held);
    if (taken_owners.count(id) == 0 && !keep_taken_owners(held)) {
        check_at_end(CheckSubject{held, NULL_TREE}, [held] { close_taken_owners(held); });
    }
    return id;
}

// ── The walk of one body ────────────────────────────────────────────────

// What gives a loan, for its text.
enum class LoanSource : std::uint8_t {
    address,
    call,
    capture,
};

struct Loan {
    int place = kNoPlace;  // kNoPlace when a callee keeps the owner
    int named = kNoPlace;  // the place at the start, for the text
    LoanSource source = LoanSource::address;
    bool is_unique = false;  // the borrow can write its object
    unsigned closure = 0;  // the closure and the field of a capture
    unsigned field = 0;
    TypeSet route;
    TypeSet targets;

    auto key(location_t location) const {
        return std::make_tuple(place, named, static_cast<int>(source), is_unique, closure, field, route, targets,
                               static_cast<unsigned>(location));
    }
};

// One event that can end a loan.
struct End {
    EndKind kind = EndKind::scope;
    location_t location = UNKNOWN_LOCATION;
    int place = kNoPlace;
    tree callee = NULL_TREE;
    bool is_closure = false;
    Taking taking;
    TypeSet types;
};

// An owner of the caller that a local of this body holds (the effects).
struct HeldOwner {
    int place = kNoPlace;
    tree type = NULL_TREE;
};

// The facts at one point of the body.
struct FlowState {
    std::map<int, std::vector<int>> loans;  // the loans of each local, by its DECL_UID
    std::map<int, int> names;  // the place that each reference or pointer local names
    std::map<int, std::vector<int>> ended;  // the ends of each loan
    std::map<int, std::vector<HeldOwner>> held;  // the owners of the caller in each local
    std::set<int> moved;  // the parameters by value that each path moved away
    bool is_reachable = true;
};

// The states of the jumps of one loop or one switch.
struct JumpTarget {
    bool is_loop = false;
    bool has_default = false;
    std::size_t scopes = 0;
    FlowState entry;
    std::vector<FlowState> breaks;
    std::vector<FlowState> continues;
};

struct Argument {
    tree node = NULL_TREE;
    tree parameter = NULL_TREE;  // the type of the parameter
    tree held = NULL_TREE;  // the type of the object that the call takes
    tree through = NULL_TREE;  // the local reference or pointer that names the object
    std::vector<int> places;
    std::vector<int> loans;
    bool is_write = false;
    bool is_rvalue = false;
    bool is_closure = false;
    bool is_borrow = false;  // the call takes a borrow, and not its object
};

void add_index(std::vector<int>& into, int index) {
    if (std::find(into.begin(), into.end(), index) == into.end()) {
        into.push_back(index);
    }
}

void add_indices(std::vector<int>& into, const std::vector<int>& from) {
    for (int index : from) {
        add_index(into, index);
    }
}

tree strip_value(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case PAREN_EXPR:
            case CLEANUP_POINT_EXPR:
            case SAVE_EXPR:
            case POINTER_PLUS_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            default:
                return node;
        }
    }
    return node;
}

// True for a function of std that gives its argument back.
bool is_identity_call(tree function) { return is_std_move_cast(function) || is_std_as_const(function); }

// The call of an identity function at NODE, or null.
tree identity_call(tree node) {
    return node != NULL_TREE && TREE_CODE(node) == CALL_EXPR && is_identity_call(cp_get_callee_fndecl_nofold(node))
                && call_expr_nargs(node) >= 1
             ? node
             : NULL_TREE;
}

bool is_const_target(tree type) {
    if (!is_indirect_type(type)) {
        return false;
    }
    tree target = TREE_TYPE(type);
    while (target != NULL_TREE && TREE_CODE(target) == ARRAY_TYPE) {
        target = TREE_TYPE(target);
    }
    return target != NULL_TREE && TYPE_READONLY(target);
}

bool is_rvalue_reference(tree type) { return TREE_CODE(type) == REFERENCE_TYPE && TYPE_REF_IS_RVALUE(type); }

bool is_closure_type(tree type) {
    return type != NULL_TREE && CLASS_TYPE_P(type) && LAMBDA_TYPE_P(TYPE_MAIN_VARIANT(type));
}

bool has_end(tree type) {
    type = element_class(type);
    return type != NULL_TREE && CLASS_TYPE_P(type) && COMPLETE_TYPE_P(type) && TYPE_HAS_NONTRIVIAL_DESTRUCTOR(type);
}

bool is_always_true(tree condition) {
    return condition == NULL_TREE || integer_nonzerop(tree_strip_any_location_wrapper(condition));
}

tree type_of_callee(tree call, tree callee) {
    if (callee != NULL_TREE) {
        return TREE_TYPE(callee);
    }
    tree function = callee_of(call);
    tree type = function != NULL_TREE ? TREE_TYPE(function) : NULL_TREE;
    if (type != NULL_TREE && POINTER_TYPE_P(type)) {
        type = TREE_TYPE(type);
    }
    return type != NULL_TREE && FUNC_OR_METHOD_TYPE_P(type) ? type : NULL_TREE;
}

// The object of NODE with each address taken off: the closure or the temporary
// that an argument gives.
tree object_of(tree node) {
    node = strip_value(node);
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(node) == ADDR_EXPR) {
            node = strip_value(TREE_OPERAND(node, 0));
            continue;
        }
        if (tree call = identity_call(node)) {
            node = strip_value(CALL_EXPR_ARG(call, 0));
            continue;
        }
        break;
    }
    return node;
}

// The value of an argument by value: the front end gives a class by value as the
// address of the object of the parameter.
tree value_of_argument(tree node) {
    tree object = object_of(node);
    return object != NULL_TREE && TREE_CODE(object) == TARGET_EXPR ? object : node;
}

// The walk of one body (the order).  Complexity: the statements of the body, times
// two for each loop around them, times the loans of the body.
class LiveWalk {
public:
    LiveWalk(tree function, bool reads_uses) : function_(function), reads_uses_(reads_uses) {
        int index = 0;
        for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE;
             parameter = DECL_CHAIN(parameter), ++index) {
            parameters_.emplace(static_cast<int>(DECL_UID(parameter)), index);
        }
        if (LAMBDA_FUNCTION_P(function) && DECL_OBJECT_MEMBER_FUNCTION_P(function)) {
            closure_object_ = DECL_ARGUMENTS(function);
            closure_ = TYPE_UID(TYPE_MAIN_VARIANT(DECL_CONTEXT(function)));
        }
        location_ = DECL_SOURCE_LOCATION(function);
    }

    Reading run() {
        walk_statement(DECL_SAVED_TREE(function_));
        if (state_.is_reachable) {
            leave();
        }
        return std::move(reading_);
    }

private:
    // ── The places of the expressions ──

    // The place of the lvalue NODE, or kNoPlace.
    int place_of_lvalue(tree node) {
        node = strip_value(node);
        if (node == NULL_TREE) {
            return kNoPlace;
        }
        switch (TREE_CODE(node)) {
            case VAR_DECL:
                if (DECL_HAS_VALUE_EXPR_P(node)) {
                    return place_of_lvalue(DECL_VALUE_EXPR(node));
                }
                return places_.root(node);
            case PARM_DECL:
            case RESULT_DECL:
                return places_.root(node);
            case COMPONENT_REF: {
                tree field = TREE_OPERAND(node, 1);
                return TREE_CODE(field) == FIELD_DECL ? places_.field(place_of_lvalue(TREE_OPERAND(node, 0)), field)
                                                      : kNoPlace;
            }
            case ARRAY_REF:
                return places_.element(place_of_lvalue(TREE_OPERAND(node, 0)));
            case INDIRECT_REF:
                return place_of_pointee(TREE_OPERAND(node, 0));
            case TARGET_EXPR:
                return TARGET_EXPR_SLOT(node) != NULL_TREE && DECL_P(TARGET_EXPR_SLOT(node))
                         ? places_.root(TARGET_EXPR_SLOT(node))
                         : kNoPlace;
            case COMPOUND_EXPR:
                return place_of_lvalue(TREE_OPERAND(node, 1));
            case INIT_EXPR:
            case MODIFY_EXPR:
                return place_of_lvalue(TREE_OPERAND(node, 0));
            case CALL_EXPR:
                return identity_call(node) != NULL_TREE ? place_of_pointee(CALL_EXPR_ARG(node, 0)) : kNoPlace;
            default:
                return kNoPlace;
        }
    }

    // The place that the pointer or the reference value NODE names, or kNoPlace.
    int place_of_pointee(tree node) {
        node = strip_value(node);
        if (node == NULL_TREE) {
            return kNoPlace;
        }
        if (TREE_CODE(node) == ADDR_EXPR) {
            return place_of_lvalue(TREE_OPERAND(node, 0));
        }
        if (TREE_CODE(node) == CALL_EXPR) {
            return identity_call(node) != NULL_TREE ? place_of_pointee(CALL_EXPR_ARG(node, 0)) : kNoPlace;
        }
        if ((VAR_P(node) || TREE_CODE(node) == PARM_DECL) && !DECL_HAS_VALUE_EXPR_P(node)) {
            auto found = state_.names.find(static_cast<int>(DECL_UID(node)));
            if (found != state_.names.end()) {
                return found->second;
            }
        }
        return is_indirect_type(TREE_TYPE(node)) ? places_.dereference(place_of_lvalue(node)) : kNoPlace;
    }

    // The call whose result NODE names through its members and dereferences, or
    // null.
    static tree base_call(tree node) {
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            node = strip_value(node);
            if (node == NULL_TREE) {
                return NULL_TREE;
            }
            switch (TREE_CODE(node)) {
                case ADDR_EXPR:
                case COMPONENT_REF:
                case ARRAY_REF:
                case INDIRECT_REF:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case TARGET_EXPR:
                    node = TARGET_EXPR_INITIAL(node);
                    continue;
                case CALL_EXPR:
                case AGGR_INIT_EXPR:
                    return node;
                default:
                    return NULL_TREE;
            }
        }
        return NULL_TREE;
    }

    // The local reference or pointer through which NODE names its object, or null.
    static tree naming_local(tree node) {
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            node = strip_value(node);
            if (node == NULL_TREE) {
                return NULL_TREE;
            }
            switch (TREE_CODE(node)) {
                case ADDR_EXPR:
                case COMPONENT_REF:
                case ARRAY_REF:
                case INDIRECT_REF:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case VAR_DECL:
                case PARM_DECL:
                    return !DECL_HAS_VALUE_EXPR_P(node) && is_indirect_type(TREE_TYPE(node)) ? node : NULL_TREE;
                default:
                    return NULL_TREE;
            }
        }
        return NULL_TREE;
    }

    // ── The loans of the values ──

    int add_loan(Loan loan) {
        auto [found, is_new] = loan_index_.emplace(loan.key(location_), static_cast<int>(loans_.size()));
        if (is_new) {
            loans_.push_back(std::move(loan));
        } else {
            // A new pass over the site makes the loan live again.
            state_.ended.erase(found->second);
        }
        return found->second;
    }

    // Adds the loan MADE, a form of the loan SOURCE, with the ends of SOURCE.
    int add_derived_loan(Loan made, int source) {
        auto found = state_.ended.find(source);
        std::vector<int> const ends = found != state_.ended.end() ? found->second : std::vector<int>{};
        int const index = add_loan(std::move(made));
        if (!ends.empty()) {
            add_indices(state_.ended[index], ends);
        }
        return index;
    }

    Loan address_loan(int place, tree pointer) {
        Loan loan;
        loan.place = place;
        loan.named = place;
        loan.is_unique = !is_const_target(TREE_TYPE(pointer));
        return loan;
    }

    // The loans of the object at PLACE: those of its root, unless a later
    // dereference leads to an object that the body does not know.
    std::vector<int> loans_of_place(int place) {
        if (place == kNoPlace) {
            return {};
        }
        int dereferences = 0;
        for (int step = place; step != kNoPlace; step = places_.parent(step)) {
            if (places_.kind(step) == StepKind::dereference && places_.depth(step) != 1) {
                ++dereferences;
            }
        }
        if (dereferences != 0) {
            return {};
        }
        auto found = state_.loans.find(static_cast<int>(DECL_UID(places_.root_decl(place))));
        return found != state_.loans.end() ? found->second : std::vector<int>{};
    }

    // The loans of the object at the address NODE.
    std::vector<int> loans_at(tree node) {
        node = strip_value(node);
        if (node == NULL_TREE) {
            return {};
        }
        if (TREE_CODE(node) == ADDR_EXPR) {
            tree object = strip_value(TREE_OPERAND(node, 0));
            if (object != NULL_TREE && TREE_CODE(object) == TARGET_EXPR) {
                return loans_of_value(object);
            }
        }
        if (TREE_CODE(node) == CALL_EXPR && identity_call(node) == NULL_TREE) {
            return loans_of_value(node);
        }
        int const place = place_of_pointee(node);
        if (place == kNoPlace) {
            tree call = base_call(node);
            return call != NULL_TREE && call != node && identity_call(call) == NULL_TREE ? loans_of_call(call)
                                                                                         : std::vector<int>{};
        }
        return loans_of_place(place);
    }

    // The loans that the object at the address NODE holds in its bytes, and no loan
    // of the object.
    std::vector<int> loans_in(tree node) {
        node = strip_value(node);
        if (node == NULL_TREE) {
            return {};
        }
        if (TREE_CODE(node) == ADDR_EXPR) {
            tree object = strip_value(TREE_OPERAND(node, 0));
            if (object != NULL_TREE && TREE_CODE(object) == TARGET_EXPR) {
                return loans_of_value(object);
            }
        }
        int const place = place_of_pointee(node);
        if (place != kNoPlace) {
            return loans_of_place(place);
        }
        std::vector<int> found;
        if (tree call = base_call(node)) {
            for (int index = 0; index < argument_count(call); ++index) {
                tree argument = argument_at(call, index);
                if (argument != NULL_TREE && is_indirect_type(TREE_TYPE(argument))) {
                    add_indices(found, loans_in(argument));
                }
            }
        }
        return found;
    }

    std::vector<int> loans_of_value(tree node) {
        node = strip_value(node);
        if (node == NULL_TREE) {
            return {};
        }
        switch (TREE_CODE(node)) {
            case ADDR_EXPR: {
                std::vector<int> found;
                int const place = place_of_lvalue(TREE_OPERAND(node, 0));
                if (place != kNoPlace) {
                    found.push_back(add_loan(address_loan(place, node)));
                }
                add_indices(found, loans_at(node));
                return found;
            }
            case VAR_DECL:
            case PARM_DECL: {
                if (DECL_HAS_VALUE_EXPR_P(node)) {
                    return loans_of_value(DECL_VALUE_EXPR(node));
                }
                auto known = state_.loans.find(static_cast<int>(DECL_UID(node)));
                std::vector<int> found = known != state_.loans.end() ? known->second : std::vector<int>{};
                // A reference as a value is the address of its object.
                if (TREE_CODE(TREE_TYPE(node)) == REFERENCE_TYPE) {
                    int const place = place_of_pointee(node);
                    if (place != kNoPlace) {
                        add_index(found, add_loan(address_loan(place, node)));
                    }
                }
                return found;
            }
            case TARGET_EXPR:
                return loans_of_value(TARGET_EXPR_INITIAL(node));
            case COND_EXPR: {
                std::vector<int> found = loans_of_value(TREE_OPERAND(node, 1));
                add_indices(found, loans_of_value(TREE_OPERAND(node, 2)));
                return found;
            }
            case COMPOUND_EXPR:
                return loans_of_value(TREE_OPERAND(node, 1));
            case CONSTRUCTOR:
                return loans_of_constructor(node);
            case CALL_EXPR:
            case AGGR_INIT_EXPR:
                return loans_of_call(node);
            case COMPONENT_REF:
            case ARRAY_REF:
            case INDIRECT_REF: {
                // A borrow that a member holds.  A borrow behind a pointer is of
                // an object that the body does not know.
                int const place = place_of_lvalue(node);
                return place != kNoPlace && !places_.has_dereference(place) && holds_borrow(TREE_TYPE(node))
                         ? loans_of_place(place)
                         : std::vector<int>{};
            }
            default:
                return {};
        }
    }

    std::vector<int> loans_of_constructor(tree node) {
        std::vector<int> found;
        bool const is_closure = is_closure_type(TREE_TYPE(node));
        unsigned position = 0;
        tree field = NULL_TREE;
        tree value = NULL_TREE;
        FOR_EACH_CONSTRUCTOR_ELT(CONSTRUCTOR_ELTS(node), position, field, value) {
            if (value != NULL_TREE) {
                add_indices(found, loans_of_member(field, value, is_closure ? TREE_TYPE(node) : NULL_TREE));
            }
        }
        return found;
    }

    // The loans of the value VALUE of the member FIELD of an object.  CLOSURE is
    // the class of a closure, or null.
    std::vector<int> loans_of_member(tree field, tree value, tree closure) {
        bool const is_field = field != NULL_TREE && TREE_CODE(field) == FIELD_DECL;
        unsigned const closure_id = closure != NULL_TREE ? TYPE_UID(TYPE_MAIN_VARIANT(closure)) : 0U;
        std::vector<int> found;
        if (!is_field || !is_indirect_type(TREE_TYPE(field))) {
            for (int loan : loans_of_value(value)) {
                // The call operator can write through a borrow that it captures.
                if (is_field && closure_id != 0 && loans_[loan].is_unique
                    && loans_[loan].source != LoanSource::capture) {
                    Loan captured = loans_[loan];
                    captured.source = LoanSource::capture;
                    captured.closure = closure_id;
                    captured.field = DECL_UID(field);
                    add_index(found, add_derived_loan(std::move(captured), loan));
                }
                add_index(found, loan);
            }
            return found;
        }
        int const place = place_of_pointee(value);
        if (place != kNoPlace) {
            Loan loan;
            loan.place = place;
            loan.named = place;
            loan.is_unique = !is_const_target(TREE_TYPE(field));
            if (closure_id != 0) {
                loan.source = LoanSource::capture;
                loan.closure = closure_id;
                loan.field = DECL_UID(field);
            }
            found.push_back(add_loan(std::move(loan)));
        }
        add_indices(found, place != kNoPlace ? loans_at(value) : loans_of_value(value));
        return found;
    }

    // True for a closure that the callee can only run: the result RESULT holds no
    // closure of its type, and its call operator gives no borrow.
    static bool is_body_argument(tree argument, tree result) {
        tree object = object_of(argument);
        tree type = object != NULL_TREE ? TREE_TYPE(object) : NULL_TREE;
        if (is_indirect_type(type)) {
            type = TREE_TYPE(type);
        }
        type = element_class(type);
        if (!is_closure_type(type) || can_hold_object(result, type)) {
            return false;
        }
        tree function = lambda_function(type);
        if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
            return true;
        }
        return undeduced_auto_decl(function) || !holds_borrow(TREE_TYPE(TREE_TYPE(function)));
    }

    std::vector<int> loans_of_call(tree call) {
        tree callee = cp_get_callee_fndecl_nofold(call);
        if (is_identity_call(callee)) {
            return argument_count(call) >= 1 ? loans_at(argument_at(call, 0)) : std::vector<int>{};
        }
        bool const is_constructor = callee != NULL_TREE && DECL_CONSTRUCTOR_P(callee);
        tree type = type_of_callee(call, callee);
        tree result = is_constructor ? DECL_CONTEXT(callee) : type != NULL_TREE ? TREE_TYPE(type) : TREE_TYPE(call);
        if (!holds_borrow(result)) {
            return {};
        }
        std::vector<int> found;
        tree parameter = type != NULL_TREE ? TYPE_ARG_TYPES(type) : NULL_TREE;
        for (int index = 0; index < argument_count(call); ++index) {
            tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
            parameter = parameter != NULL_TREE ? TREE_CHAIN(parameter) : NULL_TREE;
            if (is_constructor && index == 0) {
                continue;
            }
            add_indices(found, loans_of_argument(argument_at(call, index), parameter_type, result,
                                                 index == 0 && type != NULL_TREE && TREE_CODE(type) == METHOD_TYPE
                                                     && FUNCTION_RVALUE_QUALIFIED(type)));
        }
        return found;
    }

    // The loans that the result RESULT of a call gets from ARGUMENT.  IS_CONSUMED
    // tells that the member consumes its object.
    std::vector<int> loans_of_argument(tree argument, tree parameter, tree result, bool is_consumed) {
        if (is_body_argument(argument, result)) {
            return {};
        }
        if (!is_indirect_type(parameter)) {
            tree type = parameter != NULL_TREE ? parameter : TREE_TYPE(argument);
            return borrow_route(result, type).reach != Reach::none ? loans_of_value(value_of_argument(argument))
                                                                   : std::vector<int>{};
        }
        tree held = TREE_TYPE(parameter);
        const BorrowRoute& route = borrow_route(result, held);
        if (route.reach == Reach::none) {
            return {};
        }
        // An object that the call moves into its result lends nothing, and the
        // borrows that it holds go on.
        if ((is_consumed && !is_indirect_type(result))
            || (is_rvalue_reference(parameter) && can_hold_object(result, held))) {
            return loans_in(argument);
        }
        bool const is_into = route.reach == Reach::into;
        BorrowRoute const copy = is_into ? route : BorrowRoute{};
        std::vector<int> found;
        int const place = is_into ? place_of_pointee(argument) : kNoPlace;
        if (place != kNoPlace) {
            Loan loan;
            loan.place = place;
            loan.named = place;
            loan.source = LoanSource::call;
            loan.is_unique = !is_const_target(parameter);
            loan.route = copy.route;
            loan.targets = copy.targets;
            found.push_back(add_loan(std::move(loan)));
        }
        for (int source : loans_at(argument)) {
            Loan derived = loans_[source];
            if (is_into) {
                add_types(derived.route, copy.route);
                derived.targets = copy.targets;
            }
            add_index(found, add_derived_loan(std::move(derived), source));
        }
        return found;
    }

    // ── The stores ──

    // A name of a structured binding that reads a member through get() names its
    // own element, so it has no loan of the hidden object.
    static bool is_tuple_binding(tree decl) {
        return DECL_DECOMPOSITION_P(decl) && !DECL_DECOMP_IS_BASE(decl) && !DECL_HAS_VALUE_EXPR_P(decl);
    }

    void store_local(tree decl, tree value) {
        int const id = static_cast<int>(DECL_UID(decl));
        state_.loans.erase(id);
        state_.names.erase(id);
        tree type = TREE_TYPE(decl);
        if (!holds_borrow(type) || is_tuple_binding(decl)) {
            return;
        }
        if (is_indirect_type(type)) {
            int const place = place_of_pointee(value);
            if (place != kNoPlace) {
                state_.names.emplace(id, place);
            }
        }
        std::vector<int> loans = loans_of_value(value);
        if (!loans.empty()) {
            state_.loans.emplace(id, std::move(loans));
        }
    }

    // A store to a member of a local.  The front end gives an initializer that is
    // no constant as one store for each member.
    void store_member(tree target, tree value) {
        tree object = strip_value(TREE_OPERAND(target, 0));
        tree field = TREE_OPERAND(target, 1);
        if (object == NULL_TREE || !VAR_P(object) || DECL_HAS_VALUE_EXPR_P(object) || TREE_CODE(field) != FIELD_DECL
            || !holds_borrow(TREE_TYPE(object))) {
            return;
        }
        std::vector<int> const added =
            loans_of_member(field, value, is_closure_type(TREE_TYPE(object)) ? TREE_TYPE(object) : NULL_TREE);
        if (!added.empty()) {
            add_indices(state_.loans[static_cast<int>(DECL_UID(object))], added);
        }
    }

    // ── The effects ──

    int parameter_index(int place) const {
        if (place == kNoPlace) {
            return -1;
        }
        auto found = parameters_.find(static_cast<int>(DECL_UID(places_.root_decl(place))));
        return found != parameters_.end() ? found->second : -1;
    }

    // Calls VISIT with the effect on the parameter of PLACE, and with the effect on
    // the capture of PLACE.
    template <typename Visit>
    void visit_effects(int place, Visit visit) {
        int const index = parameter_index(place);
        if (index < 0) {
            return;
        }
        if (closure_object_ != NULL_TREE && index == 0 && places_.depth(place) >= 2) {
            int const member = places_.ancestor(place, 2);
            if (places_.kind(member) == StepKind::field) {
                visit(reading_.effects[capture_key(closure_, DECL_UID(places_.decl(member)))]);
            }
        }
        visit(reading_.effects[parameter_key(function_, index)]);
    }

    void note_ended(int place, tree type) {
        const TypeSet& parts = ended_parts(type);
        if (!parts.empty()) {
            visit_effects(place, [&](Effect& effect) { add_types(effect.ended, parts); });
        }
    }

    void note_taking(int place, const Taking& taking) {
        visit_effects(place, [&](Effect& effect) { effect.takings.insert(taking); });
    }

    // True for a parameter that the caller gives by a move.
    bool is_given_parameter(int place) const {
        tree decl = places_.root_decl(place);
        return TREE_CODE(decl) == PARM_DECL
            && (!is_indirect_type(TREE_TYPE(decl)) || is_rvalue_reference(TREE_TYPE(decl)));
    }

    // The local ROOT gets the owner at PLACE by a move.
    void hold_owner(int root, int place, tree type) {
        if (root == kNoPlace || parameter_index(place) < 0) {
            move_held(place, root);
            return;
        }
        if (parameter_index(root) >= 0) {
            return;
        }
        std::vector<HeldOwner>& held = state_.held[root_id(root)];
        for (const HeldOwner& known : held) {
            if (known.place == place) {
                return;
            }
        }
        held.push_back(HeldOwner{place, type});
    }

    int root_id(int place) const { return static_cast<int>(DECL_UID(places_.root_decl(place))); }

    // The owners of the caller in the local of FROM go to the local of TO.
    void move_held(int from, int to) {
        if (from == kNoPlace || to == kNoPlace || root_id(from) == root_id(to)) {
            return;
        }
        auto found = state_.held.find(root_id(from));
        if (found == state_.held.end()) {
            return;
        }
        std::vector<HeldOwner> const moved = found->second;
        state_.held.erase(found);
        for (const HeldOwner& held : moved) {
            hold_owner(to, held.place, held.type);
        }
    }

    // True when the local of SOURCE holds the owner that PLACE had, so a move of the
    // local to PLACE gives it back.
    bool takes_back(int source, int place) {
        if (source == kNoPlace) {
            return false;
        }
        tree decl = places_.root_decl(source);
        // A name of a structured binding names a part of its hidden object.
        if (VAR_P(decl) && DECL_DECOMPOSITION_P(decl) && !DECL_DECOMP_IS_BASE(decl)
            && DECL_DECOMP_BASE(decl) != NULL_TREE) {
            decl = DECL_DECOMP_BASE(decl);
        }
        auto found = state_.held.find(static_cast<int>(DECL_UID(decl)));
        if (found == state_.held.end()) {
            return false;
        }
        auto& held = found->second;
        auto const back =
            std::find_if(held.begin(), held.end(), [&](const HeldOwner& one) { return one.place == place; });
        if (back == held.end()) {
            return false;
        }
        held.erase(back);
        return true;
    }

    // The body ends here: each owner of the caller that a local still holds, and
    // each parameter by value that the path did not move away, ends with it.
    void leave() {
        tree result = TREE_TYPE(TREE_TYPE(function_));
        for (const auto& [local, held] : state_.held) {
            for (const HeldOwner& owner : held) {
                if (!can_hold_object(result, owner.type)) {
                    note_ended(owner.place, owner.type);
                }
            }
        }
        for (tree parameter = DECL_ARGUMENTS(function_); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
            if (!is_indirect_type(TREE_TYPE(parameter)) && has_end(TREE_TYPE(parameter))
                && state_.moved.count(static_cast<int>(DECL_UID(parameter))) == 0) {
                note_ended(places_.root(parameter), TREE_TYPE(parameter));
            }
        }
    }

    // ── The ends ──

    int add_end(End end) {
        for (std::size_t index = 0; index < ends_.size(); ++index) {
            const End& known = ends_[index];
            if (known.kind == end.kind && known.location == end.location && known.place == end.place
                && known.callee == end.callee && known.types == end.types && !(known.taking < end.taking)
                && !(end.taking < known.taking)) {
                return static_cast<int>(index);
            }
        }
        ends_.push_back(std::move(end));
        return static_cast<int>(ends_.size()) - 1;
    }

    End make_end(EndKind kind, int place) const {
        End end;
        end.kind = kind;
        end.location = location_;
        end.place = place;
        return end;
    }

    void end_loan(int loan, const End& end) { add_index(state_.ended[loan], add_end(end)); }

    // Ends each loan that END of the object at PLACE ends (the ends).  TYPE is the
    // type of that object.  The loans of the local THROUGH stay, because the event
    // writes through them.
    void end_loans(int place, const End& end, tree type, tree through) {
        if (!state_.is_reachable || place == kNoPlace) {
            return;
        }
        std::vector<int> kept;
        if (through != NULL_TREE) {
            auto found = state_.loans.find(static_cast<int>(DECL_UID(through)));
            if (found != state_.loans.end()) {
                kept = found->second;
            }
        }
        for (std::size_t index = 0; index < loans_.size(); ++index) {
            int const loan = static_cast<int>(index);
            if (!places_.overlaps(loans_[index].place, place)
                || std::find(kept.begin(), kept.end(), loan) != kept.end()) {
                continue;
            }
            End made = end;
            if (ends_loan(loans_[index], place, made, type)) {
                end_loan(loan, made);
            }
        }
    }

    // True when END of the object at PLACE ends LOAN.  It gives a call the classes
    // that its body must end.
    bool ends_loan(const Loan& loan, int place, End& end, tree type) {
        switch (end.kind) {
            case EndKind::scope:
                // The object behind a pointer does not end with the pointer.
                return places_.holds(place, loan.place) && !has_dereference_below(loan.place, place);
            case EndKind::assignment:
            case EndKind::move:
                return have_common_type(loan.route, ended_parts(type))
                    || (places_.holds(place, loan.place) && have_common_type(loan.targets, ended_cells(type)));
            case EndKind::call:
                end.types = loan.route;
                add_types(end.types, loan.targets);
                // A call through a reference does not end the object itself.
                if (type != NULL_TREE && loan.place == place && !is_rvalue_end_) {
                    tree object = element_class(type);
                    unsigned const id = object != NULL_TREE ? TYPE_UID(object) : 0U;
                    if (!std::binary_search(loan.route.begin(), loan.route.end(), id)) {
                        end.types.erase(std::remove(end.types.begin(), end.types.end(), id), end.types.end());
                    }
                }
                return !end.types.empty();
        }
        return false;
    }

    bool has_dereference_below(int place, int from) const {
        for (int step = place; step != kNoPlace && step != from; step = places_.parent(step)) {
            if (places_.kind(step) == StepKind::dereference) {
                return true;
            }
        }
        return false;
    }

    // The loans of FROM go to TO with the owner that the route names (the
    // transfer).  A move ends a loan of a part of the moved object.
    void transfer_loans(int from, int to, tree type) {
        if (!state_.is_reachable || from == kNoPlace) {
            return;
        }
        const TypeSet& parts = ended_parts(type);
        tree object = element_class(type);
        unsigned const object_id = object != NULL_TREE ? TYPE_UID(object) : 0U;
        std::map<int, int> moved;
        std::size_t const count = loans_.size();
        for (std::size_t index = 0; index < count; ++index) {
            int const loan = static_cast<int>(index);
            if (!places_.holds(from, loans_[index].place)) {
                continue;
            }
            if (!have_common_type(loans_[index].route, parts)) {
                bool names_part = false;
                for (unsigned target : loans_[index].targets) {
                    names_part =
                        names_part || (target != object_id && std::binary_search(parts.begin(), parts.end(), target));
                }
                if (names_part) {
                    end_loan(loan, make_end(EndKind::move, from));
                }
                continue;
            }
            Loan made = loans_[index];
            made.place = to == kNoPlace ? kNoPlace : places_.rebase(made.place, from, to);
            moved.emplace(loan, add_derived_loan(std::move(made), loan));
        }
        if (moved.empty()) {
            return;
        }
        for (auto& [local, loans] : state_.loans) {
            for (int& loan : loans) {
                auto found = moved.find(loan);
                if (found != moved.end()) {
                    loan = found->second;
                }
            }
        }
    }

    // The temporary that initializes the object at TO is that object.
    void transfer_temporary(tree value, int to) {
        value = strip_value(value);
        if (value == NULL_TREE || to == kNoPlace) {
            return;
        }
        tree slot = TREE_CODE(value) == TARGET_EXPR    ? TARGET_EXPR_SLOT(value)
                  : TREE_CODE(value) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_SLOT(value)
                                                       : NULL_TREE;
        if (slot == NULL_TREE || !DECL_P(slot)) {
            return;
        }
        int const from = places_.root(slot);
        transfer_loans(from, to, TREE_TYPE(slot));
        move_held(from, to);
    }

    // ── The uses ──

    std::string end_text(const End& end) const {
        switch (end.kind) {
            case EndKind::scope:
                return "the end of the scope of " + places_.text(end.place);
            case EndKind::assignment:
                return "an assignment of " + places_.text(end.place);
            case EndKind::move:
                return "a move of " + places_.text(end.place);
            case EndKind::call:
                break;
        }
        if (end.is_closure) {
            return "a call of a closure that captures " + places_.text(end.place);
        }
        return "a call of " + (end.callee != NULL_TREE ? qualified_name(end.callee) : std::string("a function pointer"))
             + " with " + places_.text(end.place);
    }

    // Records a use of NAME, whose loan LOAN has an end (the records).
    void record_use(const std::string& name, int loan, const std::vector<int>& ends) {
        std::string key = name + ":" + std::to_string(loan) + ":" + std::to_string(static_cast<unsigned>(location_));
        for (int end : ends) {
            key += "," + std::to_string(end);
        }
        if (!recorded_.insert(key).second) {
            return;
        }
        StaleUse use;
        use.location = location_;
        use.borrow = "borrow of " + places_.text(loans_[loan].named) + " in " + name;
        for (int index : ends) {
            const End& end = ends_[index];
            use.ends.push_back(JudgedEnd{end.kind, end.location, end_text(end), end.taking, end.types});
        }
        reading_.uses.push_back(std::move(use));
    }

    // A use of the local DECL by the name NAME.
    void read_use(tree decl, tree name) {
        if (!reads_uses_ || !state_.is_reachable) {
            return;
        }
        auto found = state_.loans.find(static_cast<int>(DECL_UID(decl)));
        if (found == state_.loans.end()) {
            return;
        }
        for (int loan : found->second) {
            auto ended = state_.ended.find(loan);
            if (ended != state_.ended.end() && !ended->second.empty()) {
                record_use(identifier_text(DECL_NAME(name)), loan, ended->second);
            }
        }
    }

    void read_name(tree decl) {
        if (DECL_NAME(decl) == NULL_TREE) {
            return;
        }
        if (VAR_P(decl) && DECL_HAS_VALUE_EXPR_P(decl)) {
            // A name of a structured binding reads its hidden object.
            if (DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_BASE(decl) != NULL_TREE && holds_borrow(TREE_TYPE(decl))) {
                read_use(DECL_DECOMP_BASE(decl), decl);
            }
            return;
        }
        if (!DECL_ARTIFICIAL(decl) || reads_cursors_) {
            read_use(decl, decl);
        }
    }

    // ── The statements ──

    void join(FlowState& into, const FlowState& other) {
        if (!other.is_reachable) {
            return;
        }
        if (!into.is_reachable) {
            into = other;
            return;
        }
        for (const auto& [local, loans] : other.loans) {
            add_indices(into.loans[local], loans);
        }
        for (auto name = into.names.begin(); name != into.names.end();) {
            auto found = other.names.find(name->first);
            bool const is_same = found != other.names.end() && found->second == name->second;
            name = is_same ? std::next(name) : into.names.erase(name);
        }
        for (const auto& [loan, ends] : other.ended) {
            add_indices(into.ended[loan], ends);
        }
        for (const auto& [local, held] : other.held) {
            for (const HeldOwner& owner : held) {
                auto& known = into.held[local];
                bool const is_known = std::any_of(known.begin(), known.end(),
                                                  [&](const HeldOwner& one) { return one.place == owner.place; });
                if (!is_known) {
                    known.push_back(owner);
                }
            }
        }
        for (auto moved = into.moved.begin(); moved != into.moved.end();) {
            moved = other.moved.count(*moved) != 0 ? std::next(moved) : into.moved.erase(moved);
        }
    }

    // Ends the scope of each variable of BLOCK.
    void end_scope(tree block) {
        for (tree variable = BIND_EXPR_VARS(block); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
            if (VAR_P(variable) && !TREE_STATIC(variable) && !DECL_EXTERNAL(variable) && !DECL_ARTIFICIAL(variable)
                && DECL_NAME(variable) != NULL_TREE && TREE_CODE(TREE_TYPE(variable)) != REFERENCE_TYPE
                && !DECL_HAS_VALUE_EXPR_P(variable)) {
                int const place = places_.root(variable);
                end_loans(place, make_end(EndKind::scope, place), TREE_TYPE(variable), NULL_TREE);
                state_.loans.erase(static_cast<int>(DECL_UID(variable)));
            }
        }
    }

    // Jumps out of the loop or of the switch TARGET.
    void jump(JumpTarget& target, bool is_break) {
        if (!state_.is_reachable) {
            return;
        }
        FlowState const here = state_;
        for (std::size_t index = scopes_.size(); index > target.scopes; --index) {
            end_scope(scopes_[index - 1]);
        }
        (is_break ? target.breaks : target.continues).push_back(state_);
        state_ = here;
        state_.is_reachable = false;
    }

    void walk_statement(tree node) {
        if (node == NULL_TREE || node == error_mark_node) {
            return;
        }
        switch (TREE_CODE(node)) {
            case STATEMENT_LIST:
                for (tree_stmt_iterator item = tsi_start(node); !tsi_end_p(item); tsi_next(&item)) {
                    walk_statement(tsi_stmt(item));
                }
                return;
            case BIND_EXPR:
                scopes_.push_back(node);
                walk_statement(BIND_EXPR_BODY(node));
                scopes_.pop_back();
                end_scope(node);
                return;
            case DECL_EXPR:
                walk_declaration(DECL_EXPR_DECL(node));
                return;
            case IF_STMT:
                walk_branches(IF_COND(node), THEN_CLAUSE(node), ELSE_CLAUSE(node));
                return;
            case FOR_STMT:
                walk_statement(FOR_INIT_STMT(node));
                walk_loop(FOR_COND(node), FOR_BODY(node), FOR_EXPR(node), false);
                return;
            case WHILE_STMT:
                walk_loop(WHILE_COND(node), WHILE_BODY(node), NULL_TREE, false);
                return;
            case DO_STMT:
                walk_loop(DO_COND(node), DO_BODY(node), NULL_TREE, true);
                return;
            case SWITCH_STMT:
                walk_switch(node);
                return;
            case CASE_LABEL_EXPR:
                if (!jumps_.empty()) {
                    JumpTarget& target = jumps_.back();
                    target.has_default = target.has_default || CASE_LOW(node) == NULL_TREE;
                    join(state_, target.entry);
                }
                return;
            case RETURN_EXPR:
                walk_expression(TREE_OPERAND(node, 0));
                if (state_.is_reachable) {
                    leave();
                }
                state_.is_reachable = false;
                return;
            case BREAK_STMT:
                if (!jumps_.empty()) {
                    jump(jumps_.back(), true);
                }
                return;
            case CONTINUE_STMT:
                for (std::size_t index = jumps_.size(); index > 0; --index) {
                    if (jumps_[index - 1].is_loop) {
                        jump(jumps_[index - 1], false);
                        break;
                    }
                }
                return;
            case LABEL_EXPR:
                state_.is_reachable = true;
                return;
            case CLEANUP_STMT:
                walk_statement(CLEANUP_BODY(node));
                return;
            case EXPR_STMT:
                walk_expression(EXPR_STMT_EXPR(node));
                return;
            case TRY_BLOCK:
                walk_statement(TRY_STMTS(node));
                return;
            case USING_STMT:
                return;
            default:
                break;
        }
        if (STATEMENT_CLASS_P(node)) {
            for (int index = 0; index < TREE_OPERAND_LENGTH(node); ++index) {
                walk_statement(TREE_OPERAND(node, index));
            }
            return;
        }
        walk_expression(node);
    }

    void walk_declaration(tree decl) {
        if (!VAR_P(decl) || TREE_STATIC(decl) || DECL_INITIAL(decl) == NULL_TREE
            || DECL_INITIAL(decl) == error_mark_node) {
            return;
        }
        tree value = DECL_INITIAL(decl);
        // The copy of an element of a range loop reads the cursor of the loop.
        bool const outer = std::exchange(reads_cursors_, !DECL_ARTIFICIAL(decl) && !holds_borrow(TREE_TYPE(decl)));
        walk_expression(value);
        reads_cursors_ = outer;
        transfer_temporary(value, places_.root(decl));
        store_local(decl, value);
    }

    void walk_branches(tree condition, tree first, tree second) {
        walk_expression(condition);
        // The condition of an if constexpr or of an if consteval is a constant.
        tree value = condition != NULL_TREE ? tree_strip_any_location_wrapper(condition) : NULL_TREE;
        if (value != NULL_TREE && (integer_nonzerop(value) || integer_zerop(value))) {
            walk_statement(integer_nonzerop(value) ? first : second);
            return;
        }
        FlowState const entry = state_;
        walk_statement(first);
        FlowState const after = std::exchange(state_, entry);
        walk_statement(second);
        join(state_, after);
    }

    // A loop: two passes, so a use reads an end of the pass before.  A state leaves
    // the loop where its condition is false and at each break.
    void walk_loop(tree condition, tree body, tree step, bool is_condition_last) {
        FlowState head = state_;
        FlowState exits;
        exits.is_reachable = false;
        for (int pass = 0; pass < 2; ++pass) {
            state_ = head;
            if (!is_condition_last) {
                walk_expression(condition);
                if (!is_always_true(condition)) {
                    join(exits, state_);
                }
            }
            jumps_.push_back(JumpTarget{});
            jumps_.back().is_loop = true;
            jumps_.back().scopes = scopes_.size();
            walk_statement(body);
            JumpTarget const target = std::move(jumps_.back());
            jumps_.pop_back();
            for (const FlowState& jumped : target.continues) {
                join(state_, jumped);
            }
            walk_expression(step);
            if (is_condition_last) {
                walk_expression(condition);
                if (!is_always_true(condition)) {
                    join(exits, state_);
                }
            }
            for (const FlowState& jumped : target.breaks) {
                join(exits, jumped);
            }
            join(head, state_);
        }
        if (!is_condition_last && !is_always_true(condition)) {
            state_ = head;
            walk_expression(condition);
            join(exits, state_);
        }
        state_ = exits;
    }

    void walk_switch(tree node) {
        walk_expression(SWITCH_STMT_COND(node));
        jumps_.push_back(JumpTarget{});
        jumps_.back().scopes = scopes_.size();
        jumps_.back().entry = state_;
        state_.is_reachable = false;
        walk_statement(SWITCH_STMT_BODY(node));
        JumpTarget const target = std::move(jumps_.back());
        jumps_.pop_back();
        for (const FlowState& jumped : target.breaks) {
            join(state_, jumped);
        }
        if (!target.has_default && !SWITCH_STMT_ALL_CASES_P(node)) {
            join(state_, target.entry);
        }
    }

    // ── The expressions ──

    void walk_expression(tree node) {
        if (node == NULL_TREE || node == error_mark_node || TYPE_P(node)) {
            return;
        }
        if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
            location_ = EXPR_LOCATION(node);
        }
        tree const slot = std::exchange(result_slot_, NULL_TREE);
        switch (TREE_CODE(node)) {
            case VAR_DECL:
            case PARM_DECL:
                read_name(node);
                return;
            case CALL_EXPR:
            case AGGR_INIT_EXPR:
                result_slot_ = slot;
                walk_call(node);
                return;
            case INIT_EXPR:
            case MODIFY_EXPR:
                walk_store(node);
                return;
            case TARGET_EXPR:
                result_slot_ = TARGET_EXPR_SLOT(node);
                walk_expression(TARGET_EXPR_INITIAL(node));
                result_slot_ = NULL_TREE;
                return;
            case COND_EXPR:
                walk_branches(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1), TREE_OPERAND(node, 2));
                return;
            case TRUTH_ANDIF_EXPR:
            case TRUTH_ORIF_EXPR: {
                walk_expression(TREE_OPERAND(node, 0));
                FlowState const entry = state_;
                walk_expression(TREE_OPERAND(node, 1));
                join(state_, entry);
                return;
            }
            case STMT_EXPR:
                walk_statement(STMT_EXPR_STMT(node));
                return;
            case CONVERT_EXPR:
            case NOP_EXPR:
                // A cast of a name to void reads nothing.
                if (VOID_TYPE_P(TREE_TYPE(node))) {
                    tree value = strip_value(TREE_OPERAND(node, 0));
                    if (value != NULL_TREE
                        && (DECL_P(value)
                            || (TREE_CODE(value) == INDIRECT_REF && DECL_P(strip_value(TREE_OPERAND(value, 0)))))) {
                        return;
                    }
                }
                walk_expression(TREE_OPERAND(node, 0));
                return;
            case BIND_EXPR:
            case STATEMENT_LIST:
            case DECL_EXPR:
            case RETURN_EXPR:
            case IF_STMT:
            case CLEANUP_STMT:
            case EXPR_STMT:
                walk_statement(node);
                return;
            case LAMBDA_EXPR:
            case SIZEOF_EXPR:
            case ALIGNOF_EXPR:
            case NOEXCEPT_EXPR:
            case REQUIRES_EXPR:
                return;
            case CONSTRUCTOR: {
                unsigned position = 0;
                tree value = NULL_TREE;
                FOR_EACH_CONSTRUCTOR_VALUE(CONSTRUCTOR_ELTS(node), position, value) { walk_expression(value); }
                return;
            }
            case TREE_LIST:
                for (tree item = node; item != NULL_TREE; item = TREE_CHAIN(item)) {
                    walk_expression(TREE_VALUE(item));
                }
                return;
            default:
                break;
        }
        if (EXPR_P(node) || STATEMENT_CLASS_P(node)) {
            for (int index = 0; index < TREE_OPERAND_LENGTH(node); ++index) {
                walk_expression(TREE_OPERAND(node, index));
            }
        }
    }

    // The names that the target of a store reads: the pointer of each dereference
    // and each index.
    void walk_target(tree target) {
        for (int depth = 0; target != NULL_TREE && depth < kTypeDepth; ++depth) {
            target = strip_value(target);
            if (target == NULL_TREE) {
                return;
            }
            switch (TREE_CODE(target)) {
                case COMPONENT_REF:
                    target = TREE_OPERAND(target, 0);
                    continue;
                case ARRAY_REF:
                    walk_expression(TREE_OPERAND(target, 1));
                    target = TREE_OPERAND(target, 0);
                    continue;
                case INDIRECT_REF:
                    walk_expression(TREE_OPERAND(target, 0));
                    return;
                case VAR_DECL:
                case PARM_DECL:
                case RESULT_DECL:
                    return;
                default:
                    walk_expression(target);
                    return;
            }
        }
    }

    void walk_store(tree node) {
        tree target = TREE_OPERAND(node, 0);
        tree value = TREE_OPERAND(node, 1);
        walk_expression(value);
        walk_target(target);
        int const place = place_of_lvalue(target);
        if (TREE_CODE(node) == INIT_EXPR) {
            transfer_temporary(value, place);
        } else {
            end_loans(place, make_end(EndKind::assignment, place), TREE_TYPE(target), naming_local(target));
            note_ended(place, TREE_TYPE(target));
            note_state_store(target, place);
        }
        tree stripped = strip_value(target);
        if (stripped == NULL_TREE) {
            return;
        }
        if ((VAR_P(stripped) || TREE_CODE(stripped) == PARM_DECL) && !DECL_HAS_VALUE_EXPR_P(stripped)) {
            store_local(stripped, value);
        } else if (TREE_CODE(stripped) == COMPONENT_REF) {
            store_member(stripped, value);
        }
    }

    // A store to a pointer of the state of an owner replaces the storage behind the
    // owner.  It is an effect only: the body that writes the state is of the owner,
    // and its own pointers can keep the storage.  The constructor of the owner has no
    // storage to replace.
    void note_state_store(tree target, int place) {
        tree stripped = strip_value(target);
        if (stripped == NULL_TREE || TREE_CODE(stripped) != COMPONENT_REF || place == kNoPlace
            || DECL_CONSTRUCTOR_P(function_)) {
            return;
        }
        tree field = TREE_OPERAND(stripped, 1);
        if (TREE_CODE(field) != FIELD_DECL || !holds_pointer(TREE_TYPE(field))) {
            return;
        }
        TypeSet owners;
        tree object = stripped;
        for (int depth = 0; object != NULL_TREE && depth < kTypeDepth; ++depth) {
            object = strip_value(object);
            if (object == NULL_TREE) {
                break;
            }
            add_releasing_classes(element_class(TREE_TYPE(object)), owners);
            if (!owners.empty()) {
                break;
            }
            if (TREE_CODE(object) == COMPONENT_REF || TREE_CODE(object) == ARRAY_REF
                || TREE_CODE(object) == INDIRECT_REF) {
                object = TREE_OPERAND(object, 0);
                if (is_indirect_type(TREE_TYPE(object)) && TREE_CODE(strip_value(object)) != COMPONENT_REF) {
                    add_releasing_classes(element_class(TREE_TYPE(TREE_TYPE(object))), owners);
                }
            } else if (VAR_P(object) && state_.names.count(static_cast<int>(DECL_UID(object))) != 0) {
                add_owners_above(state_.names[static_cast<int>(DECL_UID(object))], owners);
                break;
            } else {
                break;
            }
        }
        if (!owners.empty()) {
            visit_effects(place, [&](Effect& effect) { add_types(effect.ended, owners); });
        }
    }

    // Adds the innermost owner that holds the object at PLACE to OWNERS.
    void add_owners_above(int place, TypeSet& owners) {
        for (int step = place; step != kNoPlace && owners.empty(); step = places_.parent(step)) {
            tree decl = places_.decl(step);
            tree type = decl != NULL_TREE ? TREE_TYPE(decl) : NULL_TREE;
            if (is_indirect_type(type)) {
                type = TREE_TYPE(type);
            }
            add_releasing_classes(element_class(type), owners);
            if (owners.empty() && decl != NULL_TREE && TREE_CODE(decl) == FIELD_DECL) {
                add_releasing_classes(element_class(DECL_CONTEXT(decl)), owners);
            }
        }
    }

    // True when an object of TYPE is a pointer or holds one.
    static bool holds_pointer(tree type) {
        type = element_class(type);
        if (type == NULL_TREE || is_indirect_type(type)) {
            return type != NULL_TREE;
        }
        if (!RECORD_OR_UNION_TYPE_P(type) || !COMPLETE_TYPE_P(type)) {
            return false;
        }
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
            if (TREE_CODE(field) == FIELD_DECL && holds_pointer(TREE_TYPE(field))) {
                return true;
            }
        }
        return false;
    }

    // Adds TYPE and its bases to OWNERS when one of them has a destructor that a
    // person wrote.
    static bool add_releasing_classes(tree type, TypeSet& owners) {
        if (type == NULL_TREE || !has_end(type) || keeps_its_blocks(type)) {
            return false;
        }
        tree destructor = CLASSTYPE_DESTRUCTOR(type);
        bool is_releasing = destructor != NULL_TREE && user_provided_p(destructor);
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
            if (TREE_CODE(field) == FIELD_DECL && DECL_FIELD_IS_BASE(field)) {
                is_releasing = add_releasing_classes(element_class(TREE_TYPE(field)), owners) || is_releasing;
            }
        }
        if (is_releasing) {
            add_types(owners, TypeSet{TYPE_UID(type)});
        }
        return is_releasing;
    }

    // ── The calls ──

    // The places that a take of PLACE takes: PLACE, and the place of each loan of a
    // local that PLACE goes through.
    std::vector<int> taken_places(int place) {
        std::vector<int> found;
        if (place == kNoPlace) {
            return found;
        }
        found.push_back(place);
        if (places_.depth(place) == 0 || places_.kind(places_.ancestor(place, 1)) != StepKind::dereference) {
            return found;
        }
        int const pointer = places_.ancestor(place, 1);
        auto known = state_.loans.find(root_id(place));
        if (known != state_.loans.end()) {
            for (int loan : known->second) {
                if (loans_[loan].place != kNoPlace) {
                    add_index(found, places_.rebase(place, pointer, loans_[loan].place));
                }
            }
        }
        return found;
    }

    Argument read_argument(tree node, tree parameter, bool is_consumed_object) {
        Argument argument;
        argument.node = node;
        argument.parameter = parameter;
        argument.held = is_indirect_type(parameter) ? TREE_TYPE(parameter) : parameter;
        tree object = object_of(node);
        argument.is_closure = object != NULL_TREE && is_closure_type(TREE_TYPE(object));
        if (is_indirect_type(parameter)) {
            argument.is_rvalue = is_consumed_object || is_rvalue_reference(parameter);
            argument.is_write = argument.is_rvalue || !is_const_target(parameter);
            argument.loans = loans_at(node);
            argument.places = taken_places(place_of_pointee(node));
            if (argument.places.empty()) {
                for (int loan : argument.loans) {
                    if (loans_[loan].place != kNoPlace) {
                        add_index(argument.places, loans_[loan].place);
                    }
                }
            }
            argument.through = naming_local(node);
            argument.is_borrow = !argument.is_closure && !argument.is_rvalue && is_const_target(parameter)
                              && CLASS_TYPE_P(element_class(argument.held)) && holds_borrow(argument.held);
            return argument;
        }
        tree value = value_of_argument(node);
        argument.loans = loans_of_value(value);
        argument.is_rvalue = has_end(parameter);
        argument.is_write = argument.is_rvalue;
        if (argument.is_rvalue) {
            // The object of the parameter holds the owner that the caller moves in.
            if (TREE_CODE(value) == TARGET_EXPR && TARGET_EXPR_SLOT(value) != NULL_TREE
                && DECL_P(TARGET_EXPR_SLOT(value))) {
                argument.places.push_back(places_.root(TARGET_EXPR_SLOT(value)));
            }
        } else {
            argument.is_borrow = !argument.is_closure && CLASS_TYPE_P(parameter) && holds_borrow(parameter);
        }
        return argument;
    }

    void walk_call(tree call) {
        tree callee = cp_get_callee_fndecl_nofold(call);
        if (callee != NULL_TREE && TREE_CODE(callee) != FUNCTION_DECL) {
            callee = NULL_TREE;
        }
        tree const slot = TREE_CODE(call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_SLOT(call) : result_slot_;
        result_slot_ = NULL_TREE;
        int const count = argument_count(call);
        bool const is_constructor = callee != NULL_TREE && DECL_CONSTRUCTOR_P(callee);
        bool const is_assignment = callee != NULL_TREE && DECL_ASSIGNMENT_OPERATOR_P(callee) && count == 2;
        location_t const here = EXPR_HAS_LOCATION(call) ? EXPR_LOCATION(call) : location_;
        if (callee == NULL_TREE) {
            walk_expression(callee_of(call));
        }
        for (int index = 0; index < count; ++index) {
            // The target of an assignment is a store.
            if (is_assignment && index == 0) {
                walk_target(object_of(argument_at(call, index)));
            } else if (!(is_constructor && index == 0)) {
                walk_expression(argument_at(call, index));
            }
        }
        location_ = here;
        if (is_identity_call(callee)) {
            return;
        }
        tree type = type_of_callee(call, callee);
        if (type == NULL_TREE) {
            return;
        }
        std::vector<Argument> arguments;
        tree parameter = TYPE_ARG_TYPES(type);
        for (int index = 0; index < count; ++index) {
            tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
            parameter = parameter != NULL_TREE ? TREE_CHAIN(parameter) : NULL_TREE;
            if (parameter_type == NULL_TREE || VOID_TYPE_P(parameter_type) || (is_constructor && index == 0)) {
                arguments.emplace_back();
                continue;
            }
            arguments.push_back(
                read_argument(argument_at(call, index), parameter_type,
                              index == 0 && TREE_CODE(type) == METHOD_TYPE && FUNCTION_RVALUE_QUALIFIED(type)));
        }
        int built = kNoPlace;
        if (is_constructor && count >= 1) {
            built = TREE_CODE(call) == AGGR_INIT_EXPR ? (slot != NULL_TREE ? place_of_lvalue(slot) : kNoPlace)
                                                      : place_of_pointee(argument_at(call, 0));
        }
        CallFacts facts;
        facts.callee = callee;
        facts.result = is_constructor ? DECL_CONTEXT(callee) : TREE_TYPE(type);
        facts.built = built;
        facts.result_place = slot != NULL_TREE && DECL_P(slot) ? places_.root(slot) : kNoPlace;
        facts.is_constructor = is_constructor;
        facts.is_assignment = is_assignment;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            take_argument(facts, arguments, index);
        }
        for (const Argument& argument : arguments) {
            for (int loan : argument.loans) {
                if (loans_[loan].source == LoanSource::capture) {
                    take_capture(loan);
                }
            }
        }
        if (is_assignment) {
            store_assigned_object(arguments);
        } else {
            store_built_object(call, slot, is_constructor);
        }
        if (callee != NULL_TREE && TREE_THIS_VOLATILE(callee)) {
            state_.is_reachable = false;
        }
    }

    struct CallFacts {
        tree callee = NULL_TREE;
        tree result = NULL_TREE;
        int built = kNoPlace;
        int result_place = kNoPlace;
        bool is_constructor = false;
        bool is_assignment = false;
    };

    // The taking of the parameter INDEX of CALLEE.
    Taking taking_of(tree callee, std::size_t index, tree held) const {
        Taking taking;
        taking.type = taken_type(held);
        if (callee != NULL_TREE) {
            tree function = DECL_CLONED_FUNCTION_P(callee) ? DECL_CLONED_FUNCTION(callee) : callee;
            taking.callee = DECL_UID(function);
            taking.key = parameter_key(function, static_cast<int>(index));
        }
        return taking;
    }

    void end_by_call(const CallFacts& facts, const Argument& argument, std::size_t index, int place) {
        End end = make_end(EndKind::call, place);
        end.callee = facts.callee;
        end.taking = taking_of(facts.callee, index, argument.held);
        is_rvalue_end_ = argument.is_rvalue;
        end_loans(place, end, argument.held, argument.through);
        is_rvalue_end_ = false;
        note_taking(place, end.taking);
    }

    // What the call does to the object of one argument (the ends, the transfer and
    // the effects).
    void take_argument(const CallFacts& facts, const std::vector<Argument>& arguments, std::size_t index) {
        const Argument& argument = arguments[index];
        if (argument.parameter == NULL_TREE || (facts.callee != NULL_TREE && fndecl_built_in_p(facts.callee))) {
            return;
        }
        if (argument.is_borrow) {
            take_borrow(facts, argument, index);
            return;
        }
        if (!argument.is_write) {
            return;
        }
        if (argument.is_rvalue && !facts.is_constructor && !facts.is_assignment) {
            check_taken_together(facts, arguments, index);
        }
        for (int place : argument.places) {
            if (facts.is_assignment && index == 0) {
                End end = make_end(EndKind::assignment, place);
                end_loans(place, end, argument.held, argument.through);
                int const source =
                    arguments.size() > 1 && !arguments[1].places.empty() ? arguments[1].places[0] : kNoPlace;
                if (!takes_back(source, place)) {
                    note_ended(place, argument.held);
                }
            } else if (facts.callee != NULL_TREE && DECL_DESTRUCTOR_P(facts.callee)) {
                end_loans(place, make_end(EndKind::assignment, place), argument.held, argument.through);
                note_ended(place, argument.held);
            } else if (argument.is_rvalue) {
                take_moved(facts, arguments, index, place);
            } else {
                end_by_call(facts, argument, index, place);
            }
        }
    }

    // A call that takes a borrow that can write its object takes that object.
    void take_borrow(const CallFacts& facts, const Argument& argument, std::size_t index) {
        for (int loan : argument.loans) {
            if (!loans_[loan].is_unique || loans_[loan].place == kNoPlace
                || loans_[loan].source == LoanSource::capture) {
                continue;
            }
            int const place = loans_[loan].place;
            End end = make_end(EndKind::call, place);
            end.callee = facts.callee;
            end.taking = taking_of(facts.callee, index, argument.held);
            if (!state_.is_reachable) {
                continue;
            }
            for (std::size_t other = 0; other < loans_.size(); ++other) {
                int const ended = static_cast<int>(other);
                End made = end;
                if (std::find(argument.loans.begin(), argument.loans.end(), ended) == argument.loans.end()
                    && places_.overlaps(loans_[other].place, place)
                    && ends_loan(loans_[other], place, made, places_.type(place))) {
                    end_loan(ended, made);
                }
            }
            note_taking(place, end.taking);
        }
    }

    // A call that consumes an owner runs a closure that holds a loan of the owner.
    void check_taken_together(const CallFacts& facts, const std::vector<Argument>& arguments, std::size_t index) {
        const Argument& argument = arguments[index];
        const TypeSet& parts = ended_parts(argument.held);
        tree held = element_class(argument.held);
        bool const is_outside = held != NULL_TREE && CLASS_TYPE_P(held) && is_outside_owner(held);
        for (std::size_t other = 0; other < arguments.size(); ++other) {
            if (other == index || !arguments[other].is_closure) {
                continue;
            }
            for (int loan : arguments[other].loans) {
                for (int place : argument.places) {
                    bool const is_of_owner = have_common_type(loans_[loan].route, parts)
                                          || (is_outside && loans_[loan].source == LoanSource::call);
                    if (is_of_owner && places_.overlaps(loans_[loan].place, place) && reads_uses_
                        && state_.is_reachable) {
                        End end = make_end(EndKind::move, place);
                        std::string const body =
                            facts.callee != NULL_TREE ? qualified_name(facts.callee) : std::string("a call");
                        record_use("a closure of " + body, loan, {add_end(end)});
                    }
                }
            }
        }
    }

    // A call that takes the object at PLACE by a move.
    void take_moved(const CallFacts& facts, const std::vector<Argument>& arguments, std::size_t index, int place) {
        const Argument& argument = arguments[index];
        bool const is_same_class = facts.is_constructor && element_class(facts.result) == element_class(argument.held);
        note_moved(place);
        if (is_same_class && index == 1 && arguments.size() == 2) {
            transfer_loans(place, facts.built, argument.held);
            note_kept(place, facts.built, argument.held);
            return;
        }
        if (facts.is_assignment && index == 1) {
            int const target = arguments[0].places.empty() ? kNoPlace : arguments[0].places[0];
            transfer_loans(place, target, argument.held);
            note_kept(place, target, argument.held);
            return;
        }
        if (!can_hold_object(facts.result, argument.held)) {
            end_by_call(facts, argument, index, place);
            transfer_loans(place, kNoPlace, argument.held);
            return;
        }
        // The owner goes on in the result, unless the body of the callee ends it.
        // The callee can also keep it in a different argument.
        bool is_kept_elsewhere = false;
        for (std::size_t other = 0; other < arguments.size() && !facts.is_constructor; ++other) {
            const Argument& holder = arguments[other];
            is_kept_elsewhere = is_kept_elsewhere
                             || (other != index && holder.is_write && is_indirect_type(holder.parameter)
                                 && !holder.is_closure && can_hold_object(holder.held, argument.held));
        }
        int const kept = is_kept_elsewhere ? kNoPlace : facts.is_constructor ? facts.built : facts.result_place;
        if (kept != kNoPlace && places_.root_decl(kept) == places_.root_decl(place)) {
            transfer_loans(place, kNoPlace, argument.held);
            return;
        }
        End end = make_end(EndKind::call, place);
        end.callee = facts.callee;
        end.taking = taking_of(facts.callee, index, argument.held);
        if (!facts.is_constructor) {
            is_rvalue_end_ = true;
            end_loans(place, end, argument.held, NULL_TREE);
            is_rvalue_end_ = false;
            note_taking(place, end.taking);
        }
        transfer_loans(place, kept, argument.held);
        if (kept != kNoPlace) {
            hold_owner(kept, place, argument.held);
        } else if (!is_kept_elsewhere && parameter_index(place) >= 0 && !is_given_parameter(place)
                   && !can_hold_object(TREE_TYPE(TREE_TYPE(function_)), argument.held)) {
            note_ended(place, argument.held);
        }
    }

    // The path moves the object at PLACE away.
    void note_moved(int place) {
        if (places_.depth(place) == 0) {
            state_.moved.insert(root_id(place));
        }
    }

    // A move keeps the owner at PLACE in the object at TO.  An owner of the caller
    // that goes to a different object of the caller, or to an object that the body
    // does not know, leaves its place.  A caller that gives its owner by a move
    // knows that.
    void note_kept(int place, int to, tree type) {
        if (to != kNoPlace && (parameter_index(to) < 0 || parameter_index(place) < 0)) {
            hold_owner(to, place, type);
        } else if (parameter_index(place) >= 0 && !is_given_parameter(place)) {
            note_ended(place, type);
        }
    }

    // A call can run a closure, and then its body takes the captured objects.
    void take_capture(int index) {
        Loan const loan = loans_[index];
        if (loan.place == kNoPlace) {
            return;
        }
        End end = make_end(EndKind::call, loan.place);
        end.is_closure = true;
        end.taking.key = capture_key(loan.closure, loan.field);
        end.taking.is_capture = true;
        if (state_.is_reachable) {
            for (std::size_t other = 0; other < loans_.size(); ++other) {
                const Loan& known = loans_[other];
                bool const is_same_capture = known.source == LoanSource::capture && known.closure == loan.closure;
                End made = end;
                if (!is_same_capture && places_.overlaps(known.place, loan.place)
                    && ends_loan(known, loan.place, made, places_.type(loan.place))) {
                    end_loan(static_cast<int>(other), made);
                }
            }
        }
        note_taking(loan.place, end.taking);
    }

    // The object that an assignment operator writes holds the loans of the assigned
    // value.  A member keeps the loans of the other members.
    void store_assigned_object(const std::vector<Argument>& arguments) {
        if (arguments.size() != 2 || arguments[0].places.empty()) {
            return;
        }
        int const place = arguments[0].places[0];
        if (places_.has_dereference(place) || !holds_borrow(TREE_TYPE(places_.root_decl(place)))) {
            return;
        }
        int const id = root_id(place);
        if (places_.depth(place) == 0) {
            state_.loans.erase(id);
        }
        if (!arguments[1].loans.empty()) {
            add_indices(state_.loans[id], arguments[1].loans);
        }
    }

    // The object that a call builds holds the loans of the call.
    void store_built_object(tree call, tree slot, bool is_constructor) {
        tree object = NULL_TREE;
        if (TREE_CODE(call) == AGGR_INIT_EXPR) {
            object = slot;
        } else if (is_constructor && argument_count(call) >= 1) {
            object = object_of(argument_at(call, 0));
        }
        if (object == NULL_TREE || !VAR_P(object) || DECL_HAS_VALUE_EXPR_P(object) || TREE_STATIC(object)
            || !holds_borrow(TREE_TYPE(object))) {
            return;
        }
        std::vector<int> loans = loans_of_call(call);
        int const id = static_cast<int>(DECL_UID(object));
        state_.loans.erase(id);
        if (!loans.empty()) {
            state_.loans.emplace(id, std::move(loans));
        }
    }

    tree function_;
    bool reads_uses_;
    tree closure_object_ = NULL_TREE;
    unsigned closure_ = 0;
    location_t location_ = UNKNOWN_LOCATION;
    // The object that the call in hand initializes.
    tree result_slot_ = NULL_TREE;
    // True where the hidden cursor of a range loop is a use.
    bool reads_cursors_ = false;
    // True while the event in hand takes an object by a move.
    bool is_rvalue_end_ = false;
    std::unordered_map<int, int> parameters_;
    Places places_;
    std::vector<Loan> loans_;
    std::map<decltype(std::declval<Loan>().key(UNKNOWN_LOCATION)), int> loan_index_;
    std::vector<End> ends_;
    FlowState state_;
    std::vector<JumpTarget> jumps_;
    std::vector<tree> scopes_;
    std::set<std::string> recorded_;
    Reading reading_;
};

// ── The bodies ──────────────────────────────────────────────────────────

// True when the body of FUNCTION can end an owner of its caller.
bool has_effect(tree function) {
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        tree type = TREE_TYPE(parameter);
        bool const is_const = is_indirect_type(type) && is_const_target(type);
        tree held = element_class(is_indirect_type(type) ? TREE_TYPE(type) : type);
        if (held == NULL_TREE || !RECORD_OR_UNION_TYPE_P(held)) {
            continue;
        }
        if (is_closure_type(held) || !COMPLETE_TYPE_P(held) || holds_borrow(held)
            || (!is_const && !owner_classes(held).empty())) {
            return true;
        }
    }
    return false;
}

tree find_borrow(tree* node, int* walk_subtrees, void*) {
    if (TYPE_P(*node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(*node) == DECL_EXPR && VAR_P(DECL_EXPR_DECL(*node))
        && holds_borrow(TREE_TYPE(DECL_EXPR_DECL(*node)))) {
        return *node;
    }
    return NULL_TREE;
}

// True when a local or a parameter of FUNCTION can hold a borrow.
bool has_borrow(tree function) {
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        if (!is_indirect_type(TREE_TYPE(parameter)) && holds_borrow(TREE_TYPE(parameter))) {
            return true;
        }
    }
    return cp_walk_tree_without_duplicates(&DECL_SAVED_TREE(function), find_borrow, nullptr) != NULL_TREE;
}

// The last reading that an open question left open.
Reading open_reading;

Reading read_live(tree function) {
    bool const reads_uses = is_body_read_here(function, UNKNOWN_LOCATION);
    if (!has_effect(function) && !(reads_uses && has_borrow(function))) {
        return Reading{};
    }
    return LiveWalk(function, reads_uses).run();
}

// Adds the records of the body of FUNCTION to the records of the unit.
void commit(tree function, Reading&& reading) {
    read_functions.insert(DECL_UID(function));
    for (auto& [key, effect] : reading.effects) {
        Effect& known = unit_effects[key];
        add_types(known.ended, effect.ended);
        known.takings.insert(effect.takings.begin(), effect.takings.end());
    }
    for (StaleUse& use : reading.uses) {
        stale_uses.push_back(std::move(use));
    }
}

// A reading that takes an incomplete class as one that can name, hold and own each
// object.
Reading read_live_wide(tree function) {
    begin_route_reading(true);
    set_wide_borrows(true);
    Reading reading = read_live(function);
    set_wide_borrows(false);
    begin_route_reading(false);
    return reading;
}

// The reading (BodyReader of plugin.h).  A class that the unit never completes gets
// the wide reading at the end.
void read_body(tree function) {
    unsigned const unread = unread_parts;
    begin_route_reading(false);
    Reading reading = read_live(function);
    if (is_run_again()) {
        open_reading = std::move(reading);
        return;
    }
    commit(function, unread_parts == unread ? std::move(reading) : read_live_wide(function));
}

// The wide reading (BodyReader of plugin.h).  A body whose wide reading gives no
// use and the same effects needs no second reading.
bool read_body_wide(tree function) {
    Reading const reading = read_live_wide(function);
    if (!reading.uses.empty() || !(reading.effects == open_reading.effects)) {
        return true;
    }
    commit(function, std::move(open_reading));
    return false;
}

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    // A clone of a constructor or a destructor gets a second event on a copy of a
    // body that cp_genericize lowered.
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || seen_error()) {
        return;
    }
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    // The constant evaluator refuses a read of an ended object in a consteval
    // function itself.
    if (DECL_IMMEDIATE_FUNCTION_P(function)) {
        read_functions.insert(DECL_UID(function));
        return;
    }
    read_or_keep_body(function, BodyReader{&read_body, &read_body_wide, nullptr});
}

// ── The judgment ────────────────────────────────────────────────────────

// What the calls of a taking can end: the classes that a body ends, and the owners
// that a call with no body can end.
struct TakingEnds {
    TypeSet ended;
    TypeSet unseen;
};

void collect_ends(const Taking& taking, TakingEnds& ends, std::set<std::uint64_t>& seen) {
    if (!taking.is_capture && read_functions.count(taking.callee) == 0) {
        auto owners = taken_owners.find(taking.type);
        if (owners != taken_owners.end()) {
            add_types(ends.unseen, owners->second);
        }
        return;
    }
    if (!seen.insert(taking.key).second) {
        return;
    }
    auto effect = unit_effects.find(taking.key);
    if (effect == unit_effects.end()) {
        return;
    }
    add_types(ends.ended, effect->second.ended);
    for (const Taking& next : effect->second.takings) {
        collect_ends(next, ends, seen);
    }
}

// The ends of each taking that a use asked about.
std::map<Taking, TakingEnds> taking_ends;

const TakingEnds& ends_of(const Taking& taking) {
    auto found = taking_ends.find(taking);
    if (found == taking_ends.end()) {
        TakingEnds ends;
        std::set<std::uint64_t> seen;
        collect_ends(taking, ends, seen);
        found = taking_ends.emplace(taking, std::move(ends)).first;
    }
    return found->second;
}

// The text of END when it ends the borrow, or an empty text.
std::string judged_text(const JudgedEnd& end) {
    if (end.kind != EndKind::call) {
        return end.text;
    }
    const TakingEnds& ends = ends_of(end.taking);
    if (have_common_type(end.types, ends.ended)) {
        return end.text;
    }
    // After an error of the unit, a function with no record can have a body.
    if (!seen_error() && have_common_type(end.types, ends.unseen)) {
        return end.text + ", which reaches a function with no body in this unit";
    }
    return std::string();
}

}  // namespace

void finish_live_borrows() {
    for (const StaleUse& use : stale_uses) {
        Place place = place_of(use.location, Scope::quarantine);
        if (place.file == nullptr) {
            continue;
        }
        for (const JudgedEnd& end : use.ends) {
            std::string const text = judged_text(end);
            if (!text.empty()) {
                add_finding(Kind::stale_borrow, nullptr, place, use.borrow + " after " + text);
                end_locations.emplace(place.spelling, end.location);
                break;
            }
        }
    }
    stale_uses.clear();
    unit_effects.clear();
    taking_ends.clear();
}

void report_stale_borrow(const Finding& finding) {
    error_at(finding.spelling, "this use reads a %s", finding.entity.c_str());
    auto end = end_locations.find(finding.spelling);
    if (end != end_locations.end()) {
        inform(end->second, "the borrowed object can end here, so the use reads dead storage");
    }
    inform(finding.spelling, "take the borrow again after the change, or change the owner after the last use of the "
                             "borrow");
    inform(finding.spelling,
           "a test of the use itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

void register_live_borrows(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
}

}  // namespace crucible_plugin
