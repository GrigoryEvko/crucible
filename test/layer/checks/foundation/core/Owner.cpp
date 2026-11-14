// The compile-time checks of foundation/core/Owner.h.

#include <foundation/core/Owner.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core::detail {

namespace owner {

// A state of the home with the two functions of the core.
struct CheckState {
    std::size_t first = 0;
    std::size_t count = 0;

    [[nodiscard]] static constexpr CheckState emptied(CheckState const& /*state*/) noexcept { return CheckState{}; }
    static constexpr void release(CheckState const& /*state*/) noexcept {}
};

// A state of the home that is not trivially copyable.
struct CheckStateWithCopy {
    std::size_t first = 0;

    CheckStateWithCopy() noexcept = default;
    CheckStateWithCopy(CheckStateWithCopy const& other) noexcept : first{other.first} {}
    CheckStateWithCopy& operator=(CheckStateWithCopy const& other) noexcept {
        first = other.first;
        return *this;
    }

    [[nodiscard]] static constexpr CheckStateWithCopy emptied(CheckStateWithCopy const& /*state*/) noexcept {
        return CheckStateWithCopy{};
    }
    static constexpr void release(CheckStateWithCopy const& /*state*/) noexcept {}
};

}  // namespace owner

namespace owner_checks {

// A state with the two functions of the core, in a namespace that is not
// the home.
struct StateOutsideHome {
    std::size_t first = 0;

    [[nodiscard]] static constexpr StateOutsideHome emptied(StateOutsideHome const& /*state*/) noexcept {
        return StateOutsideHome{};
    }
    static constexpr void release(StateOutsideHome const& /*state*/) noexcept {}
};

// An owner on the core, with the moves of the core.
struct CheckOwner final : public OwnerCore<owner::CheckState> {
    CheckOwner() noexcept = default;
    CheckOwner(CheckOwner&&) noexcept = default;
    CheckOwner& operator=(CheckOwner&&) noexcept = default;
};

// The core takes a state of the home with the two functions, and refuses a
// state of each other namespace and a state that is not trivially copyable.
static_assert(OwnerState<owner::CheckState>);
static_assert(!OwnerState<StateOutsideHome>);
static_assert(!OwnerState<owner::CheckStateWithCopy>);
static_assert(!OwnerState<int>);

// The core adds no byte to the state, and an owner on it moves and does not
// copy.
static_assert(sizeof(OwnerCore<owner::CheckState>) == sizeof(owner::CheckState));
static_assert(sizeof(CheckOwner) == sizeof(owner::CheckState));
static_assert(std::is_nothrow_move_constructible_v<CheckOwner>);
static_assert(std::is_nothrow_move_assignable_v<CheckOwner>);
static_assert(!std::is_copy_constructible_v<CheckOwner>);
static_assert(!std::is_copy_assignable_v<CheckOwner>);
static_assert(!std::is_trivially_copyable_v<CheckOwner>);

// The verdict reads the state of an owner from its base, and the home of the
// state from the state.
static_assert(owner_state_of_(^^CheckOwner, ^^OwnerCore) == ^^owner::CheckState);
static_assert(owner_entity_of_(^^owner::CheckState) == ^^owner::CheckState);
static_assert(!has_own_move_(^^CheckOwner));

// The home holds the two states above and the states of each owner header
// that the unit includes, because the walk units compile this file after
// each header.  So the checks compare counts and pin no total: CheckOwner
// covers one state, and a second CheckOwner is one stale owner.
inline constexpr std::size_t kGapsWithNoOwner = ::foundation::core::count_owner_roster_gaps<>();
static_assert(kGapsWithNoOwner >= 2);
static_assert(::foundation::core::count_owner_roster_gaps<CheckOwner>() + 1 == kGapsWithNoOwner);
static_assert(::foundation::core::count_owner_roster_gaps<CheckOwner, CheckOwner>() == kGapsWithNoOwner);

// The stand-in roster has three states.  Two owners cover two of them, and
// three owners are stale: a second owner of one state, an owner with a move
// of its own, and an owner with no core.  The verdict must count exactly
// that, so it can go red.
namespace standin_states {

struct StateA {};

template <class T>
struct StateB {};

struct StateC {};

}  // namespace standin_states

namespace standin_owners {

template <class State>
struct Core {
    State state_{};
};

struct CoveredA : Core<standin_states::StateA> {};

struct CoveredB : Core<standin_states::StateB<int>> {};

struct SecondA : Core<standin_states::StateA> {};

struct OwnMove : Core<standin_states::StateC> {
    OwnMove() noexcept = default;
    OwnMove(OwnMove&& /*other*/) noexcept {}
    OwnMove& operator=(OwnMove&& /*other*/) noexcept { return *this; }
};

struct NoCore {};

}  // namespace standin_owners

inline constexpr OwnerRosterVerdict kStandinVerdict =
    verdict_of_owner_roster<^^standin_states, ^^standin_owners::Core, standin_owners::CoveredA,
                            standin_owners::CoveredB, standin_owners::SecondA, standin_owners::OwnMove,
                            standin_owners::NoCore>();

static_assert(kStandinVerdict.roster == 3, "the stand-in roster did not reflect its three states");
static_assert(kStandinVerdict.covered == 2, "the verdict covered a state that no owner covers");
static_assert(kStandinVerdict.stale == 3,
              "the verdict missed a second owner of one state, an owner with a move of its own, or an owner with no "
              "core");

}  // namespace owner_checks

}  // namespace foundation::core::detail
