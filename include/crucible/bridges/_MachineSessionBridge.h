#pragma once

// A machine and a session are two views of one typestate value.  A machine
// transition is a local computation.  A session step is an event an observer
// can log, replay or compare.  A bridge owns one machine and mints a session
// handle over it on demand, so the same state is driven imperatively from
// inside and read structurally from outside.
//
// The bridge is pinned because a minted handle stores the address of the
// machine it borrows.  Moving the bridge would leave the handle pointing at
// freed storage, and the next session step would follow it.
//
// Proto is not constrained to a single party.  Party count is a claim about
// the protocol a caller picked, not a structural property of any type here, so
// a concept asserting it would reject multi-party protocols whose local view
// is a perfectly valid session over this machine.  Single-party typestate is
// the intended use and is left to caller discipline.

#include <crucible/Platform.h>
#include <crucible/safety/_Machine.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/sessions/Session.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename State, typename Proto>
class [[nodiscard]] SessionFromMachine : public Pinned<SessionFromMachine<State, Proto>> {
    static_assert(safety::proto::is_well_formed_v<Proto>, "crucible::session::diagnostic [Protocol_Ill_Formed]: "
                                                          "SessionFromMachine<State, Proto>: Proto must be well-formed "
                                                          "under is_well_formed_v.  Every Continue must have an "
                                                          "enclosing Loop.");

    Machine<State> machine_;

public:
    using state_type = State;
    using machine_type = Machine<State>;
    using protocol = Proto;

    using session_handle_type = decltype(safety::proto::mint_session_handle<Proto>(std::declval<machine_type*>()));

    constexpr explicit SessionFromMachine(State s) noexcept(std::is_nothrow_move_constructible_v<State>)
        : machine_{std::move(s)} {}

    template <typename... Args>
        requires std::is_constructible_v<State, Args...>
    constexpr explicit SessionFromMachine(std::in_place_t,
                                          Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>)
        : machine_{std::in_place, std::forward<Args>(args)...} {}

    constexpr explicit SessionFromMachine(machine_type m) noexcept : machine_{std::move(m)} {}

    ~SessionFromMachine() = default;

    [[nodiscard]] constexpr machine_type& machine() & noexcept { return machine_; }

    [[nodiscard]] constexpr const machine_type& machine() const& noexcept { return machine_; }

    [[nodiscard]] constexpr const State& state() const& noexcept { return machine_.data(); }

    [[nodiscard]] constexpr State& state_mut() & noexcept { return machine_.data_mut(); }

    // The bridge is the resource anchor and each handle is a lens for one
    // observable transition, so a fresh handle is minted per call.  At most
    // one may be live at a time: two handles would drive the same machine
    // state against each other.  Nothing here enforces that, exactly as
    // nothing enforces it for two mutable borrows of the machine.

    [[nodiscard]] auto session_view() & noexcept -> session_handle_type {
        return safety::proto::mint_session_handle<Proto>(&machine_);
    }

    // The returned string comes from __PRETTY_FUNCTION__, which the linker
    // deduplicates across every translation unit naming the same Proto.  A
    // constexpr string_view captured in one unit can therefore point into a
    // different surviving copy than the one a later runtime call walks.  The
    // symptom is a printf of the right bytes next to a find() that returns
    // npos.  Call this without constexpr capture for substring matching, and
    // keep constexpr use to checks that never materialise the value, such as
    // a size or a type identity assert.

    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept {
        return safety::proto::detail::type_name<Proto>();
    }

    // The bridge cannot be moved, so this is the only way the state leaves
    // it.  The bridge is moved-from afterwards and neither view is usable.

    [[nodiscard]] constexpr State extract() && noexcept(std::is_nothrow_move_constructible_v<State>) {
        return std::move(machine_).extract();
    }
};

// A state machine whose address is its cross-thread publication identity
// cannot be owned by a bridge: readers on other threads hold that address and
// need explicit acquire ordering on every read.  The atomic form therefore
// borrows the cell rather than owning a machine, and the session view is the
// observation and replay surface over it.
//
// Reads are uniform, so the concept below fixes their shape.  Writes are not:
// each cell decides which transitions are legal and can delete the illegal
// ones under its own diagnostic, so the write helper only forwards to the
// cell's own publish step.

template <typename Cell>
concept AtomicMachineCell = requires(const std::remove_cvref_t<Cell>& cell, std::memory_order order) {
    typename std::remove_cvref_t<Cell>::state_type;
    { cell.load(order) } -> std::same_as<typename std::remove_cvref_t<Cell>::state_type>;
};

template <typename Proto, typename Cell>
    requires(AtomicMachineCell<Cell> && safety::proto::is_well_formed_v<Proto>)
[[nodiscard]] constexpr auto mint_atomic_session(Cell& cell) noexcept {
    return safety::proto::mint_session_handle<Proto>(&cell);
}

template <typename Cell>
    requires AtomicMachineCell<Cell>
[[nodiscard]] constexpr typename std::remove_cvref_t<Cell>::state_type
atomic_machine_state(const Cell& cell, std::memory_order order = std::memory_order_acquire) noexcept {
    return cell.load(order);
}

template <typename Event, AtomicMachineCell Cell>
constexpr void publish_atomic_machine_transition(Cell*& cell, Event&& event) noexcept(
    noexcept(cell->publish_from_session(std::forward<Event>(event), std::memory_order_release)))
    requires requires { cell->publish_from_session(std::forward<Event>(event), std::memory_order_release); }
{
    cell->publish_from_session(std::forward<Event>(event), std::memory_order_release);
}

// The recovered pointer is a borrow.  The bridge that minted the handle has
// to outlive every pointer recovered from it.

template <typename State, typename LoopCtx>
[[nodiscard]] constexpr Machine<State>*
machine_from_session(safety::proto::SessionHandle<safety::proto::End, Machine<State>*, LoopCtx>&& sh) noexcept {
    return std::move(sh).close();
}

// Recovering the machine before the protocol ends is the lossy direction of
// the mapping.  The remaining steps the protocol declared never happen, and
// the detach is what tells the handle's destructor that the abandonment was
// deliberate.  The usual reason is that the bridge is about to be destroyed.
template <typename Proto, typename State, typename LoopCtx>
    requires(!std::is_same_v<Proto, safety::proto::End>)
[[nodiscard]] constexpr Machine<State>*
machine_from_session(safety::proto::SessionHandle<Proto, Machine<State>*, LoopCtx>&& sh) noexcept {
    Machine<State>* p = sh.resource();
    std::move(sh).detach(safety::proto::detach_reason::OwnerLifetimeBoundEarlyExit{});
    return p;
}

#ifdef NDEBUG
namespace detail::msb_release_size_test {

struct OneByteState {
    char x;
};
struct FourByteState {
    int x;
};
struct EightByteState {
    double x;
};

using Proto1 = safety::proto::End;
using Proto2 = safety::proto::Loop<safety::proto::Send<int, safety::proto::Continue>>;

static_assert(sizeof(SessionFromMachine<OneByteState, Proto1>) == sizeof(OneByteState),
              "Release-mode SessionFromMachine must add zero bytes beyond State.");

static_assert(sizeof(SessionFromMachine<FourByteState, Proto2>) == sizeof(FourByteState),
              "Release-mode SessionFromMachine must add zero bytes beyond State.");

static_assert(sizeof(SessionFromMachine<EightByteState, Proto1>) == sizeof(EightByteState),
              "Release-mode SessionFromMachine must add zero bytes beyond State.");

}  // namespace detail::msb_release_size_test
#endif

namespace detail::msb_self_test {

struct VigilModeState {
    enum class Mode : uint8_t {
        Idle,
        Recording,
        Replaying,
        Serving
    };
    Mode mode = Mode::Idle;
    uint32_t ticks = 0;
};

using VigilProto =
    safety::proto::Loop<safety::proto::Select<safety::proto::Send<int, safety::proto::Continue>, safety::proto::End>>;

using Bridge = SessionFromMachine<VigilModeState, VigilProto>;

static_assert(!std::is_copy_constructible_v<Bridge>);
static_assert(!std::is_move_constructible_v<Bridge>);
static_assert(!std::is_copy_assignable_v<Bridge>);
static_assert(!std::is_move_assignable_v<Bridge>);

static_assert(std::is_base_of_v<Pinned<Bridge>, Bridge>);

static_assert(std::is_same_v<typename Bridge::state_type, VigilModeState>);
static_assert(std::is_same_v<typename Bridge::machine_type, Machine<VigilModeState>>);
static_assert(std::is_same_v<typename Bridge::protocol, VigilProto>);

// A loop is unrolled one step at construction, so the minted handle carries
// the loop body as its protocol and the loop itself as the context.
using ExpectedSession = safety::proto::SessionHandle<
    safety::proto::Select<safety::proto::Send<int, safety::proto::Continue>, safety::proto::End>,
    Machine<VigilModeState>*, VigilProto>;
static_assert(std::is_same_v<typename Bridge::session_handle_type, ExpectedSession>);

}  // namespace detail::msb_self_test

}  // namespace crucible::safety
