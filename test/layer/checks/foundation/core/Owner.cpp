// The compile-time checks of foundation/core/Owner.h.

#include <foundation/core/Owner.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core::detail {

namespace owner {

// A state of the home with the marker and the two functions of the core.
struct CheckState {
    using owner_state_mark = OwnerStateMark;

    std::size_t first = 0;
    std::size_t count = 0;

    [[nodiscard]] static constexpr CheckState emptied(CheckState const& /*state*/) noexcept { return CheckState{}; }
    static constexpr void release(CheckState const& /*state*/) noexcept {}
};

// A state of the home with the marker that is not trivially copyable.
struct CheckStateWithCopy {
    using owner_state_mark = OwnerStateMark;

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

// A state with the marker and the two functions of the core, in a
// namespace that is not a home.
struct StateOutsideHome {
    using owner_state_mark = OwnerStateMark;

    std::size_t first = 0;

    [[nodiscard]] static constexpr StateOutsideHome emptied(StateOutsideHome const& /*state*/) noexcept {
        return StateOutsideHome{};
    }
    static constexpr void release(StateOutsideHome const& /*state*/) noexcept {}
};

// A record with the two functions of the core and no marker.
struct RecordWithNoMark {
    std::size_t first = 0;

    [[nodiscard]] static constexpr RecordWithNoMark emptied(RecordWithNoMark const& /*state*/) noexcept {
        return RecordWithNoMark{};
    }
    static constexpr void release(RecordWithNoMark const& /*state*/) noexcept {}
};

// A record with a marker of a different type.
struct RecordWithOtherMark {
    using owner_state_mark = int;

    std::size_t first = 0;

    [[nodiscard]] static constexpr RecordWithOtherMark emptied(RecordWithOtherMark const& /*state*/) noexcept {
        return RecordWithOtherMark{};
    }
    static constexpr void release(RecordWithOtherMark const& /*state*/) noexcept {}
};

// A record with the marker and no release.
struct MarkedRecordWithNoRelease {
    using owner_state_mark = OwnerStateMark;

    std::size_t first = 0;

    [[nodiscard]] static constexpr MarkedRecordWithNoRelease
    emptied(MarkedRecordWithNoRelease const& /*state*/) noexcept {
        return MarkedRecordWithNoRelease{};
    }
};

// An owner on the core, with the moves of the core.  It is the Owner of its
// core, so it takes the key of its run.
struct CheckOwner final : public OwnerCore<owner::CheckState, CheckOwner> {
    CheckOwner() noexcept = default;
    CheckOwner(CheckOwner&&) noexcept = default;
    CheckOwner& operator=(CheckOwner&&) noexcept = default;

    [[nodiscard]] static constexpr OwnerRunKey key_of_run() noexcept { return run_key_(); }
};

// A second owner of the state of CheckOwner, as UnwrittenBytes is a second
// owner of the state of AlignedBytes.  Each owner of one state has a core of
// its own, and each takes the key of its run.
struct SecondCheckOwner final : public OwnerCore<owner::CheckState, SecondCheckOwner> {
    SecondCheckOwner() noexcept = default;
    SecondCheckOwner(SecondCheckOwner&&) noexcept = default;
    SecondCheckOwner& operator=(SecondCheckOwner&&) noexcept = default;

    [[nodiscard]] static constexpr OwnerRunKey key_of_run() noexcept { return run_key_(); }
};

// An owner on the core with a state outside each home.
struct OwnerOutsideHome final : public OwnerCore<StateOutsideHome, OwnerOutsideHome> {
    OwnerOutsideHome() noexcept = default;
    OwnerOutsideHome(OwnerOutsideHome&&) noexcept = default;
    OwnerOutsideHome& operator=(OwnerOutsideHome&&) noexcept = default;
};

// The core takes a state with the marker and the two functions from each
// namespace.  It refuses a record with no marker or a marker of another
// type, a state with no release, and a state that is not trivially
// copyable.
static_assert(OwnerState<owner::CheckState>);
static_assert(OwnerState<StateOutsideHome>);
static_assert(!OwnerState<RecordWithNoMark> && GivesOwnerOperations<RecordWithNoMark>);
static_assert(!OwnerState<RecordWithOtherMark> && GivesOwnerOperations<RecordWithOtherMark>);
static_assert(!OwnerState<MarkedRecordWithNoRelease> && MarksOwnerState<MarkedRecordWithNoRelease>);
static_assert(!OwnerState<owner::CheckStateWithCopy> && MarksOwnerState<owner::CheckStateWithCopy>);
static_assert(!OwnerState<int>);
static_assert(std::is_empty_v<OwnerStateMark>);

// The core is a public name of foundation::core, so an owner of each layer
// names it with no use of a detail namespace.
static_assert(std::meta::parent_of(^^OwnerCore) == ^^::foundation::core);

// The Owner of a core is a class of the base, and each owner of one state
// takes the key of its own run: the positive controls of the fixtures
// neg_core_owner_core_*.  A class outside the base is no Owner (the checks at
// the end of this file).
static_assert(OwnerOfTheBase<CheckOwner> && OwnerOfTheBase<SecondCheckOwner>);
static_assert(!OwnerOfTheBase<int> && !OwnerOfTheBase<owner::CheckState&>);
static_assert((static_cast<void>(CheckOwner::key_of_run()), true));
static_assert((static_cast<void>(SecondCheckOwner::key_of_run()), true));

// A class that is not the Owner of a core builds no core, moves no core and
// ends no core: each is private, and the Owner is the one friend.
static_assert(!std::is_default_constructible_v<OwnerCore<owner::CheckState, CheckOwner>>);
static_assert(!std::is_move_constructible_v<OwnerCore<owner::CheckState, CheckOwner>>);
static_assert(!std::is_destructible_v<OwnerCore<owner::CheckState, CheckOwner>>);
static_assert(std::is_default_constructible_v<CheckOwner> && std::is_destructible_v<SecondCheckOwner>);

// The core adds no byte to the state, and an owner on it moves and does not
// copy.
static_assert(sizeof(OwnerCore<owner::CheckState, CheckOwner>) == sizeof(owner::CheckState));
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

// A state with no statement can run code of an element, and a state that states
// it as false runs none.
struct StateWithNoCode {
    using owner_state_mark = OwnerStateMark;
    static constexpr bool runs_element_code = false;

    [[nodiscard]] static constexpr StateWithNoCode emptied(StateWithNoCode const& /*state*/) noexcept { return {}; }
    static constexpr void release(StateWithNoCode const& /*state*/) noexcept {}
};
struct StateWithCode {
    using owner_state_mark = OwnerStateMark;
    static constexpr bool runs_element_code = true;

    [[nodiscard]] static constexpr StateWithCode emptied(StateWithCode const& /*state*/) noexcept { return {}; }
    static constexpr void release(StateWithCode const& /*state*/) noexcept {}
};
static_assert(RunsElementCode<owner::CheckState> && RunsElementCode<StateWithCode>);
static_assert(!RunsElementCode<StateWithNoCode>);

// A bare owner is its core and nothing more.  An owner with a member, with a
// move or an end of its own, and a class on the core of another owner are none.
struct OwnerWithMember final : public OwnerCore<owner::CheckState, OwnerWithMember> {
    OwnerWithMember() noexcept = default;
    OwnerWithMember(OwnerWithMember&&) noexcept = default;
    OwnerWithMember& operator=(OwnerWithMember&&) noexcept = default;

    std::size_t held = 0;
};
struct OwnerWithMove final : public OwnerCore<owner::CheckState, OwnerWithMove> {
    OwnerWithMove() noexcept = default;
    OwnerWithMove(OwnerWithMove&& other) noexcept : OwnerCore{static_cast<OwnerCore&&>(other)} {}
    OwnerWithMove& operator=(OwnerWithMove&&) noexcept = default;
};
struct OwnerWithEnd final : public OwnerCore<owner::CheckState, OwnerWithEnd> {
    OwnerWithEnd() noexcept = default;
    OwnerWithEnd(OwnerWithEnd&&) noexcept = default;
    OwnerWithEnd& operator=(OwnerWithEnd&&) noexcept = default;
    ~OwnerWithEnd() { static_cast<void>(sizeof(*this)); }
};
struct ClassOnAnotherCore : public OwnerCore<owner::CheckState, CheckOwner> {};
static_assert(BareOwner<CheckOwner> && BareOwner<SecondCheckOwner> && BareOwner<OwnerOutsideHome>);
static_assert(!BareOwner<OwnerWithMember> && !BareOwner<OwnerWithMove> && !BareOwner<OwnerWithEnd>);
static_assert(!BareOwner<ClassOnAnotherCore> && !BareOwner<owner::CheckState> && !BareOwner<int>);

// The home holds the two states above and the states of each owner header
// that the unit includes, because the walk units compile this file after
// each header.  So the checks compare counts and pin no total: CheckOwner
// covers one state, a second CheckOwner is one stale owner, and an owner
// with a state outside the home is one stale owner.
using CheckHomes = OwnerHomes<^^owner>;
inline constexpr std::size_t kGapsWithNoOwner = ::foundation::core::count_owner_roster_gaps<CheckHomes>();
static_assert(kGapsWithNoOwner >= 2);
static_assert(::foundation::core::count_owner_roster_gaps<CheckHomes, CheckOwner>() + 1 == kGapsWithNoOwner);
static_assert(::foundation::core::count_owner_roster_gaps<CheckHomes, CheckOwner, CheckOwner>() == kGapsWithNoOwner);
static_assert(::foundation::core::count_owner_roster_gaps<CheckHomes, OwnerOutsideHome>() == kGapsWithNoOwner + 1);
static_assert(OwnerHomeList<CheckHomes> && OwnerHomeList<OwnerHomes<>> && !OwnerHomeList<int>);
static_assert(CheckHomes::count == 1 && OwnerHomes<>::count == 0);

// The stand-in roster has four states in two homes.  Three owners cover
// three of them, and four owners are stale: a second owner of one state, an
// owner with a move of its own, an owner with no core, and an owner with a
// state outside each home.  The verdict must count exactly that, so it can
// go red.
namespace standin_states {

struct StateA {};

template <class T>
struct StateB {};

struct StateC {};

}  // namespace standin_states

namespace standin_states_of_second_layer {

struct StateD {};

}  // namespace standin_states_of_second_layer

namespace standin_states_outside {

struct StateE {};

}  // namespace standin_states_outside

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

struct CoveredD : Core<standin_states_of_second_layer::StateD> {};

struct Outside : Core<standin_states_outside::StateE> {};

}  // namespace standin_owners

inline constexpr OwnerRosterVerdict kStandinVerdict =
    verdict_of_owner_roster<OwnerHomes<^^standin_states, ^^standin_states_of_second_layer>, ^^standin_owners::Core,
                            standin_owners::CoveredA, standin_owners::CoveredB, standin_owners::SecondA,
                            standin_owners::OwnMove, standin_owners::NoCore, standin_owners::CoveredD,
                            standin_owners::Outside>();

static_assert(kStandinVerdict.roster == 4, "the stand-in roster did not reflect the four states of its two homes");
static_assert(kStandinVerdict.covered == 3, "the verdict covered a state that no owner covers");
static_assert(kStandinVerdict.stale == 4,
              "the verdict missed a second owner of one state, an owner with a move of its own, an owner with no "
              "core, or an owner with a state outside each home");

// With one home, the state of the second layer is outside each home, so its
// owner is stale and its state is no member of the roster.
inline constexpr OwnerRosterVerdict kOneHomeVerdict =
    verdict_of_owner_roster<OwnerHomes<^^standin_states>, ^^standin_owners::Core, standin_owners::CoveredA,
                            standin_owners::CoveredD>();
static_assert(kOneHomeVerdict.roster == 3 && kOneHomeVerdict.covered == 1 && kOneHomeVerdict.stale == 1);

}  // namespace owner_checks

}  // namespace foundation::core::detail

// A class outside the namespaces foundation and fixy is no Owner of a core,
// also when it is a class of a namespace with one of the two names inside
// another namespace.
namespace owner_gate_checks {

struct OwnerOutsideTheBase final {};

namespace foundation {

struct OwnerOfAnInnerFoundation final {};

}  // namespace foundation

}  // namespace owner_gate_checks

static_assert(!::foundation::core::OwnerOfTheBase<owner_gate_checks::OwnerOutsideTheBase>);
static_assert(!::foundation::core::OwnerOfTheBase<owner_gate_checks::foundation::OwnerOfAnInnerFoundation>);
