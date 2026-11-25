// The compile-time checks of fixy/Mutation.h.

#include <fixy/Mutation.h>

#include <foundation/Pinned.h>
#include <foundation/Transfer.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>

namespace fixy {

static_assert(sizeof(AppendOnly<char*>) == sizeof(::foundation::core::SegmentList<char*>),
              "AppendOnly<char*> must collapse to its SegmentList. The grade "
              "(Length{size_t}) is computed from c.size() rather than stored "
              "separately, which holds only while SeqPrefixLattice supplies a "
              "grade_of trait method.");
static_assert(sizeof(AppendOnly<std::uint64_t>) == sizeof(::foundation::core::SegmentList<std::uint64_t>),
              "AppendOnly<uint64_t> must collapse to its SegmentList. The grade is "
              "computed from c.size() rather than stored separately.");

static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
              "OrderedAppendOnly must collapse empty KeyFn/Cmp to zero layout cost");

namespace detail::log_checks {

using ::foundation::core::Alloc;
using ::foundation::core::View;

// An element that only moves, one whose move can throw, and a pinned one.  Each
// of them is an element of a log.  Only the first one moves into it, so an
// ordered log takes only that one.
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
static_assert(AppendOnlyElement<ThrowingMove> && AppendOnlyElement<MovedPin>);
static_assert(!AppendOnlyElement<int const> && !AppendOnlyElement<int[2]> && !AppendOnlyElement<int&>);
static_assert(OrderedLogElement<int> && OrderedLogElement<MoveOnly>);
static_assert(!OrderedLogElement<ThrowingMove> && !OrderedLogElement<MovedPin>);

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
static_assert(!Appends<AppendOnly<MovedPin>&, Alloc, MovedPin>);
static_assert(!Appends<AppendOnly<ThrowingMove>&, Alloc, ThrowingMove>);

// append_with builds each element in place.  An ordered log has no such append,
// because it reads the key of an item before the item goes into the log.
template <class Log, class Marker, class Make>
concept Builds = requires(Log&& log, Marker marker, Make make) {
    { static_cast<Log&&>(log).append_with(marker, make) } noexcept -> std::same_as<void>;
};
struct MakesPin final {
    [[nodiscard]] MovedPin operator()(std::size_t) const noexcept { return MovedPin{}; }
};
struct MakesInt final {
    [[nodiscard]] int operator()(std::size_t) const noexcept { return 1; }
};
static_assert(Builds<AppendOnly<MovedPin>&, Alloc, MakesPin> && Builds<AppendOnly<int>&, Alloc, MakesInt>);
static_assert(!Builds<AppendOnly<int>&, OtherTag, MakesInt> && !Builds<AppendOnly<int>, Alloc, MakesInt>);
static_assert(!Builds<OrderedAppendOnly<int>&, Alloc, MakesInt>);

// A log gives each element, and the list of its elements, as a const object.  A
// temporary log gives none, and no log gives a View of all its elements.
template <class Log, class T>
concept Reads = requires(Log&& log) {
    { static_cast<Log&&>(log)[std::size_t{0}] } noexcept -> std::same_as<T const&>;
    { static_cast<Log&&>(log).front() } noexcept -> std::same_as<T const&>;
    { static_cast<Log&&>(log).back() } noexcept -> std::same_as<T const&>;
    { static_cast<Log&&>(log).peek() } noexcept -> std::same_as<::foundation::core::SegmentList<T> const&>;
};
template <class Log>
concept Views = requires(Log const& log) { log.view(); };
static_assert(Reads<AppendOnly<int>&, int> && Reads<AppendOnly<int> const&, int>);
static_assert(Reads<AppendOnly<MoveOnly>&, MoveOnly> && Reads<AppendOnly<MovedPin>&, MovedPin>);
struct ValueOfMoveOnly final {
    [[nodiscard]] constexpr int operator()(MoveOnly const& item) const noexcept { return item.value; }
};
static_assert(Reads<OrderedAppendOnly<int>&, int>
              && Reads<OrderedAppendOnly<MoveOnly, ValueOfMoveOnly> const&, MoveOnly>);
static_assert(!Reads<AppendOnly<int>, int> && !Reads<AppendOnly<int> const, int>);
static_assert(!Reads<OrderedAppendOnly<int>, int> && !Views<AppendOnly<int>> && !Views<OrderedAppendOnly<int>>);

// Two threads can read one log when they can read its element.
static_assert(::foundation::transfer::Shareable<AppendOnly<int>>
              && ::foundation::transfer::Shareable<OrderedAppendOnly<int>>);
static_assert(!::foundation::transfer::Shareable<AppendOnly<char*>>);

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
