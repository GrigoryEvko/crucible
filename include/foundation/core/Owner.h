#pragma once

// The core of each owner of heap storage in the base: Box, Hole, HeapArray,
// MaskedArray, GrowList, GrowText, AlignedBytes and SwissTableBuffer.
//
// An owner keeps its fields in a state: a plain record, such as the first
// element and the count.  The state type gives two functions:
//
//   emptied(state)   the state of a source after a move.  It owns nothing,
//                    and a growable owner keeps its maximum in it
//   release(state)   ends what the state owns, such as each element and the
//                    storage.  It does nothing for an emptied state
//
// OwnerCore<State, Owner> holds the state and gives the two moves and the end
// of the owner.  An owner derives from the core with itself as Owner, reads
// its fields through state_, and defaults its own moves, so each owner has
// one order.  No member of the core is public, so the public base gives a
// caller no door, and the walks of the roster see the base with no unchecked
// access.
//
// Owner of a core
// The core names its Owner as its one friend.  The constructors, the moves,
// the end and run_key_() are private, so only the Owner builds the core and
// takes the key of its run.  A class that derives from the core of another
// owner, such as OwnerCore<S, Box<int>> with S from the public base of a
// Box, builds no core and takes no key.  The core asks that Owner is a class
// of the namespace foundation or fixy (OwnerOfTheBase), and the rule
// base-namespace refuses such a class in a file outside the base.  So a
// class outside the base that names itself as Owner is refused too.  A state
// can have more than one owner, as AlignedBytesState has.
//
// Order of a move assignment
// The source can live inside the value that the target holds, as in
// head = move(head->next): the end of the old value of head ends the source.
// So the assignment does these steps, in this order:
//
//   1. It reads the state of the source.
//   2. It writes the emptied state into the source.
//   3. It reads the old state of the target.
//   4. It writes the state of step 1 into the target.
//   5. It releases the old state of step 3.
//
// No read of the source comes after the release, and code that the release
// runs sees the new value in the target.  A self-move needs no test of the
// address: step 3 reads the emptied state of step 2, so step 5 releases
// nothing, and step 4 gives the value back.
//
// Copy of a state
// Each copy of a state goes through a local state of the core, and no local
// state is const.  GCC keeps a const local aggregate in memory, and it copies
// an aggregate from memory to memory with one 16-byte access and one 8-byte
// access.  A field that a narrow store wrote then reaches a wide load, and
// the load waits for a store forward that cannot occur.  GCC keeps a local
// state that is not const in registers, so a copy between a state in memory
// and a local reads and writes one field at a time.  An owner that gives its
// state away calls take_state_, which copies in the same way.  The shape
// probe owner_take_and_end of test/shape/core_shapes.cpp holds this code.
//
// The build from a state, the moves, the end, take_state_ and reset_ are
// always inline, and so is each constructor of an owner that takes its
// storage, such as the constructor of an UnwrittenBytes.  GCC stops
// the inlining of a unit when the unit grows past --param inline-unit-growth,
// and a call that stays out of line takes the address of a state, which
// then goes through the stack.  So the hot path of an owner does not depend
// on the inline budget of its unit.  The release of a state that is small
// and hot is always inline too, as AlignedBytesState::release is.
//
// Marker
// A state opts in to the core with the member
//
//   using owner_state_mark = ::foundation::core::OwnerStateMark;
//
// The core refuses a state with no marker, so a record with the two
// functions does not become a state by accident.  The namespace of a state
// is no part of the gate, so each layer keeps its states in its own
// namespace.
//
// Roster
// Each layer keeps its states in one home namespace: detail::owner of
// foundation::core, and fixy::detail::owner.  The probe of the owners,
// test/foundation/test_core_owner_probe.cpp, names each home in an
// OwnerHomes list and reads the homes through count_owner_roster_gaps, so a
// new owner on the core is in its roster at once.

#include <foundation/Platform.h>
#include <foundation/reflect/Members.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// The marker of an owner state.  It has no value, so the member type costs
// no byte.
struct OwnerStateMark final {};

// The home namespaces of a roster of owner states.
template <std::meta::info... Homes>
struct OwnerHomes final {
    // The last entry is a sentinel, so an empty list gives an array.
    static constexpr std::meta::info homes[sizeof...(Homes) + 1] = {Homes..., std::meta::info{}};
    static constexpr std::size_t count = sizeof...(Homes);
};

// The state opts in to the core with the marker.  Each clause is its own
// concept, so a refusal names the clause that failed.
template <class State>
concept MarksOwnerState = std::same_as<typename State::owner_state_mark, OwnerStateMark>;

// The core reads a state by a copy, writes the default state, and calls the
// two functions.
template <class State>
concept GivesOwnerOperations = std::is_trivially_copyable_v<State> && std::is_nothrow_default_constructible_v<State>
                            && requires(State const& state) {
                                   { State::emptied(state) } noexcept -> std::same_as<State>;
                                   { State::release(state) } noexcept -> std::same_as<void>;
                               };

// The fields of an owner, with the marker and the two functions of the core,
// in a namespace of each layer.
template <class State>
concept OwnerState = MarksOwnerState<State> && GivesOwnerOperations<State>;

// The mark of an owner that holds its storage through pointers: a class that is
// not on OwnerCore, or one on OwnerCore whose members move no element, such as a
// SegmentList.  No door of the class stores a pointer or a borrow that a caller
// gives.  The store check of a frame body
// (foundation/core/CaptureStore.h) reads its type arguments and no member.
// Each member that is not const keeps each object that a borrow of a caller
// can name, until the end of the owner.  So the kind stale-borrow of the rule
// view-escape reads a call of a member as no end of a borrow.  An EpochArena
// ends the objects of an epoch, and only the frame of that epoch can name them.
// A class whose blocks can hold an object of each type names them by a pointer
// to bytes or to void, or a class behind a pointer holds an array of bytes.  The
// blocks of each other class hold the types that its pointers name.
struct owns_its_storage final {};

// A state whose release gives nothing back, such as the run of a region that
// owns no storage.  It states the member releases_nothing as true, so the end
// of its core is trivial, as the end of its two words was before the core.
template <class State>
concept ReleasesNothing = requires { requires State::releases_nothing; };

namespace detail {

// True when the class of type is declared in the namespace foundation or
// fixy, or in a namespace inside them: the outermost scope of the class below
// the global namespace is one of the two.  The walk goes up through each
// enclosing class and function, so a local class and a closure of a function
// of the base are classes of the base too.  A specialization gives the scope
// of its template.  The walk stops at a scope with no parent: GCC gives the
// function main no parent, so a class of main is no class of the base.  The
// layer foundation declares no namespace fixy, so the test reads the name of
// the namespace.  Complexity: the depth of the scopes.
[[nodiscard]] consteval bool is_class_of_the_base_(std::meta::info type) {
    std::meta::info outermost = std::meta::dealias(type);
    while (std::meta::has_parent(outermost) && std::meta::parent_of(outermost) != ^^::) {
        outermost = std::meta::parent_of(outermost);
    }
    return std::meta::is_namespace(outermost)
        && (::foundation::reflect::has_identifier_named(outermost, "foundation")
            || ::foundation::reflect::has_identifier_named(outermost, "fixy"));
}

}  // namespace detail

// The Owner of a core: a class of the base (owner of a core).  A class
// template gives the namespace of its template.
template <class Owner>
concept OwnerOfTheBase = std::is_class_v<Owner> && detail::is_class_of_the_base_(^^Owner);

template <OwnerState State, class Owner>
    requires OwnerOfTheBase<Owner>
class OwnerCore;

namespace detail {

// The key of the constructors of a View from raw parts
// (foundation/core/Region.h).  An owner on the core proves its run: its state
// holds the first element and the count of the elements that it owns, and its
// moves and its end keep the two.  OwnerCore is the one friend of the key,
// and it gives the key only to its Owner, through the private run_key_().  So
// only an owner of the base makes a View from raw parts, and the roster of
// the owners names each such class.  The key is in namespace detail, and the
// rule owners keeps each caller outside the base out, also a caller that
// deduces the key from run_key_().
class [[nodiscard]] OwnerRunKey final {
    template <OwnerState State, class Owner>
        requires OwnerOfTheBase<Owner>
    friend class ::foundation::core::OwnerCore;

    constexpr OwnerRunKey() noexcept {}
    constexpr OwnerRunKey(OwnerRunKey const&) noexcept {}

public:
    OwnerRunKey& operator=(OwnerRunKey const&) = delete("a key of the run of an owner lives for one construction");
};

}  // namespace detail

template <OwnerState State, class Owner>
    requires OwnerOfTheBase<Owner>
class OwnerCore {
    // The one class that builds, moves and ends this core, and takes the key
    // of its run (owner of a core).
    friend Owner;

    // The key of the constructors of a View from raw parts, for the run that
    // this owner holds (detail::OwnerRunKey).  An owner whose friend needs the
    // key names it in its own private part with `using Core::run_key_;`.
    [[nodiscard]] static constexpr detail::OwnerRunKey run_key_() noexcept { return detail::OwnerRunKey{}; }

    constexpr OwnerCore() noexcept = default;
    // The build from a state is always inline, as the moves are.  A build out
    // of line takes the state by its address, so the fields go through memory.
    CRUCIBLE_INLINE constexpr explicit OwnerCore(State const& state) noexcept : state_{state} {}

    // The state moves through a local (the copy of a state).
    CRUCIBLE_INLINE constexpr OwnerCore(OwnerCore&& other) noexcept {
        State taken = other.take_state_();
        state_ = taken;
    }

    // The five steps of the order of a move assignment.  The steps work on
    // the states, so no step builds a second core.  take_state_ does steps
    // 1 and 2.
    CRUCIBLE_INLINE constexpr OwnerCore& operator=(OwnerCore&& other) noexcept {
        State taken = other.take_state_();
        State held = state_;
        state_ = taken;
        State::release(held);
        return *this;
    }

    OwnerCore(OwnerCore const&) = delete("an owner owns its storage alone: a copy would release it two times");
    OwnerCore&
    operator=(OwnerCore const&) = delete("an owner owns its storage alone: a copy would release it two times");

    CRUCIBLE_INLINE constexpr ~OwnerCore()
        requires(!ReleasesNothing<State>)
    {
        State::release(state_);
    }
    constexpr ~OwnerCore()
        requires ReleasesNothing<State>
    = default;

protected:
    // The fields of the owner.  The owner reads and writes them, and the core
    // moves and releases them.
    State state_{};

    // Gives the state to the caller, and leaves the emptied state in this
    // owner.  The caller then owns what the state owns.
    [[nodiscard]] CRUCIBLE_INLINE constexpr State take_state_() noexcept {
        State taken = state_;
        state_ = State::emptied(taken);
        return taken;
    }

    // Releases the value, and leaves the owner empty.  The owner is empty
    // before the release starts, as in a move assignment.
    CRUCIBLE_INLINE constexpr void reset_() noexcept {
        State held = take_state_();
        State::release(held);
    }
};

}  // namespace foundation::core

namespace foundation::core::detail {

// The home of each state type of an owner of foundation.
namespace owner {}

// The class or the class template that a type names: a specialization gives
// its template.
[[nodiscard]] consteval std::meta::info owner_entity_of_(std::meta::info type) {
    std::meta::info const bare = std::meta::dealias(type);
    return std::meta::has_template_arguments(bare) ? std::meta::template_of(bare) : bare;
}

// The result of a roster check: the classes and class templates of the home
// namespace, the members that exactly one owner covers, and the owners that
// cover no member.  A complete roster has covered == roster and stale == 0.
struct OwnerRosterVerdict {
    std::size_t roster = 0;
    std::size_t covered = 0;
    std::size_t stale = 0;
};

// The state of an owner that derives publicly from a specialization of the
// class template Core, or no reflection when no public base is one.  The
// walks index the lists of a query, as the walks of foundation/core/Scalar.h
// do, and they read only what this function can access.
[[nodiscard]] consteval std::meta::info owner_state_of_(std::meta::info owner, std::meta::info core) {
    auto const bases = std::meta::bases_of(owner, std::meta::access_context::current());
    for (std::size_t index = 0; index < bases.size(); ++index) {
        std::meta::info const type = std::meta::dealias(std::meta::type_of(bases[index]));
        if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == core) {
            return std::meta::template_arguments_of(type)[0];
        }
    }
    return std::meta::info{};
}

// True when the owner writes a public move of its own, so the core does not
// give the order of its moves.
[[nodiscard]] consteval bool has_own_move_(std::meta::info owner) {
    auto const members = std::meta::members_of(owner, std::meta::access_context::current());
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::meta::info const member = members[index];
        bool const is_move = std::meta::is_move_constructor(member) || std::meta::is_move_assignment(member);
        if (is_move && std::meta::is_user_provided(member)) {
            return true;
        }
    }
    return false;
}

// True when the namespace is one of the homes of the list.
template <class HomeList>
[[nodiscard]] consteval bool is_owner_home_(std::meta::info space) {
    for (std::size_t index = 0; index < HomeList::count; ++index) {
        if (HomeList::homes[index] == space) {
            return true;
        }
    }
    return false;
}

// A specialization of OwnerHomes.
template <class HomeList>
inline constexpr bool is_owner_homes_ = false;

template <std::meta::info... Homes>
inline constexpr bool is_owner_homes_<OwnerHomes<Homes...>> = true;

template <class HomeList>
concept OwnerHomeList = is_owner_homes_<HomeList>;

// Compares the members of the home namespaces with the owners that a probe
// runs.  An owner covers the state of its base Core when it writes no move
// of its own.  A member that an owner covers counts one time.  An owner with
// no such base, with a move of its own, with a state outside each home, or
// with the state of an earlier owner is stale.  count_owner_roster_gaps
// calls it with the homes of its caller and OwnerCore, and the stand-in
// roster of the check file with homes and a core of its own.
// Complexity: O((members + owners) * owners), with O(homes) for each home
// test.
template <OwnerHomeList HomeList, std::meta::info Core, class... Owners>
[[nodiscard]] consteval OwnerRosterVerdict verdict_of_owner_roster() {
    // The last entry is a sentinel, so an empty pack gives an array.
    std::meta::info const owners[] = {^^Owners..., std::meta::info{}};
    std::meta::info covered[sizeof...(Owners) + 1]{};
    OwnerRosterVerdict verdict{};
    for (std::size_t index = 0; index < sizeof...(Owners); ++index) {
        std::meta::info const state = owner_state_of_(owners[index], Core);
        bool is_stale = state == std::meta::info{} || has_own_move_(owners[index]);
        std::meta::info const entity = is_stale ? std::meta::info{} : owner_entity_of_(state);
        is_stale = is_stale || !is_owner_home_<HomeList>(std::meta::parent_of(entity));
        for (std::size_t earlier = 0; !is_stale && earlier < index; ++earlier) {
            is_stale = covered[earlier] == entity;
        }
        if (is_stale) {
            ++verdict.stale;
        } else {
            covered[index] = entity;
        }
    }
    for (std::size_t home = 0; home < HomeList::count; ++home) {
        auto const members = std::meta::members_of(HomeList::homes[home], std::meta::access_context::current());
        for (std::size_t position = 0; position < members.size(); ++position) {
            std::meta::info const member = members[position];
            bool const is_class = std::meta::is_class_template(member)
                               || (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                                   && std::meta::is_class_type(member));
            if (!is_class) {
                continue;
            }
            ++verdict.roster;
            for (std::size_t index = 0; index < sizeof...(Owners); ++index) {
                if (covered[index] == member) {
                    ++verdict.covered;
                    break;
                }
            }
        }
    }
    return verdict;
}

}  // namespace foundation::core::detail

namespace foundation::core {

// The gaps of a probe of the owners: the number of states of the homes that
// no owner of the list covers, plus the number of owners of the list that
// are stale.  An owner is stale when it does not derive from OwnerCore,
// writes a move of its own, has a state outside each home, or has the state
// of an earlier owner.  Zero means that each owner state has a probe and
// that no probe is stale.  The probe of the owners asserts zero.
// Complexity: O((members + owners) * owners).
// The home of the owner states of foundation, for an OwnerHomes list.
inline constexpr std::meta::info owner_state_home = ^^detail::owner;

template <detail::OwnerHomeList HomeList, class... Owners>
[[nodiscard]] consteval std::size_t count_owner_roster_gaps() {
    detail::OwnerRosterVerdict const verdict = detail::verdict_of_owner_roster<HomeList, ^^OwnerCore, Owners...>();
    return (verdict.roster - verdict.covered) + verdict.stale;
}

}  // namespace foundation::core
