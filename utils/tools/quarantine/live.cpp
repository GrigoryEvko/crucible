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
// Parts
//     A door of a part gives a reference to one part of a product type at the
//     constant index of its first template argument: std::get of a std::tuple, a
//     std::pair and a std::array, and Record::at (kPartDoors).  FixedArray::at
//     gives only a const reference, and each change of an element goes through a
//     door of the whole array, so a part of it would change no finding.  The loan
//     of a door of a part, and its name as an argument, is the part at that
//     index, a step with the index.  Two parts of different indices are two
//     objects.  A part overlaps an element of no index and each field of its
//     object, because the walk does not map an index to a field.  A sum type, such
//     as a std::variant or a OneOf, has no row: its alternatives share storage.  A
//     door with no row gives a loan of its whole object.
//
// Ends
//     These events end a loan:
//       the end of the scope of its root
//       the end of the full expression of a temporary that a move gave the owner
//       an assignment of an object that has an owner of the route in its bytes, or
//           a target in a member of a union
//       a move of an object that has a target in its bytes, and a move of an
//           object of a class that states linear{} for a loan of its own bytes
//       a call that takes such an object by a move, when its result cannot hold
//           the owner and its body ends it
//       a call that takes the place by a reference that is not const, when its
//           body ends an owner of the route or a target
//       a call that consumes an owner and takes a closure with a loan of it
//       a door of an owner of a resource by its number that takes the owner by
//           an rvalue reference (the numbers)
//     A write through a borrow does not end that borrow.  A call through a
//     reference does not end the object itself, and a target is no such object.
//
// Numbers
//     A class that states owns_by_number{} of foundation/Transfer.h, such as a
//     Handle, owns a resource of the kernel by its number, and a class that states
//     refers_by_number{}, such as a HandleRef, refers to it.  The first type
//     argument of each names the resource, so the borrow has it as a target and the
//     owner has it behind itself (route.cpp, the numbers).  The doors of the owner
//     that take it by an rvalue reference end the number: a member of the owner,
//     such as depart(), and a function whose parameter names the owner by an
//     rvalue reference in its general declaration, such as with_stream_halves.  A
//     constructor and an assignment of the owner keep the number, so the loans of
//     a plain move stay with the source.  A generic function that forwards a T&&
//     moves the value, so the loans follow it, and the call ends them only by its
//     body, as for each other owner.
//
// Ends beside a borrow
//     One argument of a call can give a borrow, as a View or the capture of a
//     closure, and a different argument can give its object by a reference that is
//     not const, by a move, or by a closure that captures it so.  The callee can end
//     the object through the second argument and then read the borrow, so the call is
//     a use of the borrow after that end.  The effects of the body judge the end, as
//     they judge a later use.
//
// Borrows through a capture
//     A closure argument also holds each loan that its captures hold: the loans of a
//     captured local, such as a View that the closure captures by reference, and a
//     copy of a borrow.  Its own captures are no such loan, so a capture of the
//     object itself stays with the ends beside a borrow.  Beside an argument that
//     gives the object of such a loan, the call is a use of the loan.  The callee
//     changes the object when it ends an owner of the route or a target, and also
//     when it moves such an owner out of the object, into a call or into a local.  A
//     move counts also when the body gives the owner back: while the owner is away,
//     the callee can lend its storage under a unique borrow and run the closure,
//     which reads the same bytes.  A frame door changes the object too, when it
//     lends the object in place to the call operator of a closure: under a unique
//     borrow, or by a reference to a member or an element of it, and the lent types
//     meet the types that the loan names.  A reference to the object itself is a
//     second name of it, as a capture of the object itself is.  Each use of the loan
//     counts, also a read of its size.  A function with no body in the unit that
//     gets the object and the closure can lend the one to the other, so it changes
//     the object too.  Such a use comes after each other use at the end of the unit,
//     so a finding of a different form at the same place keeps its text.
//
// Transfer
//     A move of an owner keeps the storage behind it, so each loan whose route has
//     a class in the bytes of the moved object goes to the new object.  A call
//     whose result can hold the owner gives the loan to the object of the result.
//     When a different argument can keep the owner too, such as the array of an
//     exchange, the loan goes to no place, and only the body of the call ends it.
//     An owner can also leave its wrapper, as the array leaves in take() of a class
//     that holds it.  Then the result cannot hold the wrapper, but it can hold each
//     owner of the route whose own pointers name a target.  The loan goes to the
//     result, unless the body of the call ends such an owner.  When the result holds
//     no owner of the object, and one different argument by a pointer or a reference
//     that is not const can hold the object, as the pointer of std::construct_at
//     does, the loan goes to the object of that argument.  A call that takes the
//     object by a reference that writes it can move a member out into its result,
//     as take() of a keyed holder gives its array out.  When the class of the result
//     is an owner that the object holds by value, each loan whose owner the result
//     can hold also names the object of the result, so the end of the result ends
//     the borrow (share_with_result).  An object behind a pointer
//     is no local of the body, so a body that moves an owner into it ends nothing.
//     The front end gives a trivial assignment of a class from a temporary as a read
//     of the temporary through its address, so the store gets the loans of the
//     temporary, as an assignment through operator= does.
//
// Effects
//     The effect of a body on a parameter is the set of the classes with an end
//     that the body assigns, destroys, moves away or replaces by a store to a
//     pointer of their state, and the calls that take an object of the parameter.
//     A capture of a closure has an effect too.  A body that moves an owner of its
//     caller to a local, and then back, ends nothing, but the effect keeps the moved
//     classes for the borrows through a capture.  The effect on an owner inside
//     the object has the ends of a path that come before the owner leaves the object,
//     and the end of a local that still holds it.  So expect() of an Option moves the
//     payload out, clears the Option, and ends no owner inside it.  The walk of each
//     body gives its effects, and the end of the unit joins them along the calls.  A
//     function with no body in the unit can end each owner that the type of its
//     parameter owns, apart from a class that keeps its blocks and that no code can
//     move or assign (route.cpp, the owners and ends).
//
// Order
//     The walk reads the statements in their order.  A condition joins the state
//     of its branches, and a loop gets two passes, with the states of each break
//     and each continue.  A loan ends when it ends on one path.  A local reads as
//     moved, an owner as given back, and an owner as taken out of its object, when
//     that is so on each path.
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
//     place.  A door of a class with no row of the parts gives a loan of its whole
//     object, so a change of a different member of the same type is a finding.  A
//     door that ends a part by an index argument, such as FixedArray::set, ends the
//     whole object, because an index does not prove that the door changes only that
//     part.  A swap in a callee reads as
//     the end of the two owners, and an exchange as the end of the old owner, also
//     when a local keeps it.  An owner that leaves the block of a Box reads as ended,
//     and so does an owner that a match gives to an arm with a borrow of it.  Two owners
//     of one class in one object are one owner, so the one that leaves takes the loans
//     of the other one along.  A function whose parameter names an owner by a number
//     by an rvalue reference ends the number, also when it keeps the owner or gives it
//     back.  The plant plant-live-limits of utils/plants/view-escape/ pins these
//     findings.
//     A call that keeps a borrow in a different argument or in a member gives no
//     loan, and neither does a container of borrows.  A closure with a declared
//     result that holds a borrow gives its result the loans of its captures, and a
//     generic closure gives none.  A call that consumes an owner beside a plain
//     argument that borrows it ends the borrow when its body ends the owner, or
//     when its body moves or lends the owner and a closure of a body captures the
//     parameter of the borrow (the ends beside a borrow).  A capture names the
//     parameter by a reference or a copy, through a constructor or a local copy,
//     but a borrow that a different call makes from the parameter, as a window
//     of a View, names no parameter (fail open).  The record of a body holds no
//     order, so a read of the borrow before the end is a finding too (fail
//     closed, the plant plant-live-limits), and so is a capture by a closure that
//     runs before the move.  A closure that ends an owner whose storage holds the
//     object of a different argument, as a Box holds a keyed array, gives a
//     finding when the callee lends that object to the closure (the holders).  A
//     member function of that object counts as a lender, also when it only reads
//     the object, because the record does not show which member lends (fail
//     closed).
//     The product gives an arena and a pointer into it to one object (Vigil.h,
//     keep_load_), and its body keeps the owner.  The split mints take a Box and
//     the Ref of its object (SpscEnds.h).  Their bodies move the owner on, but no
//     closure captures the borrow, and a move of a Box keeps its block, so they
//     give no finding.
//     A callee with no body that takes a class which the unit never completes ends
//     only a loan whose route or targets hold that class.  So an end through such a
//     class, of an owner that it reaches by a path that the unit cannot see, is no
//     finding.  The control control-live-limits pins each form.
//     A move of a spent linear object ends a loan of its own bytes only when the
//     types of the loan meet the types in those bytes (names_own_bytes).  A scalar
//     member that is no array element is in no such set, because a count beside a
//     pointer would meet each borrow of the run behind the pointer.  So a borrow of
//     such a member through a door of its class stays past the move.  A borrow of an
//     erased type, as a void pointer, meets the bytes only through a run of one-byte
//     elements.
//     A loan that a call through a writing reference shares with its result also
//     stays on the object (share_with_result), because the walk does not see whether
//     the body moved the member out or made a new owner.  So a later change of the
//     emptied object ends the borrow too (fail closed).  A keyed holder has no door
//     that changes it after take(), because take() ends the key.

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

    // The part at the constant index INDEX of the object at PLACE, whose type is
    // TYPE (the parts).  A door with a constant index gives it.
    int part(int place, long index, tree type) {
        int const made = place == kNoPlace ? kNoPlace : intern(place, StepKind::element, NULL_TREE, index);
        if (made != kNoPlace && type != NULL_TREE) {
            part_types_[made] = type;
        }
        return made;
    }

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
            && matches(ancestor(longer, nodes_[shorter].depth), shorter);
    }

    // True when the places ONE and OTHER of one depth can name one object (the
    // parts).  A part matches an element of no index and a field of the same
    // object, because the walk does not map an index to a field.  Two parts match
    // only when their indices are the same.  Complexity: linear in the depth.
    bool matches(int one, int other) const {
        if (one == other) {
            return true;
        }
        if (one == kNoPlace || other == kNoPlace) {
            return false;
        }
        const Node& first = nodes_[one];
        const Node& second = nodes_[other];
        bool const is_first_part = first.kind == StepKind::element && first.index >= 0;
        bool const is_second_part = second.kind == StepKind::element && second.index >= 0;
        if (!is_first_part && !is_second_part) {
            return false;
        }
        bool const are_other_parts = is_first_part && is_second_part && first.index != second.index;
        bool const are_steps_of_one_kind =
            first.kind == second.kind || first.kind == StepKind::field || second.kind == StepKind::field;
        return !are_other_parts && are_steps_of_one_kind && matches(first.parent, second.parent);
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
        int const made = intern(rebase(node.parent, from, to), node.kind, node.decl, node.index);
        auto const part = part_types_.find(place);
        if (part != part_types_.end()) {
            part_types_[made] = part->second;
        }
        return made;
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
        if (node.kind == StepKind::element && node.index >= 0) {
            auto const found = part_types_.find(place);
            return found != part_types_.end() ? found->second : NULL_TREE;
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
                // A part has the spelling of its object, as the door names it.
                return node.index >= 0 ? text(node.parent) : text(node.parent) + "[]";
        }
        return "an object";
    }

private:
    struct Node {
        int parent = kNoPlace;
        StepKind kind = StepKind::root;
        tree decl = NULL_TREE;
        int depth = 0;
        long index = -1;  // the constant index of a part, or -1
    };

    int intern_step(int place, StepKind kind, tree decl) {
        return place == kNoPlace ? kNoPlace : intern(place, kind, decl);
    }

    // The key holds the DECL_UID of the step, or one more than the index of a part.
    int intern(int parent, StepKind kind, tree decl, long index = -1) {
        std::uint64_t const slot =
            decl != NULL_TREE ? std::uint64_t{DECL_UID(decl)} : static_cast<std::uint64_t>(index + 1);
        std::uint64_t const key =
            (static_cast<std::uint64_t>(parent + 1) << 34) | (slot << 2) | static_cast<std::uint64_t>(kind);
        auto [found, is_new] = index_.emplace(key, static_cast<int>(nodes_.size()));
        if (is_new) {
            nodes_.push_back(Node{parent, kind, decl, parent == kNoPlace ? 0 : nodes_[parent].depth + 1, index});
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
    std::unordered_map<int, tree> part_types_;  // the type of each part, by its place
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

// The classes with an end that a body ends, the classes with an end that it moves
// out of the object, the types that it lends to a closure under a unique borrow,
// and the calls that take an object.
struct Ends {
    TypeSet ended;
    TypeSet moved;
    TypeSet lent;
    bool is_lent = false;
    bool is_captured = false;  // a closure of the body captures the parameter (the ends beside a borrow)
    std::set<Taking> takings;

    void add(const Ends& other) {
        add_types(ended, other.ended);
        add_types(moved, other.moved);
        add_types(lent, other.lent);
        is_lent = is_lent || other.is_lent;
        is_captured = is_captured || other.is_captured;
        takings.insert(other.takings.begin(), other.takings.end());
    }

    bool operator==(const Ends& other) const {
        return ended == other.ended && moved == other.moved && lent == other.lent && is_lent == other.is_lent
            && is_captured == other.is_captured
            && !std::lexicographical_compare(takings.begin(), takings.end(), other.takings.begin(), other.takings.end())
            && !std::lexicographical_compare(other.takings.begin(), other.takings.end(), takings.begin(),
                                             takings.end());
    }
};

// The effect of a body on a parameter or on a capture (the effects).  INNER has the
// ends of an owner inside the object that the body does not give to its result.
struct Effect {
    Ends all;
    Ends inner;

    bool operator==(const Effect& other) const { return all == other.all && inner == other.inner; }
};

enum class EndKind : std::uint8_t {
    scope,
    assignment,
    move,
    call,
    number,  // a door that takes an owner of a resource by its number ends the number
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
    bool is_through = false;  // a closure holds the loan through a capture, so a move of an owner changes it
    TypeSet named;  // the types that the loan through a capture names, or none for the whole object
    Taking closure;  // the parameter that gets the closure of a loan through a capture
    bool is_plain = false;  // a plain borrow beside a writer, which a move changes only through a capture
    Taking borrow_taking;  // the parameter that gets the plain borrow
    bool is_holder = false;  // a closure ends an owner that holds the object that the callee lends
    bool is_receiver = false;  // the lent object is the object of a member function
    Taking lender;  // the parameter that gets the lent object
};

// What one reading of a body gives.
// A call whose two arguments name one object (the two names).
struct TwoNames {
    location_t location = UNKNOWN_LOCATION;
    std::string entity;
};

struct Reading {
    std::unordered_map<std::uint64_t, Effect> effects;
    std::vector<StaleUse> uses;
    std::vector<TwoNames> two_names;
};

// The effects of the unit, the functions with a body, the uses, and the owners of
// each type that a call takes, by its TYPE_UID.
std::unordered_map<std::uint64_t, Effect> unit_effects;
std::unordered_set<unsigned> read_functions;
// The deleted functions that a taking names.  A call of one is an error of its own,
// so it runs no code.
std::unordered_set<unsigned> deleted_functions;
std::vector<StaleUse> stale_uses;
std::unordered_map<unsigned, TypeSet> taken_owners;

// The end of the borrow of each finding, by the place of its use.
std::unordered_map<location_t, location_t> end_locations;

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
    bool is_erased = false;  // the borrow can name each object
    bool is_moved = false;  // a move took the owner to the place
    unsigned closure = 0;  // the closure and the field of a capture
    unsigned field = 0;
    TypeSet route;
    TypeSet direct;  // the owners of the route whose own pointers name a target
    TypeSet targets;
    TypeSet types;  // each type that the borrow names, or none for the whole object (route.cpp, the targets)

    auto key(location_t location) const {
        return std::make_tuple(place, named, static_cast<int>(source), is_unique, is_erased, is_moved, closure, field,
                               route, direct, targets, types, static_cast<unsigned>(location));
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
    std::set<int> taken_out;  // the places whose owner each path moved to a local
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

// The declaration of the most general template of FUNCTION, or FUNCTION when it
// is no instance of a template.
tree general_declaration(tree function) {
    if (DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE
        || TREE_CODE(DECL_TI_TEMPLATE(function)) != TEMPLATE_DECL) {
        return function;
    }
    tree const pattern = most_general_template(DECL_TI_TEMPLATE(function));
    return pattern != NULL_TREE && TREE_CODE(pattern) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(pattern) : function;
}

// True when TYPE, or the class template of which TYPE is an instance, states
// owns_by_number{}.  The type of a parameter of a template can be an instance
// that no code instantiated, so the marks of its pattern count.
bool names_number_owner(tree type) {
    type = element_class(type);
    if (type == NULL_TREE || !CLASS_TYPE_P(type)) {
        return false;
    }
    if (is_number_owner_class(type)) {
        return true;
    }
    tree const info = CLASSTYPE_TEMPLATE_INFO(type);
    if (info == NULL_TREE || TREE_CODE(TI_TEMPLATE(info)) != TEMPLATE_DECL) {
        return false;
    }
    tree const pattern = most_general_template(TI_TEMPLATE(info));
    return pattern != NULL_TREE && TREE_CODE(pattern) == TEMPLATE_DECL && is_number_owner_class(TREE_TYPE(pattern));
}

// True when the general declaration FUNCTION is a door of an owner of a resource
// by its number for the argument at INDEX (the numbers): a member function of
// the owner for its object, or a parameter whose own type is an rvalue reference
// to the owner.  A parameter of a generic type, such as T&& or a pack, forwards
// the value and is no such door.
bool is_number_door(tree function, std::size_t index) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    bool const has_object = DECL_IOBJ_MEMBER_FUNCTION_P(function);
    if (has_object && index == 0) {
        return DECL_CONTEXT(function) != NULL_TREE && names_number_owner(DECL_CONTEXT(function));
    }
    std::size_t position = has_object ? 1 : 0;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter), ++position) {
        if (DECL_PACK_P(parameter)) {
            return false;
        }
        if (position == index) {
            tree const type = TREE_TYPE(parameter);
            return type != NULL_TREE && TYPE_REF_P(type) && TYPE_REF_IS_RVALUE(type)
                && names_number_owner(TREE_TYPE(type));
        }
    }
    return false;
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

// ── The parts ──

// A door that gives a reference to one part of a product type at the constant
// index of its first template argument: the name of the door, and the namespace
// and the name of the class of its receiver (the parts).  A row is only for a
// product type, whose parts have storage of their own.  The alternatives of a sum
// type, such as a std::variant or a OneOf, share storage, so a door of a sum type
// is no row, and each door with no row gives a loan of its whole object.
struct PartDoor {
    const char* door = nullptr;
    const char* space = nullptr;  // std, or core for foundation::core
    const char* receiver = nullptr;
};

constexpr PartDoor kPartDoors[] = {
    {"get", "std", "tuple"},
    {"get", "std", "pair"},
    {"get", "std", "array"},
    {"at", "core", "Record"},
};

// True when the class TYPE has the name NAME in the namespace SPACE: std or a
// namespace inside it, or foundation::core.
bool is_class_of(tree type, const char* space, const char* name) {
    tree const decl = type != NULL_TREE && CLASS_TYPE_P(type) ? TYPE_NAME(TYPE_MAIN_VARIANT(type)) : NULL_TREE;
    if (decl == NULL_TREE || DECL_NAME(decl) == NULL_TREE
        || std::strcmp(IDENTIFIER_POINTER(DECL_NAME(decl)), name) != 0) {
        return false;
    }
    if (std::strcmp(space, "std") == 0) {
        return is_standard_class(type);
    }
    tree const core = CP_DECL_CONTEXT(decl);
    tree const foundation = core != NULL_TREE && TREE_CODE(core) == NAMESPACE_DECL ? CP_DECL_CONTEXT(core) : NULL_TREE;
    return foundation != NULL_TREE && TREE_CODE(foundation) == NAMESPACE_DECL && DECL_NAME(core) != NULL_TREE
        && std::strcmp(IDENTIFIER_POINTER(DECL_NAME(core)), space) == 0 && DECL_NAME(foundation) != NULL_TREE
        && std::strcmp(IDENTIFIER_POINTER(DECL_NAME(foundation)), "foundation") == 0
        && CP_DECL_CONTEXT(foundation) == global_namespace;
}

// The value of the first template argument of the instance FUNCTION of a function
// template, when it is a constant index, or -1.
long template_index_of(tree function) {
    if (DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE
        || TREE_CODE(DECL_TI_TEMPLATE(function)) != TEMPLATE_DECL || !PRIMARY_TEMPLATE_P(DECL_TI_TEMPLATE(function))) {
        return -1;
    }
    tree const arguments = INNERMOST_TEMPLATE_ARGS(DECL_TI_ARGS(function));
    tree const first =
        arguments != NULL_TREE && TREE_VEC_LENGTH(arguments) > 0 ? TREE_VEC_ELT(arguments, 0) : NULL_TREE;
    return first != NULL_TREE && TREE_CODE(first) == INTEGER_CST && tree_fits_shwi_p(first) && tree_to_shwi(first) >= 0
             ? static_cast<long>(tree_to_shwi(first))
             : -1;
}

// The constant index of the part that a call of CALLEE gives a reference to, when
// a row of kPartDoors names the door, or -1 (the parts).  The receiver is the
// object of the first parameter.
long part_index_of(tree callee) {
    tree const type = callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL ? TREE_TYPE(callee) : NULL_TREE;
    if (type == NULL_TREE || TREE_CODE(TREE_TYPE(type)) != REFERENCE_TYPE || TYPE_ARG_TYPES(type) == NULL_TREE
        || DECL_NAME(callee) == NULL_TREE) {
        return -1;
    }
    tree const first = TREE_VALUE(TYPE_ARG_TYPES(type));
    tree const receiver = first != NULL_TREE && is_indirect_type(first) ? element_class(TREE_TYPE(first)) : NULL_TREE;
    tree const scope = CP_DECL_CONTEXT(callee);
    bool const is_of_std = scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && DECL_NAMESPACE_STD_P(scope);
    for (const PartDoor& row : kPartDoors) {
        bool const is_std = std::strcmp(row.space, "std") == 0;
        bool const is_door = std::strcmp(IDENTIFIER_POINTER(DECL_NAME(callee)), row.door) == 0
                          && (is_std ? is_of_std
                                     : DECL_CONTEXT(callee) != NULL_TREE && TYPE_P(DECL_CONTEXT(callee))
                                           && TYPE_MAIN_VARIANT(DECL_CONTEXT(callee)) == receiver);
        if (is_door && is_class_of(receiver, row.space, row.receiver)) {
            return template_index_of(callee);
        }
    }
    return -1;
}

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
                // A trivial assignment from a temporary reads *&TARGET_EXPR: the value of
                // the temporary.
                if (TREE_CODE(node) == INDIRECT_REF) {
                    tree const address = strip_value(TREE_OPERAND(node, 0));
                    if (address != NULL_TREE && TREE_CODE(address) == ADDR_EXPR
                        && TREE_CODE(strip_value(TREE_OPERAND(address, 0))) == TARGET_EXPR) {
                        return loans_of_value(TREE_OPERAND(address, 0));
                    }
                }
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

    // The place of the parameter that VALUE names, or kNoPlace: the parameter itself,
    // its object by a reference, or a local that a copy of the parameter starts.
    int parameter_named_by(tree value) {
        tree const named = value != NULL_TREE ? strip_value(object_of(value)) : NULL_TREE;
        int const place = named != NULL_TREE && TREE_CODE(named) == PARM_DECL ? places_.root(named)
                        : value != NULL_TREE                                  ? place_of_pointee(value)
                                                                              : kNoPlace;
        tree const root = place != kNoPlace ? places_.root_decl(place) : named;
        if (root == NULL_TREE) {
            return kNoPlace;
        }
        if (TREE_CODE(root) == PARM_DECL) {
            return place;
        }
        auto copy = parameter_copies_.find(DECL_P(root) ? DECL_UID(root) : 0U);
        return copy != parameter_copies_.end() ? copy->second : kNoPlace;
    }

    // The place of the parameter that the capture value VALUE names, by a reference
    // or by a copy, directly, through a temporary, through a constructor or through
    // a local copy, or kNoPlace (the ends beside a borrow).
    int captured_parameter(tree value) {
        int place = parameter_named_by(value);
        tree init = strip_value(value);
        if (init != NULL_TREE && TREE_CODE(init) == TARGET_EXPR) {
            init = strip_value(TARGET_EXPR_INITIAL(init));
        }
        if (place == kNoPlace && init != NULL_TREE && init != strip_value(value)) {
            place = parameter_named_by(init);
        }
        if (place == kNoPlace && init != NULL_TREE
            && (TREE_CODE(init) == AGGR_INIT_EXPR || TREE_CODE(init) == CALL_EXPR)) {
            tree const constructor = cp_get_callee_fndecl_nofold(init);
            // The first argument of a constructor is the object that it builds, also
            // in an AGGR_INIT_EXPR.
            if (constructor != NULL_TREE && DECL_CONSTRUCTOR_P(constructor)) {
                for (int index = 1; index < argument_count(init) && place == kNoPlace; ++index) {
                    place = captured_parameter(argument_at(init, index));
                }
            }
        }
        return place;
    }

    // The loans of the value VALUE of the member FIELD of an object.  CLOSURE is
    // the class of a closure, or null.  A capture of a parameter gives the parameter
    // the effect is_captured.
    std::vector<int> loans_of_member(tree field, tree value, tree closure) {
        bool const is_field = field != NULL_TREE && TREE_CODE(field) == FIELD_DECL;
        unsigned const closure_id = closure != NULL_TREE ? TYPE_UID(TYPE_MAIN_VARIANT(closure)) : 0U;
        std::vector<int> found;
        if (is_field && closure_id != 0) {
            int const parameter = captured_parameter(value);
            if (parameter != kNoPlace) {
                visit_effects(parameter, [](Effect& effect) {
                    effect.all.is_captured = true;
                    effect.inner.is_captured = true;
                });
            }
        }
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
        long const part = part_index_of(callee);
        for (int index = 0; index < argument_count(call); ++index) {
            tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
            parameter = parameter != NULL_TREE ? TREE_CHAIN(parameter) : NULL_TREE;
            if (is_constructor && index == 0) {
                continue;
            }
            add_indices(found, loans_of_argument(argument_at(call, index), parameter_type, result,
                                                 index == 0 && type != NULL_TREE && TREE_CODE(type) == METHOD_TYPE
                                                     && FUNCTION_RVALUE_QUALIFIED(type),
                                                 index == 0 ? part : -1));
        }
        return found;
    }

    // The loans that the result RESULT of a call gets from ARGUMENT.  IS_CONSUMED
    // tells that the member consumes its object.  PART is the constant index of the
    // part of the object that a door of a part gives, or -1 (the parts).
    std::vector<int> loans_of_argument(tree argument, tree parameter, tree result, bool is_consumed, long part) {
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
        int place = is_into ? place_of_pointee(argument) : kNoPlace;
        if (place != kNoPlace && part >= 0) {
            place = places_.part(place, part, TREE_TYPE(result));
        }
        if (place != kNoPlace) {
            Loan loan;
            loan.place = place;
            loan.named = place;
            loan.source = LoanSource::call;
            loan.is_unique = !is_const_target(parameter);
            loan.is_erased = copy.is_erased;
            loan.route = copy.route;
            loan.direct = copy.direct;
            loan.targets = copy.targets;
            loan.types = types_named_by(result);
            found.push_back(add_loan(std::move(loan)));
        }
        for (int source : loans_at(argument)) {
            Loan derived = loans_[source];
            if (is_into) {
                add_types(derived.route, copy.route);
                derived.is_erased = copy.is_erased;
                derived.direct = copy.direct;
                derived.targets = copy.targets;
                derived.types = types_named_by(result);
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

    // True when an owner came out of PLACE on each path.
    bool is_taken_out(int place) const {
        return std::any_of(state_.taken_out.begin(), state_.taken_out.end(),
                           [&](int taken) { return places_.overlaps(taken, place); });
    }

    // The body ends the classes OWNERS at PLACE.  An owner that came out of the place
    // does not end there.
    void note_ended(int place, const TypeSet& owners) {
        if (!owners.empty()) {
            bool const is_inner = !is_taken_out(place);
            visit_effects(place, [&](Effect& effect) {
                add_types(effect.all.ended, owners);
                if (is_inner) {
                    add_types(effect.inner.ended, owners);
                }
            });
        }
    }

    void note_ended(int place, tree type) { note_ended(place, ended_parts(type)); }

    // The body lends the object at PLACE to the call operator of a closure, by a
    // parameter of the type LENT (the borrows through a capture).
    void note_lent(int place, tree lent) {
        const TypeSet& types = types_named_by(lent);
        visit_effects(place, [&](Effect& effect) {
            for (Ends* ends : {&effect.all, &effect.inner}) {
                ends->is_lent = true;
                add_types(ends->lent, types);
            }
        });
    }

    // True when PLACE is a member or an element of the object of its root, and not
    // that object itself.  A capture of a closure names the captured object, so it
    // is no member.
    bool is_part(int place) const {
        for (; place != kNoPlace; place = places_.parent(place)) {
            StepKind const kind = places_.kind(place);
            if (kind == StepKind::element || (kind == StepKind::field && !is_capture_field(places_.decl(place)))) {
                return true;
            }
        }
        return false;
    }

    static bool is_capture_field(tree field) {
        tree context = DECL_CONTEXT(field);
        return context != NULL_TREE && TYPE_P(context) && LAMBDA_TYPE_P(context);
    }

    // The body moves the owner of TYPE at PLACE out of its object, into a call or a
    // local.  The body can give the owner back, so the move ends nothing (the borrows
    // through a capture).
    void note_moved_out(int place, tree type) {
        const TypeSet& parts = ended_parts(type);
        if (parts.empty()) {
            return;
        }
        visit_effects(place, [&](Effect& effect) {
            add_types(effect.all.moved, parts);
            add_types(effect.inner.moved, parts);
        });
    }

    // The owner at PLACE ends with a local that holds it.
    void note_dropped(int place, tree type) {
        const TypeSet& parts = ended_parts(type);
        visit_effects(place, [&](Effect& effect) {
            add_types(effect.all.ended, parts);
            add_types(effect.inner.ended, parts);
        });
    }

    // The call of TAKING takes the object at PLACE.  With GIVES_INNER, its result can
    // get an owner inside the object.
    void note_taking(int place, const Taking& taking, bool gives_inner = false) {
        bool const is_inner = !is_taken_out(place);
        Taking inner = taking;
        inner.is_inner = gives_inner;
        visit_effects(place, [&](Effect& effect) {
            effect.all.takings.insert(taking);
            if (is_inner) {
                effect.inner.takings.insert(inner);
            }
        });
    }

    // True for a parameter that the caller gives by a move.
    bool is_given_parameter(int place) const {
        tree decl = places_.root_decl(place);
        return TREE_CODE(decl) == PARM_DECL
            && (!is_indirect_type(TREE_TYPE(decl)) || is_rvalue_reference(TREE_TYPE(decl)));
    }

    // The local ROOT gets the owner at PLACE by a move.  An object behind a pointer
    // is no local: the storage that the pointer names keeps the owner, and no end
    // of the frame ends it.
    void hold_owner(int root, int place, tree type) {
        if (root != kNoPlace && places_.has_dereference(root)) {
            if (parameter_index(place) >= 0) {
                state_.taken_out.insert(place);
            } else {
                state_.held.erase(root_id(place));
            }
            return;
        }
        if (root == kNoPlace || parameter_index(place) < 0) {
            move_held(place, root);
            return;
        }
        if (parameter_index(root) >= 0) {
            return;
        }
        state_.taken_out.insert(place);
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
        state_.taken_out.erase(place);
        return true;
    }

    // True when LOCAL is the result, and it holds an owner that came out of OWNER.
    bool is_given_back(int local, const HeldOwner& owner) const {
        tree result = DECL_RESULT(function_);
        return result != NULL_TREE && local == static_cast<int>(DECL_UID(result))
            && can_hold_owner(TREE_TYPE(result), ended_parts(owner.type));
    }

    // The body ends here: each owner of the caller that a local still holds, and
    // each parameter by value that the path did not move away, ends with it.
    void leave() {
        tree result = TREE_TYPE(TREE_TYPE(function_));
        for (const auto& [local, held] : state_.held) {
            for (const HeldOwner& owner : held) {
                if (!can_hold_object(result, owner.type) && !is_given_back(local, owner)) {
                    note_dropped(owner.place, owner.type);
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
    // writes through them.  RESULT is the type of the result that can get an owner
    // inside the object, or null.
    void end_loans(int place, const End& end, tree type, tree through, tree result = NULL_TREE) {
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
            made.taking.is_inner = is_inner_loan(loans_[index], result);
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
            case EndKind::number:
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

    // True when an owner inside the object of LOAN has its targets, and a value of
    // RESULT can hold that owner.  A null RESULT holds none.
    static bool is_inner_loan(const Loan& loan, tree result) {
        return result != NULL_TREE && !loan.is_erased && can_hold_owner(result, loan.route)
            && can_hold_each_owner(result, loan.direct);
    }

    // True when LOAN, with no owner of the moved object in its route, names the own
    // bytes of the object at FROM: no dereference step leads from FROM to its place,
    // and its types are none or meet the types in the bytes of its place.  So a Ref
    // of the object, a View of an inline FixedArray member and a Ref of a member name
    // them.  A View of a run that a pointer member names, as an arena run, meets no
    // such type.  A place of no known type, and bytes with a run of one-byte elements,
    // which can hold an object of each type, name them (fail closed).  A borrow of an
    // erased type, as a void pointer, meets other bytes only through such a run.
    bool names_own_bytes(int from, const Loan& loan) const {
        for (int place = loan.place; place != from; place = places_.parent(place)) {
            if (place == kNoPlace || places_.kind(place) == StepKind::dereference) {
                return false;
            }
        }
        tree const type = places_.type(loan.place);
        if (loan.types.empty() || type == NULL_TREE) {
            return true;
        }
        const TypeSet& own = inline_types(type);
        bool const holds_byte_run = std::binary_search(own.begin(), own.end(), ~0U);
        return holds_byte_run || have_common_type(loan.types, own);
    }

    // The loans of FROM go to TO with the owner that the route names (the
    // transfer).  A move ends a loan of a part of the moved object.  A move of an
    // object of a class that states linear{} leaves it spent, so it also ends a loan
    // of its own bytes (names_own_bytes): the object itself, as the Ref that to()
    // gives, and a member or an inline array in it.  A class that only holds a
    // linear value, and a sum such as an Option, stay usable after the move of the
    // payload (states_linear of linear.cpp).
    // With RESULT, only an owner inside the object goes into the value of RESULT at TO.
    void transfer_loans(int from, int to, tree type, tree result = NULL_TREE) {
        if (!state_.is_reachable || from == kNoPlace) {
            return;
        }
        const TypeSet& parts = ended_parts(type);
        tree object = element_class(type);
        unsigned const object_id = object != NULL_TREE ? TYPE_UID(object) : 0U;
        // The moved value is the object at FROM itself, and not a payload that a
        // consume moves out of it.
        tree const moved_class = element_class(places_.type(from));
        bool const is_linear_object =
            object != NULL_TREE && CLASS_TYPE_P(object) && moved_class == object && states_linear(object);
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
                if (names_part || (is_linear_object && names_own_bytes(from, loans_[index]))) {
                    end_loan(loan, make_end(EndKind::move, from));
                }
                continue;
            }
            Loan made = loans_[index];
            if (result != NULL_TREE) {
                made.place = is_inner_loan(made, result) ? to : kNoPlace;
            } else {
                made.place = to == kNoPlace ? kNoPlace : places_.rebase(made.place, from, to);
            }
            made.is_moved = true;
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

    // The temporary that initializes the object at TO is that object, so it does not
    // end with its full expression.
    void transfer_temporary(tree value, int to) {
        value = strip_value(value);
        if (value == NULL_TREE) {
            return;
        }
        if (TREE_CODE(value) == COND_EXPR) {
            transfer_temporary(TREE_OPERAND(value, 1), to);
            transfer_temporary(TREE_OPERAND(value, 2), to);
            return;
        }
        if (TREE_CODE(value) == COMPOUND_EXPR) {
            transfer_temporary(TREE_OPERAND(value, 1), to);
            return;
        }
        tree slot = TREE_CODE(value) == TARGET_EXPR    ? TARGET_EXPR_SLOT(value)
                  : TREE_CODE(value) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_SLOT(value)
                                                       : NULL_TREE;
        if (slot == NULL_TREE || !DECL_P(slot)) {
            return;
        }
        temporaries_.erase(std::remove(temporaries_.begin(), temporaries_.end(), slot), temporaries_.end());
        int const from = places_.root(slot);
        if (to == kNoPlace || from == to) {
            return;
        }
        transfer_loans(from, to, TREE_TYPE(slot));
        move_held(from, to);
    }

    // The temporaries of a full expression end with it.  Only a loan of an owner that
    // a move took into a temporary ends here.
    void end_temporaries() {
        for (tree temporary : temporaries_) {
            int const place = places_.root(temporary);
            for (std::size_t index = 0; state_.is_reachable && index < loans_.size(); ++index) {
                if (loans_[index].is_moved && places_.holds(place, loans_[index].place)) {
                    end_loan(static_cast<int>(index), make_end(EndKind::scope, place));
                }
            }
        }
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
            case EndKind::number:
                return (DECL_CONSTRUCTOR_P(end.callee) ? "a constructor of " + type_text(DECL_CONTEXT(end.callee))
                                                       : "a call of " + qualified_name(end.callee))
                     + " with " + places_.text(end.place) + ", which ends the number of the resource that it owns";
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
        keep_common(into.moved, other.moved);
        keep_common(into.taken_out, other.taken_out);
    }

    static void keep_common(std::set<int>& into, const std::set<int>& other) {
        for (auto one = into.begin(); one != into.end();) {
            one = other.count(*one) != 0 ? std::next(one) : into.erase(one);
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
        int const copied = captured_parameter(value);
        if (copied != kNoPlace) {
            parameter_copies_[DECL_UID(decl)] = copied;
        }
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
                if (TARGET_EXPR_SLOT(node) != NULL_TREE && DECL_P(TARGET_EXPR_SLOT(node))) {
                    transfer_temporary(TARGET_EXPR_INITIAL(node), places_.root(TARGET_EXPR_SLOT(node)));
                    if (has_end(TREE_TYPE(TARGET_EXPR_SLOT(node)))) {
                        temporaries_.push_back(TARGET_EXPR_SLOT(node));
                    }
                }
                return;
            case CLEANUP_POINT_EXPR: {
                std::vector<tree> outer = std::exchange(temporaries_, {});
                walk_expression(TREE_OPERAND(node, 0));
                end_temporaries();
                temporaries_ = std::move(outer);
                return;
            }
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
                tree field = NULL_TREE;
                tree value = NULL_TREE;
                FOR_EACH_CONSTRUCTOR_ELT(CONSTRUCTOR_ELTS(node), position, field, value) {
                    walk_expression(value);
                    bool const is_member =
                        slot != NULL_TREE && DECL_P(slot) && field != NULL_TREE && TREE_CODE(field) == FIELD_DECL;
                    transfer_temporary(value, is_member ? places_.field(places_.root(slot), field) : kNoPlace);
                }
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
        note_ended(place, owners);
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
        check_ends_beside(callee, arguments);
        check_two_names(callee, arguments);
        end_numbers(callee, arguments,
                    (is_constructor || is_assignment) && is_number_owner_class(DECL_CONTEXT(callee)));
        CallFacts facts;
        facts.callee = callee;
        facts.result = is_constructor ? DECL_CONTEXT(callee) : TREE_TYPE(type);
        facts.built = built;
        facts.result_place = slot != NULL_TREE && DECL_P(slot) ? places_.root(slot) : kNoPlace;
        facts.is_constructor = is_constructor;
        facts.is_assignment = is_assignment;
        facts.is_body_call = callee != NULL_TREE && LAMBDA_FUNCTION_P(callee) && !arguments.empty()
                          && !arguments[0].places.empty() && parameter_index(arguments[0].places[0]) >= 0;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            take_argument(facts, arguments, index);
        }
        // An argument that keeps a moved object gets its loans after the call takes
        // the argument itself, so a change of its old value ends none of them.
        for (const KeptObject& kept : std::exchange(kept_objects_, {})) {
            transfer_loans(kept.from, kept.to, kept.type);
            note_kept(kept.from, kept.to, kept.type);
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
        bool is_body_call = false;  // a call of a closure that the function got by a parameter or a capture
    };

    // ── The ends beside a borrow ──

    // An object that one argument of a call binds: its place, the loan that gives
    // it, and whether the callee can write or end it through this argument.
    struct Binding {
        int place = kNoPlace;
        int loan = -1;
        bool is_write = false;
        bool is_capture = false;
    };

    // The source of the value that a move constructor builds for a parameter by
    // value, or kNoPlace.
    int moved_source(tree node) {
        tree value = strip_value(value_of_argument(node));
        if (value != NULL_TREE && TREE_CODE(value) == TARGET_EXPR) {
            value = strip_value(TARGET_EXPR_INITIAL(value));
        }
        if (value == NULL_TREE || (TREE_CODE(value) != AGGR_INIT_EXPR && TREE_CODE(value) != CALL_EXPR)) {
            return kNoPlace;
        }
        tree constructor = cp_get_callee_fndecl_nofold(value);
        if (constructor == NULL_TREE || !DECL_CONSTRUCTOR_P(constructor) || argument_count(value) != 2) {
            return kNoPlace;
        }
        tree source = argument_at(value, 1);
        tree type = TREE_TYPE(source);
        if (type == NULL_TREE || !is_rvalue_reference(type)) {
            return kNoPlace;
        }
        return place_of_pointee(source);
    }

    // The objects that ARGUMENT binds.
    void add_bindings(const Argument& argument, std::vector<Binding>& out) {
        if (argument.parameter == NULL_TREE) {
            return;
        }
        if (argument.is_closure) {
            for (int loan : argument.loans) {
                if (loans_[loan].place != kNoPlace && loans_[loan].source == LoanSource::capture) {
                    out.push_back(Binding{loans_[loan].place, loan, loans_[loan].is_unique, true});
                }
            }
            return;
        }
        if (is_indirect_type(argument.parameter)) {
            for (int place : argument.places) {
                out.push_back(Binding{place, -1, argument.is_write, false});
            }
            for (int loan : argument.loans) {
                if (loans_[loan].place != kNoPlace) {
                    out.push_back(Binding{loans_[loan].place, loan, argument.is_write, false});
                }
            }
            return;
        }
        if (has_end(argument.parameter)) {
            int const source = moved_source(argument.node);
            if (source != kNoPlace) {
                out.push_back(Binding{source, -1, true, false});
            }
        }
        for (int loan : argument.loans) {
            if (loans_[loan].place != kNoPlace) {
                out.push_back(Binding{loans_[loan].place, loan, loans_[loan].is_unique, false});
            }
        }
    }

    // The callee can use the borrow BORROW of the argument at BORROW_INDEX after the
    // argument at WRITER_INDEX ends its object.  The use is at the call.  A plain
    // borrow gets the judge of a borrow through a capture too: a move of an owner of
    // the borrow changes it when the callee captures the parameter of the borrow in
    // a closure, which can read it while the owner is away.
    void note_end_beside(const Binding& borrow, const Argument& borrow_argument, std::size_t borrow_index,
                         const Binding& writer, const Argument& writer_argument, std::size_t writer_index,
                         tree callee) {
        End end = make_end(EndKind::call, writer.place);
        end.callee = callee;
        if (writer.is_capture) {
            end.is_closure = true;
            end.taking = taking_of_capture(loans_[writer.loan].closure, loans_[writer.loan].field);
        } else {
            end.taking = taking_of_parameter(callee, writer_index, writer_argument.held);
        }
        tree const type = writer.is_capture ? places_.type(writer.place) : writer_argument.held;
        is_rvalue_end_ = writer_argument.is_rvalue && !writer.is_capture;
        bool const is_ended = ends_loan(loans_[borrow.loan], writer.place, end, type);
        is_rvalue_end_ = false;
        if (!is_ended) {
            return;
        }
        tree const named = strip_value(object_of(borrow_argument.node));
        bool const is_named = named != NULL_TREE && DECL_P(named) && DECL_NAME(named) != NULL_TREE;
        std::size_t const recorded = reading_.uses.size();
        record_use(is_named ? identifier_text(DECL_NAME(named)) : std::string("an argument of the call"), borrow.loan,
                   {add_end(end)});
        if (reading_.uses.size() != recorded) {
            StaleUse& use = reading_.uses.back();
            use.is_through = true;
            use.is_plain = true;
            use.named = loans_[borrow.loan].types;
            use.borrow_taking = taking_of_parameter(callee, borrow_index, borrow_argument.held);
        }
    }

    // A call that gets a borrow in one argument, and a different argument that can
    // end the borrowed object (the ends beside a borrow).  Complexity: the square of
    // the bindings of the call.
    void check_ends_beside(tree callee, const std::vector<Argument>& arguments) {
        if (!reads_uses_ || !state_.is_reachable || arguments.size() < 2) {
            return;
        }
        std::vector<std::vector<Binding>> bindings(arguments.size());
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            add_bindings(arguments[index], bindings[index]);
        }
        for (std::size_t lender = 0; lender < arguments.size(); ++lender) {
            for (std::size_t writer = 0; writer < arguments.size(); ++writer) {
                if (lender == writer) {
                    continue;
                }
                for (const Binding& borrow : bindings[lender]) {
                    for (const Binding& end : bindings[writer]) {
                        if (borrow.loan >= 0 && !borrow.is_write && end.is_write
                            && places_.overlaps(loans_[borrow.loan].place, end.place)) {
                            note_end_beside(borrow, arguments[lender], lender, end, arguments[writer], writer, callee);
                        }
                    }
                }
                check_borrows_through(arguments[lender], lender, bindings[writer], arguments[writer], writer, callee);
                check_holders(arguments[lender], arguments[writer], writer, callee);
            }
        }
    }

    // The closure CLOSURE captures by a unique reference an owner whose storage holds
    // the object of the argument OTHER at OTHER_INDEX, as a Box holds its payload (the
    // holders).  The callee can lend that object to the closure, and the closure can
    // end the owner, also when the object has no end of its own.  The use is a record,
    // and the end of the unit judges it when the callee lends the object: a member
    // function lends its own object, and a different callee lends a parameter whose
    // ends show a loan to a closure (is_lent).  A member function that only reads its
    // object gives a finding too (fail closed), because the record cannot see which
    // member lends.  Complexity: the loans of the closure times the loans of OTHER.
    void check_holders(const Argument& closure, const Argument& other, std::size_t other_index, tree callee) {
        if (!closure.is_closure || other.is_closure) {
            return;
        }
        unsigned const own = closure_id_of(closure);
        for (int held : closure.loans) {
            const Loan& capture = loans_[held];
            if (capture.source != LoanSource::capture || capture.closure != own || capture.place == kNoPlace
                || !capture.is_unique) {
                continue;
            }
            for (int lent : other.loans) {
                // The route of the loan of the argument holds an owner, and the closure
                // captures the place of that owner, so the owner keeps the storage of
                // the object of the argument.
                const Loan& loan = loans_[lent];
                if (loan.place == kNoPlace || loan.route.empty() || !places_.overlaps(capture.place, loan.place)) {
                    continue;
                }
                int const place = loan.named != kNoPlace ? loan.named : loan.place;
                End end = make_end(EndKind::call, capture.place);
                end.callee = callee;
                end.is_closure = true;
                end.taking = taking_of_capture(capture.closure, capture.field);
                end.types = loan.route;
                StaleUse use;
                use.location = location_;
                use.borrow = "borrow of " + places_.text(place) + " in a closure of "
                           + (callee != NULL_TREE ? qualified_name(callee) : std::string("a call"));
                use.ends.push_back(JudgedEnd{end.kind, end.location, end_text(end), end.taking, end.types});
                use.is_holder = true;
                use.lender = taking_of_parameter(callee, other_index, other.held);
                use.is_receiver = other_index == 0 && callee != NULL_TREE && DECL_LANG_SPECIFIC(callee) != nullptr
                               && DECL_OBJECT_MEMBER_FUNCTION_P(callee);
                reading_.uses.push_back(std::move(use));
            }
        }
    }

    // The closure CLOSURE holds a loan through its captures, and the argument at
    // WRITER_INDEX gives the object of the loan (the borrows through a capture).
    // Complexity: the loans of the closure times the bindings of the writer.
    void check_borrows_through(const Argument& closure, std::size_t closure_index, const std::vector<Binding>& writers,
                               const Argument& writer_argument, std::size_t writer_index, tree callee) {
        if (!closure.is_closure) {
            return;
        }
        unsigned const own = closure_id_of(closure);
        Taking const closure_taking = taking_of_parameter(callee, closure_index, closure.held);
        for (int held : closure.loans) {
            bool const is_own_capture = loans_[held].source == LoanSource::capture && loans_[held].closure == own;
            if (loans_[held].place == kNoPlace || is_own_capture) {
                continue;
            }
            for (const Binding& writer : writers) {
                if (writer.is_write && places_.overlaps(loans_[held].place, writer.place)) {
                    note_end_through(held, closure, closure_taking, writer, writer_argument, writer_index, callee);
                }
            }
        }
    }

    // The TYPE_UID of the class of the closure that ARGUMENT gives, or 0.
    static unsigned closure_id_of(const Argument& argument) {
        tree object = object_of(argument.node);
        tree type = object != NULL_TREE ? TREE_TYPE(object) : NULL_TREE;
        if (is_indirect_type(type)) {
            type = TREE_TYPE(type);
        }
        type = element_class(type);
        return type != NULL_TREE && is_closure_type(type) ? TYPE_UID(TYPE_MAIN_VARIANT(type)) : 0U;
    }

    // The name of the local that the closure CLOSURE captures and that holds LOAN,
    // or the name of a copy in the closure.
    std::string holder_of(const Argument& closure, int loan) {
        for (int capture : closure.loans) {
            const Loan& known = loans_[capture];
            if (known.source != LoanSource::capture || known.place == kNoPlace || places_.depth(known.place) != 0) {
                continue;
            }
            auto found = state_.loans.find(root_id(known.place));
            if (found != state_.loans.end()
                && std::find(found->second.begin(), found->second.end(), loan) != found->second.end()) {
                return places_.text(known.place);
            }
        }
        return "a closure argument";
    }

    // The callee can end the object of the loan HELD through the argument at
    // WRITER_INDEX, or move an owner of it away, and then run the closure CLOSURE,
    // which holds the loan.  The use is at the call.
    void note_end_through(int held, const Argument& closure, const Taking& closure_taking, const Binding& writer,
                          const Argument& writer_argument, std::size_t writer_index, tree callee) {
        End end = make_end(EndKind::call, writer.place);
        end.callee = callee;
        if (writer.is_capture) {
            end.is_closure = true;
            end.taking = taking_of_capture(loans_[writer.loan].closure, loans_[writer.loan].field);
        } else {
            end.taking = taking_of_parameter(callee, writer_index, writer_argument.held);
        }
        tree const type = writer.is_capture ? places_.type(writer.place) : writer_argument.held;
        is_rvalue_end_ = writer_argument.is_rvalue && !writer.is_capture;
        // With no type to end, the use stays for a lend of the object.
        static_cast<void>(ends_loan(loans_[held], writer.place, end, type));
        is_rvalue_end_ = false;
        std::size_t const before = reading_.uses.size();
        record_use(holder_of(closure, held), held, {add_end(end)});
        if (reading_.uses.size() != before) {
            StaleUse& use = reading_.uses.back();
            use.is_through = true;
            use.named = loans_[held].types;
            use.closure = closure_taking;
        }
    }

    // ── The two names ──

    // The object that a trivial move into a parameter by value copies: the front
    // end gives the object itself, with no constructor.  For an empty class it gives
    // the source and then an empty value, in a COMPOUND_EXPR.  Null for a value
    // that names no object.
    static tree trivially_moved_object(tree node) {
        node = strip_value(node);
        if (node != NULL_TREE && TREE_CODE(node) == COMPOUND_EXPR) {
            node = strip_value(TREE_OPERAND(node, 0));
        }
        if (node == NULL_TREE) {
            return NULL_TREE;
        }
        switch (TREE_CODE(node)) {
            case VAR_DECL:
            case PARM_DECL:
            case COMPONENT_REF:
            case ARRAY_REF:
            case INDIRECT_REF:
                return node;
            default:
                return NULL_TREE;
        }
    }

    // The object that ARGUMENT names itself, not through a loan: the object of a
    // reference or of a pointer, or the source of a move into a parameter by value.
    // A linear class and a unique borrow class have no copy, so a parameter by value
    // gets a named object of such a class by a move.  kNoPlace for a closure and for
    // an argument that names no such object.
    // The part that a call of a door of a part at NODE gives, with the type of the
    // part, or kNoPlace (the parts).
    int door_part(tree node) {
        node = strip_value(node);
        if (node == NULL_TREE || TREE_CODE(node) != CALL_EXPR || call_expr_nargs(node) < 1) {
            return kNoPlace;
        }
        tree const callee = cp_get_callee_fndecl_nofold(node);
        long const index = part_index_of(callee);
        return index >= 0 ? places_.part(place_of_pointee(CALL_EXPR_ARG(node, 0)), index,
                                         TREE_TYPE(TREE_TYPE(TREE_TYPE(callee))))
                          : kNoPlace;
    }

    int named_place(const Argument& argument) {
        if (argument.parameter == NULL_TREE || argument.is_closure) {
            return kNoPlace;
        }
        if (is_indirect_type(argument.parameter)) {
            int const place = place_of_pointee(argument.node);
            return place != kNoPlace ? place : door_part(argument.node);
        }
        int const source = moved_source(argument.node);
        if (source != kNoPlace) {
            return source;
        }
        tree const object = trivially_moved_object(argument.node);
        return object != NULL_TREE ? place_of_lvalue(object) : kNoPlace;
    }

    // True when ARGUMENT can write or move its object.  A parameter by value with
    // a named object gets it by a move.
    static bool is_writing(const Argument& argument) {
        return !is_indirect_type(argument.parameter) || argument.is_write;
    }

    // The class of the object that two overlapping places ONE and OTHER name, when
    // it is a linear class or a unique borrow class, or null.  IS_LINEAR tells which.
    tree two_names_class(int one, int other, bool& is_linear) const {
        tree const object = element_class(places_.type(places_.holds(one, other) ? other : one));
        is_linear = object != NULL_TREE && CLASS_TYPE_P(object) && is_linear_class(object);
        return object != NULL_TREE && (is_linear || is_unique_borrow_class(object)) ? object : NULL_TREE;
    }

    // Records the two names of the call at the place PLACE of the object OBJECT.
    void add_two_names(tree callee, std::string const& names, int place, tree object, bool is_linear) {
        std::string const callee_text =
            callee != NULL_TREE ? qualified_name(callee) : std::string("a function pointer");
        reading_.two_names.push_back(TwoNames{location_, names + " of " + callee_text + " name " + places_.text(place)
                                                             + ", " + (is_linear ? "a linear " : "a unique borrow ")
                                                             + type_text(object)});
    }

    // A call whose two arguments name one object of a linear class or of a unique
    // borrow class, and one of them can write or move it (the two names).  One
    // argument names the object itself, and the other one names it itself too, or
    // gives a loan or a capture of it.  A door of a part names its part (the parts),
    // so one part two times is two names, and two parts of one object are none.  Two
    // other loans of one object, from a door or from a moved object, are no second
    // name.  An assignment whose two sides name one
    // object is the kind self-move of the rule must-consume, and a loan that a move
    // of the target of an assignment gives is no second name: the target holds no
    // value until the assignment.  A body of a door with the annotation
    // two_names_door of foundation/Linear.h lends its object under two names on
    // purpose.  Complexity: the square of the arguments of the call, times their
    // loans.
    void check_two_names(tree callee, const std::vector<Argument>& arguments) {
        if (!reads_uses_ || !state_.is_reachable || arguments.size() < 2 || is_two_names_door(function_)) {
            return;
        }
        bool const is_assignment = callee != NULL_TREE && DECL_ASSIGNMENT_OPERATOR_P(callee);
        std::vector<int> places(arguments.size(), kNoPlace);
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            places[index] = named_place(arguments[index]);
        }
        for (std::size_t first = 0; first < arguments.size() && !is_assignment; ++first) {
            for (std::size_t second = first + 1; second < arguments.size(); ++second) {
                if (places[first] == kNoPlace || places[second] == kNoPlace
                    || !places_.overlaps(places[first], places[second])
                    || !(is_writing(arguments[first]) || is_writing(arguments[second]))) {
                    continue;
                }
                bool is_linear = false;
                tree const object = two_names_class(places[first], places[second], is_linear);
                if (object != NULL_TREE) {
                    int const inner = places_.holds(places[first], places[second]) ? places[second] : places[first];
                    add_two_names(callee,
                                  "argument " + std::to_string(first) + " and argument " + std::to_string(second),
                                  inner, object, is_linear);
                    return;
                }
            }
        }
        for (std::size_t named = 0; named < arguments.size(); ++named) {
            if (places[named] == kNoPlace) {
                continue;
            }
            for (std::size_t lending = 0; lending < arguments.size(); ++lending) {
                if (lending == named) {
                    continue;
                }
                std::vector<Binding> bindings;
                add_bindings(arguments[lending], bindings);
                for (const Binding& binding : bindings) {
                    if (binding.loan < 0 || !places_.overlaps(places[named], binding.place)
                        || !(is_writing(arguments[named]) || binding.is_write)) {
                        continue;
                    }
                    bool const is_moved_target = is_assignment && named == 0 && loans_[binding.loan].is_moved;
                    bool is_linear = false;
                    tree const object = two_names_class(places[named], binding.place, is_linear);
                    if (is_moved_target || object == NULL_TREE) {
                        continue;
                    }
                    int const inner = places_.holds(places[named], binding.place) ? binding.place : places[named];
                    add_two_names(callee,
                                  "argument " + std::to_string(named) + " and "
                                      + (binding.is_capture ? "a capture of argument " : "a loan of argument ")
                                      + std::to_string(lending),
                                  inner, object, is_linear);
                    return;
                }
            }
        }
    }

    // A door of an owner of a resource by its number that takes the owner by an
    // rvalue reference ends each loan of the number (the numbers), such as depart()
    // of a Handle.  A constructor and an assignment of the owner itself move the
    // number, so its loans stay with the source, as a plain move keeps them.
    void end_numbers(tree callee, const std::vector<Argument>& arguments, bool is_own_move) {
        if (callee == NULL_TREE || is_own_move) {
            return;
        }
        tree const general = general_declaration(callee);
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const Argument& argument = arguments[index];
            if (argument.node == NULL_TREE || !argument.is_rvalue || !is_indirect_type(argument.parameter)
                || !is_number_owner_class(element_class(argument.held)) || !is_number_door(general, index)) {
                continue;
            }
            int const place = place_of_pointee(argument.node);
            if (place != kNoPlace) {
                End end = make_end(EndKind::number, place);
                end.callee = callee;
                end_loans(place, end, argument.held, NULL_TREE);
            }
        }
    }

    // The body of the call can end the object at PLACE that ARGUMENT gives.  With
    // RESULT, the result of the call is a value of RESULT that can get an owner inside
    // the object.
    void end_by_call(const CallFacts& facts, const Argument& argument, std::size_t index, int place,
                     tree result = NULL_TREE) {
        End end = make_end(EndKind::call, place);
        end.callee = facts.callee;
        end.taking = taking_of_parameter(facts.callee, index, argument.held);
        is_rvalue_end_ = argument.is_rvalue;
        end_loans(place, end, argument.held, argument.through, result);
        is_rvalue_end_ = false;
        note_taking(place, end.taking, result != NULL_TREE);
        // A frame door gives a part of the object to its body by a reference.  A
        // reference to the object itself is a second name of it.
        if (!argument.is_rvalue && facts.is_body_call && is_part(place)) {
            note_lent(place, argument.parameter);
        }
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
                share_with_result(facts, argument, place);
            }
        }
    }

    // A call that takes the object at PLACE by a reference that writes it, and whose
    // result is of an owner class that the object holds by value, can move that
    // member out into the result, as take() of a keyed holder does (the transfer).
    // Each loan whose owner the result can hold then also names the object of the
    // result: a derived loan beside the loan on the object.  Complexity: linear in
    // the loans and in the loans of the locals.
    void share_with_result(const CallFacts& facts, const Argument& argument, int place) {
        int const holder = facts.result_place;
        if (!state_.is_reachable || holder == kNoPlace || facts.is_constructor || facts.is_assignment
            || places_.root_decl(holder) == places_.root_decl(place)) {
            return;
        }
        tree const given = element_class(facts.result);
        const TypeSet& parts = ended_parts(argument.held);
        if (given == NULL_TREE || !CLASS_TYPE_P(given)
            || !std::binary_search(parts.begin(), parts.end(), static_cast<unsigned>(TYPE_UID(given)))) {
            return;
        }
        std::map<int, int> shared;
        std::size_t const count = loans_.size();
        for (std::size_t index = 0; index < count; ++index) {
            if (!places_.holds(place, loans_[index].place) || !is_inner_loan(loans_[index], facts.result)) {
                continue;
            }
            Loan made = loans_[index];
            made.place = holder;
            made.is_moved = true;
            int const loan = static_cast<int>(index);
            shared.emplace(loan, add_derived_loan(std::move(made), loan));
        }
        if (shared.empty()) {
            return;
        }
        for (auto& [local, loans] : state_.loans) {
            std::vector<int> added;
            for (int loan : loans) {
                auto found = shared.find(loan);
                if (found != shared.end()) {
                    added.push_back(found->second);
                }
            }
            loans.insert(loans.end(), added.begin(), added.end());
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
            end.taking = taking_of_parameter(facts.callee, index, argument.held);
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
            // A frame door gives a unique borrow of the object to its body.
            if (facts.is_body_call) {
                note_lent(place, argument.parameter);
            }
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
    // The place of the one argument other than INDEX that can keep the object of the
    // argument INDEX: a pointer or a reference that is not const, to a class that can
    // hold it.  kNoPlace when no argument or more than one can.
    int keeping_place(const std::vector<Argument>& arguments, std::size_t index) const {
        const Argument& argument = arguments[index];
        int found = kNoPlace;
        int count = 0;
        for (std::size_t other = 0; other < arguments.size(); ++other) {
            const Argument& keeper = arguments[other];
            if (other == index || keeper.parameter == NULL_TREE || keeper.is_closure || keeper.is_rvalue
                || !keeper.is_write || !is_indirect_type(keeper.parameter)
                || !can_hold_object(keeper.held, argument.held)) {
                continue;
            }
            ++count;
            found = keeper.places.size() == 1 ? keeper.places[0] : kNoPlace;
        }
        return count == 1 ? found : kNoPlace;
    }

    void take_moved(const CallFacts& facts, const std::vector<Argument>& arguments, std::size_t index, int place) {
        const Argument& argument = arguments[index];
        bool const is_same_class = facts.is_constructor && element_class(facts.result) == element_class(argument.held);
        note_moved(place);
        note_moved_out(place, argument.held);
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
            // An owner inside the object can go on in the result.
            int const holder = facts.is_constructor ? facts.built : facts.result_place;
            bool const gives_inner = holder != kNoPlace && places_.root_decl(holder) != places_.root_decl(place)
                                  && can_hold_owner(facts.result, ended_parts(argument.held));
            tree const result = gives_inner ? facts.result : NULL_TREE;
            end_by_call(facts, argument, index, place, result);
            if (gives_inner) {
                transfer_loans(place, holder, argument.held, result);
                hold_owner(holder, place, argument.held);
                return;
            }
            // A different argument can keep the object, as the pointer of a construct
            // does.
            int const keeper = keeping_place(arguments, index);
            if (keeper == kNoPlace) {
                transfer_loans(place, kNoPlace, argument.held);
            } else {
                kept_objects_.push_back(KeptObject{place, keeper, argument.held});
            }
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
        end.taking = taking_of_parameter(facts.callee, index, argument.held);
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
        end.taking = taking_of_capture(loan.closure, loan.field);
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
    // The objects that a different argument of the call in hand keeps.
    struct KeptObject {
        int from = kNoPlace;
        int to = kNoPlace;
        tree type = NULL_TREE;
    };
    std::vector<KeptObject> kept_objects_;
    // The temporaries with an end of the full expression in hand.
    std::vector<tree> temporaries_;
    // True where the hidden cursor of a range loop is a use.
    bool reads_cursors_ = false;
    // True while the event in hand takes an object by a move.
    bool is_rvalue_end_ = false;
    std::unordered_map<int, int> parameters_;
    // The parameter that a local copy of it, or of its address, names (the ends
    // beside a borrow).
    std::unordered_map<unsigned, int> parameter_copies_;
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

// The local or the parameter that the argument NODE names, through its addresses,
// members, elements, casts, std::move, the move into a parameter by value and the
// doors of a part, or null.  The front end gives a value of an empty class as the
// source and then an
// empty value, in a COMPOUND_EXPR.
tree root_of_argument(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = strip_value(node);
        if (node == NULL_TREE) {
            break;
        }
        switch (TREE_CODE(node)) {
            case COMPONENT_REF:
            case ARRAY_REF:
            case INDIRECT_REF:
            case ADDR_EXPR:
            case COMPOUND_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case TARGET_EXPR:
                node = TARGET_EXPR_INITIAL(node);
                continue;
            case CALL_EXPR:
            case AGGR_INIT_EXPR: {
                tree const constructor = cp_get_callee_fndecl_nofold(node);
                if (identity_call(node) != NULL_TREE) {
                    node = CALL_EXPR_ARG(node, 0);
                    continue;
                }
                if (constructor != NULL_TREE && DECL_CONSTRUCTOR_P(constructor) && argument_count(node) == 2) {
                    node = argument_at(node, 1);
                    continue;
                }
                // A door of a part gives a part of its first argument (the parts).
                if (TREE_CODE(node) == CALL_EXPR && call_expr_nargs(node) >= 1 && part_index_of(constructor) >= 0) {
                    node = CALL_EXPR_ARG(node, 0);
                    continue;
                }
                return NULL_TREE;
            }
            case VAR_DECL:
            case PARM_DECL:
                return node;
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

// True for a call whose two arguments can name one object: one local names both,
// or a reference local names one of them (the two names).
bool can_name_twice(tree call) {
    int const count = TREE_CODE(call) == AGGR_INIT_EXPR ? aggr_init_expr_nargs(call) : call_expr_nargs(call);
    if (count < 2) {
        return false;
    }
    std::vector<tree> roots;
    for (int index = 0; index < count; ++index) {
        tree const root = root_of_argument(TREE_CODE(call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_ARG(call, index)
                                                                             : CALL_EXPR_ARG(call, index));
        if (root == NULL_TREE) {
            continue;
        }
        bool const is_reference_local = VAR_P(root) && is_indirect_type(TREE_TYPE(root));
        if (is_reference_local || std::find(roots.begin(), roots.end(), root) != roots.end()) {
            return true;
        }
        roots.push_back(root);
    }
    return false;
}

tree find_borrow(tree* node, int* walk_subtrees, void*) {
    if (TYPE_P(*node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if ((TREE_CODE(*node) == CALL_EXPR || TREE_CODE(*node) == AGGR_INIT_EXPR) && can_name_twice(*node)) {
        return *node;
    }
    if (TREE_CODE(*node) == DECL_EXPR && VAR_P(DECL_EXPR_DECL(*node))
        && holds_borrow(TREE_TYPE(DECL_EXPR_DECL(*node)))) {
        return *node;
    }
    // A closure, and a temporary that holds a borrow, can give a borrow to a call
    // (the ends beside a borrow).
    if (TREE_CODE(*node) == LAMBDA_EXPR
        || (TREE_CODE(*node) == TARGET_EXPR && TREE_TYPE(*node) != NULL_TREE && holds_borrow(TREE_TYPE(*node)))) {
        return *node;
    }
    return NULL_TREE;
}

// True when a local, a parameter or a temporary of FUNCTION can hold a borrow.
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
        known.all.add(effect.all);
        known.inner.add(effect.inner);
    }
    for (StaleUse& use : reading.uses) {
        stale_uses.push_back(std::move(use));
    }
    for (const TwoNames& names : reading.two_names) {
        Place place = place_of(names.location, Scope::quarantine);
        if (place.file != nullptr) {
            add_finding(Kind::two_names, nullptr, place, names.entity);
        }
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

// What the calls of a taking can end: the classes that a body ends, the classes
// that a body moves out of the object, and the owners that a call with no body can
// end.
struct TakingEnds {
    TypeSet ended;
    TypeSet moved;
    TypeSet unseen;
    TypeSet lent;  // the types that a body lends to a closure
    bool is_lent = false;
    bool is_captured = false;  // a closure of a body captures the parameter
    std::set<unsigned> unseen_callees;  // the functions with no body in the unit that a taking reaches
};

void collect_ends(const Taking& taking, TakingEnds& ends, std::set<std::pair<std::uint64_t, bool>>& seen) {
    if (!taking.is_capture && read_functions.count(taking.callee) == 0) {
        auto owners = taken_owners.find(taking.owners);
        if (owners != taken_owners.end()) {
            add_types(ends.unseen, owners->second);
        }
        if (deleted_functions.count(taking.callee) == 0) {
            ends.unseen_callees.insert(taking.callee);
        }
        return;
    }
    if (!seen.emplace(taking.key, taking.is_inner).second) {
        return;
    }
    auto effect = unit_effects.find(taking.key);
    if (effect == unit_effects.end()) {
        return;
    }
    const Ends& of_call = taking.is_inner ? effect->second.inner : effect->second.all;
    add_types(ends.ended, of_call.ended);
    add_types(ends.moved, of_call.moved);
    add_types(ends.lent, of_call.lent);
    ends.is_lent = ends.is_lent || of_call.is_lent;
    ends.is_captured = ends.is_captured || of_call.is_captured;
    for (const Taking& next : of_call.takings) {
        collect_ends(next, ends, seen);
    }
}

// The ends of each taking that a use asked about.
std::map<Taking, TakingEnds> taking_ends;

const TakingEnds& ends_of(const Taking& taking) {
    auto found = taking_ends.find(taking);
    if (found == taking_ends.end()) {
        TakingEnds ends;
        std::set<std::pair<std::uint64_t, bool>> seen;
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
    switch (call_end(end.taking, end.types)) {
        case CallEnd::body:
            return end.text;
        case CallEnd::unseen:
            return end.text + ", which reaches a function with no body in this unit";
        case CallEnd::none:
            break;
    }
    return std::string();
}

// The text of END when it changes the loan of USE, which a closure holds through a
// capture, or an empty text.  A move of an owner of the loan out of its object, a
// lend of the object to a closure, and a function with no body that gets the object
// and the closure change it too (the borrows through a capture).  A move or a lend
// changes a plain borrow beside the owner only when a closure of the callee captures
// the parameter of the borrow, because a move of an owner keeps its block, and the
// lend ends before the callee reads the borrow outside a closure.
std::string judged_through_text(const StaleUse& use, const JudgedEnd& end) {
    std::string const text = judged_text(end);
    if (!text.empty() || end.kind != EndKind::call) {
        return text;
    }
    const TakingEnds& ends = ends_of(end.taking);
    bool const can_read_away = !use.is_plain || ends_of(use.borrow_taking).is_captured;
    if (can_read_away && have_common_type(end.types, ends.moved)) {
        return end.text + ", which moves an owner of the borrow";
    }
    // A loan of the whole object names each type that a lend can name.
    if (can_read_away && ends.is_lent && (use.named.empty() || can_name_one_object(use.named, ends.lent))) {
        return end.text + ", which lends the object to a closure under a unique borrow";
    }
    // A function with no body in the unit that gets the object and the closure can
    // lend one to the other.  A call through a pointer names no function, and after
    // an error of the unit a function with no record can have a body.
    if (seen_error()) {
        return std::string();
    }
    const std::set<unsigned>& closure_callees = ends_of(use.closure).unseen_callees;
    for (unsigned callee : ends.unseen_callees) {
        if (callee != 0 && closure_callees.count(callee) != 0) {
            return end.text + ", which reaches a function with no body in this unit with the closure";
        }
    }
    return std::string();
}

}  // namespace

unsigned owner_key(tree type) {
    type = element_class(type);
    if (type == NULL_TREE || !RECORD_OR_UNION_TYPE_P(type)) {
        return kOwnsNothing;
    }
    unsigned const id = TYPE_UID(type);
    if (taken_owners.count(id) == 0 && !keep_taken_owners(type)) {
        check_at_end(CheckSubject{type, NULL_TREE}, [type] { close_taken_owners(type); });
    }
    return id;
}

Taking taking_of_parameter(tree callee, std::size_t index, tree held) {
    Taking taking;
    taking.owners = owner_key(held);
    if (callee != NULL_TREE) {
        tree function = DECL_CLONED_FUNCTION_P(callee) ? DECL_CLONED_FUNCTION(callee) : callee;
        taking.callee = DECL_UID(function);
        taking.key = parameter_key(function, static_cast<int>(index));
        if (DECL_DELETED_FN(function)) {
            deleted_functions.insert(DECL_UID(function));
        }
    }
    return taking;
}

Taking taking_of_capture(unsigned closure, unsigned field) {
    Taking taking;
    taking.key = capture_key(closure, field);
    taking.is_capture = true;
    return taking;
}

CallEnd call_end(const Taking& taking, const TypeSet& types) {
    const TakingEnds& ends = ends_of(taking);
    if (have_common_type(types, ends.ended)) {
        return CallEnd::body;
    }
    // After an error of the unit, a function with no record can have a body.
    return !seen_error() && have_common_type(types, ends.unseen) ? CallEnd::unseen : CallEnd::none;
}

CallEnd call_end(const Taking& taking, unsigned owners) {
    auto found = taken_owners.find(owners);
    return found != taken_owners.end() ? call_end(taking, found->second) : CallEnd::none;
}

void finish_live_borrows() {
    // A use through a capture comes after each other use, so a finding of a
    // different form at the same place keeps its text.  A plain borrow beside a
    // writer keeps its place in the first pass.
    for (bool const is_through_pass : {false, true}) {
        for (const StaleUse& use : stale_uses) {
            bool const is_late = use.is_through && !use.is_plain;
            Place place = is_late == is_through_pass ? place_of(use.location, Scope::quarantine) : Place{};
            if (place.file == nullptr) {
                continue;
            }
            for (const JudgedEnd& end : use.ends) {
                bool const is_lent = !use.is_holder || use.is_receiver || ends_of(use.lender).is_lent;
                std::string const text = !is_lent       ? std::string()
                                       : use.is_through ? judged_through_text(use, end)
                                                        : judged_text(end);
                if (!text.empty()) {
                    add_finding(Kind::stale_borrow, nullptr, place, use.borrow + " after " + text);
                    end_locations.emplace(place.spelling, end.location);
                    break;
                }
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

void report_two_names(const Finding& finding) {
    error_at(finding.spelling, "this call gives one object to two parameters: %s", finding.entity.c_str());
    inform(finding.spelling, "the object is linear or a unique borrow, so the callee can write or move it under one "
                             "name and then use it under the other name");
    inform(finding.spelling, "give the object once, or give the two parts that the callee needs");
    inform(finding.spelling,
           "a test of the call itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

void register_live_borrows(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
}

}  // namespace crucible_plugin
