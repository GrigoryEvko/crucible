// The compile-time checks of foundation/core/SegmentList.h.

#include <foundation/core/SegmentList.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/SmallList.h>
#include <foundation/core/Verbs.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Annotation.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::segment_list_checks {

// A linear element: it moves, it does not copy, and the seal makes its
// assignment user-provided, so it is not trivially copyable.
struct[[= ::foundation::linear::linear{}]] LinearToken final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr explicit LinearToken(int from) noexcept : id{from} {}
    constexpr LinearToken(LinearToken&& other) noexcept : id{other.id} {}
    constexpr LinearToken& operator=(LinearToken&& other) noexcept {
        id = other.id;
        return *this;
    }
    LinearToken(LinearToken const&) = delete;
};

// An element whose move can throw, and one that does not move: each comes
// only through append_with.
struct ThrowingMove final {
    int value = 0;
    ThrowingMove() noexcept = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};
struct Fixed final {
    int value = 0;
    Fixed() noexcept = default;
    Fixed(Fixed const&) = delete;
    Fixed& operator=(Fixed const&) = delete;
};

// A pinned element that writes a move of its own.  Its address is its
// identity, so append refuses it.
struct MovedPin final : ::foundation::Pinned<MovedPin> {
    int value = 0;
    MovedPin() noexcept = default;
    MovedPin(MovedPin&& other) noexcept : ::foundation::Pinned<MovedPin>{}, value{other.value} {}
};

// An element of 72 bytes.
struct Event final {
    FixedArray<std::uint64_t, 9> words{};
};

// The directory, the tail, its end and the count, with no other byte.  The
// list moves and does not copy, and its state is an owner state of the core.
static_assert(sizeof(SegmentList<int>) == 35 * sizeof(std::size_t));
static_assert(sizeof(SegmentList<Event>) == 35 * sizeof(std::size_t));
static_assert(std::is_nothrow_default_constructible_v<SegmentList<int>>);
static_assert(std::is_nothrow_move_constructible_v<SegmentList<int>>
              && std::is_nothrow_move_assignable_v<SegmentList<int>>);
static_assert(!std::is_copy_constructible_v<SegmentList<int>> && !std::is_copy_assignable_v<SegmentList<int>>);
static_assert(OwnerState<owner::SegmentListState<int>> && OwnerState<owner::SegmentListState<LinearToken>>);
static_assert(::foundation::linear::is_linear_v<SegmentList<LinearToken>>);
static_assert(!::foundation::linear::is_linear_v<SegmentList<int>>);
static_assert(!std::is_move_assignable_v<SegmentList<LinearToken>>);

// The release of a list ends each element, so the end of a list runs code of a
// caller only when the end of its element does (the code of an element of
// foundation/core/Owner.h).  A list is its core and nothing more, so its move
// runs no such code.
struct ElementWithEnd final {
    int value = 0;
    ElementWithEnd() noexcept = default;
    ElementWithEnd(ElementWithEnd&&) noexcept = default;
    ~ElementWithEnd() {}
};
static_assert(BareOwner<SegmentList<int>> && BareOwner<SegmentList<ElementWithEnd>>);
static_assert(!EndRunsCode<SegmentList<int>> && !EndRunsCode<SegmentList<Box<int>>>);
static_assert(!EndRunsCode<SegmentList<SegmentList<int>>> && !EndRunsCode<Box<SegmentList<int>>>);
static_assert(EndRunsCode<SegmentList<ElementWithEnd>> && !MoveRunsCode<SegmentList<ElementWithEnd>>);

// A list holds each owned element, also one that does not move, and it
// refuses an array, a const element and a reference.
template <class T>
concept ListOf = requires { typename SegmentList<T>; };
static_assert(ListOf<int> && ListOf<Fixed> && ListOf<Atomic<int>> && ListOf<MovedPin> && ListOf<LinearToken>);
static_assert(ListOf<SegmentList<int>> && ListOf<Box<int>>);
static_assert(!ListOf<int const> && !ListOf<int[2]> && !ListOf<int&>);

// Segment 0 holds the least power of two of slots whose bytes are at least
// 256, and at least one slot.
static_assert(first_segment_slots_<char> == 256 && first_segment_slots_<std::uint64_t> == 32);
static_assert(first_segment_slots_<Event> == 4 && first_segment_slots_<char[255]> == 2);
static_assert(first_segment_slots_<char[256]> == 1 && first_segment_slots_<char[257]> == 1);

// The place of each position of the first five segments: the offsets run
// from zero to the slots of the segment, and the next position opens the next
// segment.
template <class T>
[[nodiscard]] consteval bool places_run_in_order() {
    std::size_t position = 0;
    for (std::size_t segment = 0; segment != 5; ++segment) {
        for (std::size_t offset = 0; offset != segment_slots_<T>(segment); ++offset) {
            SegmentPlace const place = segment_place_<T>(position);
            if (place.segment != segment || place.offset != offset) return false;
            ++position;
        }
    }
    return true;
}
static_assert(places_run_in_order<char>() && places_run_in_order<std::uint64_t>() && places_run_in_order<Event>());
static_assert(places_run_in_order<char[256]>() && places_run_in_order<char[255]>());

// The last position of the directory is the last slot of segment 31, and the
// next position is past the directory, where an append ends the process.  The
// slots of the directory have at least one terabyte.
template <class T>
[[nodiscard]] consteval bool directory_ends_at_its_limit() {
    std::size_t slots = 0;
    for (std::size_t segment = 0; segment != segment_limit_; ++segment) {
        slots += segment_slots_<T>(segment);
    }
    if (slots != directory_slots_<T>) return false;
    SegmentPlace const last = segment_place_<T>(slots - 1);
    SegmentPlace const past = segment_place_<T>(slots);
    return last.segment == segment_limit_ - 1 && last.offset == segment_slots_<T>(segment_limit_ - 1) - 1
        && past.segment == segment_limit_ && past.offset == 0 && slots >= (std::size_t{1} << 40) / sizeof(T) - 256;
}
static_assert(directory_ends_at_its_limit<char>() && directory_ends_at_its_limit<std::uint64_t>());
static_assert(directory_ends_at_its_limit<Event>() && directory_ends_at_its_limit<char[300]>());

// A window gives its first elements and the elements after a count, and
// neither goes past the window.
static_assert(SegmentWindow{3, 5}.limited(2).first == 3 && SegmentWindow{3, 5}.limited(2).count == 2);
static_assert(SegmentWindow{3, 5}.limited(9).count == 5);
static_assert(SegmentWindow{3, 5}.skipped(2).first == 5 && SegmentWindow{3, 5}.skipped(2).count == 3);
static_assert(SegmentWindow{3, 5}.skipped(9).first == 8 && SegmentWindow{3, 5}.skipped(9).count == 0);

// An append takes the allocation capability first, a value that moves with no
// throw, and a list that is no temporary and not const.
template <class List, class Marker, class Value>
concept Appends = requires(List&& list, Marker marker, Value value) {
    static_cast<List&&>(list).append(marker, static_cast<Value&&>(value));
};
struct OtherTag final {};
static_assert(Appends<SegmentList<int>&, Alloc, int> && !Appends<SegmentList<int>&, OtherTag, int>);
static_assert(!Appends<SegmentList<int>, Alloc, int> && !Appends<SegmentList<int> const&, Alloc, int>);
static_assert(Appends<SegmentList<LinearToken>&, Alloc, LinearToken>);
static_assert(!Appends<SegmentList<ThrowingMove>&, Alloc, ThrowingMove>);
static_assert(!Appends<SegmentList<MovedPin>&, Alloc, MovedPin>);
static_assert(!MovedSegmentElement<Fixed> && !MovedSegmentElement<Atomic<int>> && !MovedSegmentElement<MovedPin>);

// append_with takes a make that gives a T for the position with no throw, and
// no type that converts to T.  The rule borrow-match reads a make with a
// parameter, so a make with none is refused.
template <class List, class Marker, class Make>
concept Builds =
    requires(List&& list, Marker marker, Make make) { static_cast<List&&>(list).append_with(marker, make); };
struct MakesFixed final {
    [[nodiscard]] Fixed operator()(std::size_t) const noexcept { return Fixed{}; }
};
struct MakesCell final {
    [[nodiscard]] Atomic<int> operator()(std::size_t) const noexcept { return Atomic<int>{1}; }
};
struct MakesShort final {
    [[nodiscard]] short operator()(std::size_t) const noexcept { return 1; }
};
struct MakesWithThrow final {
    [[nodiscard]] int operator()(std::size_t position) const noexcept(false);
};
struct MakesInt final {
    [[nodiscard]] int operator()(std::size_t) const noexcept { return 1; }
};
struct MakesFromNothing final {
    [[nodiscard]] int operator()() const noexcept { return 1; }
};
static_assert(Builds<SegmentList<Fixed>&, Alloc, MakesFixed> && Builds<SegmentList<Atomic<int>>&, Alloc, MakesCell>);
static_assert(Builds<SegmentList<int>&, Alloc, MakesInt> && !Builds<SegmentList<int>&, OtherTag, MakesInt>);
static_assert(!Builds<SegmentList<int>&, Alloc, MakesShort> && !Builds<SegmentList<int>&, Alloc, MakesWithThrow>);
static_assert(!Builds<SegmentList<int>&, Alloc, MakesFromNothing>);
static_assert(!Builds<SegmentList<int>, Alloc, MakesInt> && !Builds<SegmentList<int> const&, Alloc, MakesInt>);

// Each read lends only from a list that is no temporary, and it gives a
// const element.
template <class List>
concept GetsConst = requires(List&& list) {
    { *static_cast<List&&>(list).get(0).expect("an element") } -> std::same_as<int const&>;
};
template <class List>
concept GivesLast = requires(List&& list) { static_cast<List&&>(list).last(); };
template <class List>
concept Loops = requires(List&& list) {
    static_cast<List&&>(list).begin();
    static_cast<List&&>(list).end();
};
template <class List>
concept Walks = requires(List&& list) { static_cast<List&&>(list).walk(); };
template <class List>
concept WalksSegments = requires(List&& list) { static_cast<List&&>(list).segments(); };
static_assert(GetsConst<SegmentList<int>&> && GetsConst<SegmentList<int> const&> && !GetsConst<SegmentList<int>>);
static_assert(GivesLast<SegmentList<int>&> && !GivesLast<SegmentList<int>> && !GivesLast<SegmentList<int> const>);
static_assert(Loops<SegmentList<int>&> && Loops<SegmentList<int> const&> && !Loops<SegmentList<int>>);
static_assert(Walks<SegmentList<int>&> && !Walks<SegmentList<int>> && !Walks<SegmentList<int> const>);
static_assert(WalksSegments<SegmentList<int>&> && !WalksSegments<SegmentList<int>>);

// A list has no subscript, no view of one run, and no door that ends an
// element before the list ends.
template <class List>
concept HasSubscript = requires(List& list) { list[0]; };
template <class List>
concept Views = requires(List& list) { list.view(); };
template <class List>
concept Takes = requires(List& list) { list.take_last(); };
template <class List>
concept Clears = requires(List& list) { list.clear(); };
static_assert(!HasSubscript<SegmentList<int>> && !Views<SegmentList<int>> && !Takes<SegmentList<int>>);
static_assert(!Clears<SegmentList<int>> && Clears<SmallList<int, 2>> && Takes<SmallList<int, 2>>);

// A drain takes each element by value with no throw.  A body that takes a
// reference would drop a linear element, and a list that does not move its
// elements has no drain.
template <class List, class Body>
concept Drains = requires(List&& list, Body body) { static_cast<List&&>(list).drain(body); };
struct TakesInt final {
    void operator()(int) noexcept {}
};
struct TakesReference final {
    void operator()(int&&) noexcept {}
};
struct TakesWithThrow final {
    void operator()(int value) noexcept(false);
};
struct TakesToken final {
    void operator()(LinearToken token) noexcept { ::foundation::linear::drop(static_cast<LinearToken&&>(token)); }
};
struct TakesFixed final {
    void operator()(Fixed) noexcept {}
};
static_assert(Drains<SegmentList<int>, TakesInt> && !Drains<SegmentList<int>&, TakesInt>);
static_assert(!Drains<SegmentList<int>, TakesReference> && !Drains<SegmentList<int>, TakesWithThrow>);
static_assert(Drains<SegmentList<LinearToken>, TakesToken> && !Drains<SegmentList<Fixed>, TakesFixed>);

// Two threads read one list at the same time when the element permits it, and
// a list moves to another thread when its element does.
static_assert(::foundation::transfer::Sendable<SegmentList<int>>
              && ::foundation::transfer::Shareable<SegmentList<int>>);
static_assert(::foundation::transfer::Sendable<SegmentList<int> const&>);
static_assert(!::foundation::transfer::Sendable<SegmentList<int>&>);
static_assert(!::foundation::transfer::Sendable<SegmentList<int*>>
              && !::foundation::transfer::Shareable<SegmentList<int*>>);
static_assert(::foundation::transfer::SendableOwned<SegmentList<int>>);

// Each list states that it keeps its blocks.  A list of elements that hold no
// borrow also states that it owns its storage, and a list of elements that hold
// one does not.  An owner holds a borrow when its object does.
struct HoldsRun final {
    View<int const> run{};
};
struct[[= ::foundation::transfer::not_sendable{}]] StaysOnItsThread final {
    int value = 0;
};
template <class T>
concept StatesItsStorage = ::foundation::reflect::carries_annotation(^^SegmentList<T>, ^^owns_its_storage);
static_assert(StatesItsStorage<int> && StatesItsStorage<Event> && StatesItsStorage<LinearToken>);
static_assert(StatesItsStorage<Box<int>> && StatesItsStorage<SegmentList<int>> && StatesItsStorage<Atomic<int>>);
static_assert(!StatesItsStorage<View<int const>> && !StatesItsStorage<int*> && !StatesItsStorage<HoldsRun>);
static_assert(!StatesItsStorage<Box<View<int const>>> && !StatesItsStorage<SegmentList<View<int const>>>);
static_assert(!StatesItsStorage<StaysOnItsThread>);
template <class T>
concept KeepsItsBlocks = ::foundation::reflect::carries_annotation(^^SegmentList<T>, ^^keeps_its_blocks);
static_assert(KeepsItsBlocks<int> && KeepsItsBlocks<LinearToken> && KeepsItsBlocks<Box<int>>);
static_assert(KeepsItsBlocks<View<int const>> && KeepsItsBlocks<int*> && KeepsItsBlocks<HoldsRun>);
static_assert(KeepsItsBlocks<Box<View<int const>>> && KeepsItsBlocks<SegmentList<View<int const>>>);
static_assert(KeepsItsBlocks<StaysOnItsThread>);

// The surface of the list is closed over the shared verbs and the names of
// the family.
inline constexpr TextView segment_list_names{"element_type append_with last segments drain expect_empty"};
CRUCIBLE_CLOSED_SURFACE(segment_list_names, SegmentList<int>);
CRUCIBLE_CLOSED_SURFACE(segment_list_names, SegmentList<LinearToken>);
CRUCIBLE_CLOSED_SURFACE(segment_list_names, SegmentList<Atomic<int>>);
inline constexpr TextView segment_cursor_names{""};
CRUCIBLE_CLOSED_SURFACE(segment_cursor_names, SegmentCursor<int>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(segment_cursor_names, SegmentRunCursor<int>, CursorProtocol);

// The empty list in a constant evaluation: it holds no element, gives no
// element, and its loops and walks run no step.  The allocation door is no
// constexpr function, so the evaluation proves that an empty list does not
// reach it.
[[nodiscard]] consteval bool empty_list_holds_nothing() {
    SegmentList<int> list{};
    std::size_t steps = 0;
    for (int const& element : list) {
        static_cast<void>(element);
        ++steps;
    }
    for (View<int const> run : list.segments()) {
        static_cast<void>(run);
        ++steps;
    }
    SegmentList<int> moved{std::move(list)};
    bool const is_empty = moved.size() == 0 && moved.get(0).is_none() && moved.last().is_none() && steps == 0
                       && moved.walk().size() == 0 && moved.segments().size() == 0;
    std::move(moved).expect_empty("the moved list holds no element");
    return is_empty;
}
static_assert(empty_list_holds_nothing());

}  // namespace detail::segment_list_checks

}  // namespace foundation::core
