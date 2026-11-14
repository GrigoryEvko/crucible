#pragma once

// The core of each owner of heap storage in the base: Box, Hole, HeapArray,
// MaskedArray, GrowList, GrowText, AlignedBuffer and SwissTableBuffer.
//
// An owner keeps its fields in a state: a plain record, such as the first
// element and the count.  The state type gives two functions:
//
//   emptied(state)   the state of a source after a move.  It owns nothing,
//                    and a growable owner keeps its maximum in it
//   release(state)   ends what the state owns, such as each element and the
//                    storage.  It does nothing for an emptied state
//
// OwnerCore<State> holds the state and gives the two moves and the end of
// the owner.  An owner derives from the core, reads its fields through
// state_, and defaults its own moves, so each owner has one order.  Each
// member of the core is protected, so the public base gives a caller no
// door, and the walks of the roster see the base with no unchecked access.
//
// THE ORDER OF A MOVE ASSIGNMENT
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
// THE ROSTER
// Each state type is a member of the namespace detail::owner, and the core
// refuses a state of each other namespace.  The probe of the owners,
// test/foundation/test_core_owner_probe.cpp, reads that namespace through
// count_owner_roster_gaps, so a new owner on the core is in its roster at
// once.

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core::detail {

// The home of each state type of an owner.
namespace owner {}

// The class or the class template that a type names: a specialization gives
// its template.
[[nodiscard]] consteval std::meta::info owner_entity_of_(std::meta::info type) {
    std::meta::info const bare = std::meta::dealias(type);
    return std::meta::has_template_arguments(bare) ? std::meta::template_of(bare) : bare;
}

// The fields of an owner, with the two functions of the core.  The state is
// a member of detail::owner, so the roster of the probe holds it.
template <class State>
concept OwnerState = std::is_trivially_copyable_v<State> && std::is_nothrow_default_constructible_v<State>
                  && requires(State const& state) {
                         { State::emptied(state) } noexcept -> std::same_as<State>;
                         { State::release(state) } noexcept -> std::same_as<void>;
                     } && std::meta::parent_of(owner_entity_of_(^^State)) == ^^owner;

template <OwnerState State>
class OwnerCore {
protected:
    // The fields of the owner.  The owner reads and writes them, and the core
    // moves and releases them.
    State state_{};

    constexpr OwnerCore() noexcept = default;
    constexpr explicit OwnerCore(State const& state) noexcept : state_{state} {}

    constexpr OwnerCore(OwnerCore&& other) noexcept : state_{other.state_} { other.state_ = State::emptied(state_); }

    // The five steps of THE ORDER OF A MOVE ASSIGNMENT.  The steps work on
    // the states, so no step builds a second core.
    constexpr OwnerCore& operator=(OwnerCore&& other) noexcept {
        State const taken = other.state_;
        other.state_ = State::emptied(taken);
        State const held = state_;
        state_ = taken;
        State::release(held);
        return *this;
    }

    OwnerCore(OwnerCore const&) = delete("an owner owns its storage alone: a copy would release it two times");
    OwnerCore&
    operator=(OwnerCore const&) = delete("an owner owns its storage alone: a copy would release it two times");

    constexpr ~OwnerCore() { State::release(state_); }

    // Releases the value, and leaves the owner empty.  The owner is empty
    // before the release starts, as in a move assignment.
    constexpr void reset_() noexcept {
        State const held = state_;
        state_ = State::emptied(held);
        State::release(held);
    }
};

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

// Compares the members of the namespace Home with the owners that a probe
// runs.  An owner covers the state of its base Core when it writes no move
// of its own.  A member that an owner covers counts one time.  An owner with
// no such base, with a move of its own, with a state outside Home, or with
// the state of an earlier owner is stale.  count_owner_roster_gaps calls it
// with detail::owner and OwnerCore, and the stand-in roster of the check file
// with a home and a core of its own.  Complexity: O(members * owners).
template <std::meta::info Home, std::meta::info Core, class... Owners>
[[nodiscard]] consteval OwnerRosterVerdict verdict_of_owner_roster() {
    // The last entry is a sentinel, so an empty pack gives an array.
    std::meta::info const owners[] = {^^Owners..., std::meta::info{}};
    std::meta::info covered[sizeof...(Owners) + 1]{};
    OwnerRosterVerdict verdict{};
    for (std::size_t index = 0; index < sizeof...(Owners); ++index) {
        std::meta::info const state = owner_state_of_(owners[index], Core);
        bool is_stale = state == std::meta::info{} || has_own_move_(owners[index]);
        std::meta::info const entity = is_stale ? std::meta::info{} : owner_entity_of_(state);
        is_stale = is_stale || std::meta::parent_of(entity) != Home;
        for (std::size_t earlier = 0; !is_stale && earlier < index; ++earlier) {
            is_stale = covered[earlier] == entity;
        }
        if (is_stale) {
            ++verdict.stale;
        } else {
            covered[index] = entity;
        }
    }
    auto const members = std::meta::members_of(Home, std::meta::access_context::current());
    for (std::size_t position = 0; position < members.size(); ++position) {
        std::meta::info const member = members[position];
        bool const is_class =
            std::meta::is_class_template(member)
            || (std::meta::is_type(member) && !std::meta::is_type_alias(member) && std::meta::is_class_type(member));
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
    return verdict;
}

}  // namespace foundation::core::detail

namespace foundation::core {

// The gaps of a probe of the owners: the number of states of detail::owner
// that no owner of the list covers, plus the number of owners of the list
// that are stale.  An owner is stale when it does not derive from OwnerCore,
// writes a move of its own, or has the state of an earlier owner.  Zero means
// that each owner state has a probe and that no probe is stale.  The probe of
// the owners asserts zero.  Complexity: O(members * owners).
template <class... Owners>
[[nodiscard]] consteval std::size_t count_owner_roster_gaps() {
    detail::OwnerRosterVerdict const verdict =
        detail::verdict_of_owner_roster<^^detail::owner, ^^detail::OwnerCore, Owners...>();
    return (verdict.roster - verdict.covered) + verdict.stale;
}

}  // namespace foundation::core
