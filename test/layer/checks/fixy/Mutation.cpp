// The compile-time checks of fixy/Mutation.h.

#include <fixy/Mutation.h>

#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>

namespace fixy {

static_assert(sizeof(AppendOnly<char*>) == sizeof(::foundation::core::GrowList<char*>),
              "AppendOnly<char*> must collapse to its GrowList. The grade "
              "(Length{size_t}) is computed from c.size() rather than stored "
              "separately, which holds only while SeqPrefixLattice supplies a "
              "grade_of trait method.");
static_assert(sizeof(AppendOnly<std::uint64_t>) == sizeof(::foundation::core::GrowList<std::uint64_t>),
              "AppendOnly<uint64_t> must collapse to its GrowList. The grade is "
              "computed from c.size() rather than stored separately.");

static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
              "OrderedAppendOnly must collapse empty KeyFn/Cmp to zero layout cost");

namespace detail::log_checks {

using ::foundation::core::Alloc;
using ::foundation::core::View;

// An element that only moves, one whose move can throw, and a pinned one.
struct MoveOnly final {
    int value = 0;
    MoveOnly() noexcept = default;
    MoveOnly(MoveOnly&& other) noexcept : value{other.value} {}
};
struct ThrowingMove final {
    int value = 0;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};
struct MovedPin final : ::foundation::Pinned<MovedPin> {
    int value = 0;
    MovedPin() noexcept = default;
    MovedPin(MovedPin&& other) noexcept : ::foundation::Pinned<MovedPin>{}, value{other.value} {}
};
static_assert(AppendOnlyElement<int> && AppendOnlyElement<MoveOnly> && AppendOnlyElement<char*>);
static_assert(!AppendOnlyElement<ThrowingMove> && !AppendOnlyElement<MovedPin>);

// An append takes the allocation capability first, it does not throw, and a
// const log and a temporary log have none.
template <class Log, class Marker, class Item>
concept Appends = requires(Log&& log, Marker marker, Item&& item) {
    { static_cast<Log&&>(log).append(marker, static_cast<Item&&>(item)) } noexcept -> std::same_as<void>;
};
struct OtherTag final {};
static_assert(Appends<AppendOnly<int>&, Alloc, int> && Appends<AppendOnly<MoveOnly>&, Alloc, MoveOnly>);
static_assert(Appends<OrderedAppendOnly<int>&, Alloc, int>);
static_assert(!Appends<AppendOnly<int>&, OtherTag, int> && !Appends<OrderedAppendOnly<int>&, OtherTag, int>);
static_assert(!Appends<AppendOnly<int> const&, Alloc, int> && !Appends<AppendOnly<int>, Alloc, int>);

// A log gives a plain element by value, and no iterator, no reference and no
// View out of a frame.  Each other element is read in a frame.
template <class Log, class T>
concept ReadsByValue = requires(Log const& log) {
    { log[std::size_t{0}] } noexcept -> std::same_as<T>;
    { log.front() } noexcept -> std::same_as<T>;
    { log.back() } noexcept -> std::same_as<T>;
};
template <class Log>
concept Iterates = requires(Log const& log) { log.begin(); };
static_assert(ReadsByValue<AppendOnly<int>, int> && ReadsByValue<OrderedAppendOnly<int>, int>);
static_assert(!ReadsByValue<AppendOnly<MoveOnly>, MoveOnly> && !ReadsByValue<AppendOnly<char*>, char*>);
static_assert(!Iterates<AppendOnly<int>> && !Iterates<OrderedAppendOnly<int>>);

struct CountsElements final {
    [[nodiscard]] constexpr std::size_t operator()(View<int const> held) const noexcept { return held.size(); }
};
struct GivesElements final {
    [[nodiscard]] constexpr View<int const> operator()(View<int const> held) const noexcept { return held; }
};
struct KeepsElements final {
    View<int const>& kept;
    void operator()(View<int const> held) const noexcept { kept = held; }
};
template <class Log, class Body>
concept Lends = requires(Log&& log, Body body) { static_cast<Log&&>(log).with_view(static_cast<Body&&>(body)); };
static_assert(Lends<AppendOnly<int>&, CountsElements> && Lends<AppendOnly<int> const&, CountsElements>);
static_assert(Lends<OrderedAppendOnly<int>&, CountsElements>);
static_assert(!Lends<AppendOnly<int>&, GivesElements> && !Lends<AppendOnly<int>&, KeepsElements>);
static_assert(!Lends<OrderedAppendOnly<int>&, GivesElements> && !Lends<OrderedAppendOnly<int>&, KeepsElements>);
static_assert(!Lends<AppendOnly<int>, CountsElements> && !Lends<OrderedAppendOnly<int>, CountsElements>);

// The store check reads the elements of a log, and no slot of its storage.  So
// a body that reaches a log of plain elements stores no borrow, and a log of a
// borrow is a borrow slot.
struct AppendsToLog final {
    AppendOnly<int>& log;
};
struct AppendsToOrderedLog final {
    OrderedAppendOnly<int>& log;
};
struct AppendsAView final {
    AppendOnly<View<int const>>& log;
};
static_assert(::foundation::core::StoresNoBorrowOutside<AppendsToLog>);
static_assert(::foundation::core::StoresNoBorrowOutside<AppendsToOrderedLog>);
static_assert(!::foundation::core::StoresNoBorrowOutside<AppendsAView>);

}  // namespace detail::log_checks

static_assert(sizeof(Monotonic<uint32_t, std::less<uint32_t>>) == sizeof(uint32_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: value and grade have the "
              "same type and collapse to one storage cell.");
static_assert(sizeof(Monotonic<uint64_t, std::less<uint64_t>>) == sizeof(uint64_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: value and grade have the "
              "same type and collapse to one storage cell.");

static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t),
              "BoundedMonotonic must collapse to underlying T");
static_assert(!std::is_trivially_copyable_v<BoundedMonotonic<std::uint32_t, 1024U>>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<BoundedMonotonic<std::uint32_t, 1024U>>,
              "std::bit_cast and std::start_lifetime_as must not build a counter above its bound");

// A constant evaluation moves a bounded counter back inside its bound, and the
// counter advances again from there.
consteval uint32_t bounded_counter_after_reset() {
    auto counter = mint_bounded_monotonic<uint32_t, 8U>(8U);
    counter.reset_under_quiescence(3U);
    counter.bump();
    return counter.get();
}
static_assert(bounded_counter_after_reset() == 4U);

// An order with no known direction gives a cell no bump.  test_mutation
// bumps each cell under each of the two orders that have a direction.
namespace detail::mutation_checks {
struct ByLowBit {
    [[nodiscard]] constexpr bool operator()(uint32_t left, uint32_t right) const noexcept {
        return (left & 1U) < (right & 1U);
    }
};
template <typename Cell>
concept Bumps = requires(Cell& cell) { cell.bump(); };
static_assert(Bumps<Monotonic<uint32_t>>);
static_assert(!Bumps<Monotonic<uint32_t, ByLowBit>>);
static_assert(Bumps<BoundedMonotonic<uint32_t, 8U>>);
static_assert(!Bumps<BoundedMonotonic<uint32_t, 8U, ByLowBit>>);
static_assert(!Bumps<AtomicMonotonic<uint32_t, void, ByLowBit>>);
}  // namespace detail::mutation_checks

static_assert(sizeof(WriteOnceNonNull<int*>) == sizeof(int*));
// The pointee of a slot is an object type, so the mint refuses a void pointee.
static_assert(!SlotPointer<void*>);

static_assert(alignof(AtomicMonotonic<uint64_t>) >= 64, "AtomicMonotonic must be cache-line aligned: repeated "
                                                        "advance/bump/CAS traffic invalidates the consumer's "
                                                        "cached line every iteration, so the counter must not "
                                                        "share a line with unrelated embedder state.");
static_assert(alignof(AtomicMonotonic<uint32_t>) >= 64);
static_assert(sizeof(AtomicMonotonic<uint64_t>) >= 64, "AtomicMonotonic occupies a full cache line by "
                                                       "construction; embedders rely on the counter NOT "
                                                       "sharing a line with any field touched on the "
                                                       "producer/consumer hot path.");
static_assert(sizeof(AtomicMonotonic<uint32_t>) >= 64);

}  // namespace fixy
