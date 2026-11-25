// The compile-time checks of foundation/core/LiveList.h.

#include <foundation/core/LiveList.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>
#include <foundation/core/Verbs.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Annotation.h>
#include <foundation/reflect/Members.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::live_list_checks {

// The one writer of the lists of the checks, another owner, and two roles.
class LogWriter;
class OtherWriter;
struct AppendRole final {};
struct OtherRole final {};
using Proof = ::foundation::SoleWriterProof<LogWriter, AppendRole>;
using OtherOwnerProof = ::foundation::SoleWriterProof<OtherWriter, AppendRole>;
using OtherRoleProof = ::foundation::SoleWriterProof<LogWriter, OtherRole>;

// An element of 72 bytes.
struct Event final {
    FixedArray<std::uint64_t, 9> words{};
};

// A linear element.
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

// An element that does not move, one whose move and copy can throw, and one
// that only moves.
struct Fixed final {
    int value = 0;
    Fixed() noexcept = default;
    Fixed(Fixed const&) = delete;
    Fixed& operator=(Fixed const&) = delete;
};
struct Throwing final {
    int value = 0;
    Throwing() noexcept = default;
    Throwing(Throwing const&) noexcept(false) {}
};
struct MovesOnly final {
    int value = 0;
    MovesOnly() noexcept = default;
    MovesOnly(MovesOnly&&) noexcept = default;
};

// An element that a read writes.
struct CountsItsReads final {
    mutable int reads = 0;
};

// An element that states that it moves to no other thread.
struct[[= ::foundation::transfer::not_sendable{}]] StaysOnItsThread final {
    int value = 0;
};

// The element of a list on other threads: it is read by two threads, it moves
// to another one, and it holds no borrow.
static_assert(LiveListElement<int> && LiveListElement<Event> && LiveListElement<Fixed>);
static_assert(LiveListElement<Atomic<int>> && LiveListElement<Box<int>> && LiveListElement<SegmentList<int>>);
static_assert(!LiveListElement<LinearToken> && !LiveListElement<SegmentList<LinearToken>>);
static_assert(!LiveListElement<CountsItsReads> && !LiveListElement<GrowText>);
static_assert(!LiveListElement<StaysOnItsThread> && !LiveListElement<Box<StaysOnItsThread>>);
static_assert(!LiveListElement<int*> && !LiveListElement<View<int const>> && !LiveListElement<Box<View<int const>>>);
static_assert(!LiveListElement<int const> && !LiveListElement<int[2]> && !LiveListElement<int&>);

// The writer of a list is the proof of the one writer, and no other token.
static_assert(LiveListWriter<Proof> && LiveListWriter<OtherOwnerProof>);
static_assert(!LiveListWriter<void> && !LiveListWriter<int> && !LiveListWriter<SoleWriter<AppendRole, OtherRole>>);

using Live = LiveList<Event, Proof>;
using Numbers = LiveList<int, Proof>;

// The four lines of the directory, the line of the words of the writer, and the
// line of the published count.  The words of the writer are the tail, its end
// and the count of a SegmentList.
template <class Name>
[[nodiscard]] consteval std::size_t offset_of_part(std::meta::info type, Name const& name) {
    return static_cast<std::size_t>(
        std::meta::offset_of(::foundation::reflect::member_named_in(type, std::meta::access_context::unchecked(), name))
            .bytes);
}
static_assert(sizeof(Live) == 6 * 64 && alignof(Live) == 64);
static_assert(sizeof(Numbers) == 6 * 64 && alignof(Numbers) == 64);
static_assert(offset_of_part(^^Live, "segments_") == 0 && offset_of_part(^^Live, "published_") == 5 * 64);
static_assert(offset_of_part(^^owner::SegmentListState<Event>, "segments") == 0);
static_assert(offset_of_part(^^owner::SegmentListState<Event>, "tail") == 4 * 64);
static_assert(offset_of_part(^^owner::SegmentListState<Event>, "tail_end") == 4 * 64 + 8);
static_assert(offset_of_part(^^owner::SegmentListState<Event>, "count") == 4 * 64 + 16);
static_assert(sizeof(LiveSegments<Event, Proof>) == sizeof(SegmentList<Event>));

// A list does not copy, move or take an assignment.
static_assert(std::is_nothrow_default_constructible_v<Live>);
static_assert(!std::is_copy_constructible_v<Live> && !std::is_move_constructible_v<Live>);
static_assert(!std::is_copy_assignable_v<Live> && !std::is_move_assignable_v<Live>);
static_assert(!std::is_constructible_v<Live, SegmentList<Event>> && !std::is_trivially_copyable_v<Live>);

// An append takes the proof of the list first and the allocation capability
// second, a value that moves with no throw, and a list that is no temporary and
// not const.  A proof of another owner or of another role names another writer.
template <class List, class Proof, class Marker, class Value>
concept Appends = requires(List&& list, Proof const& proof, Marker marker, Value value) {
    static_cast<List&&>(list).append(proof, marker, static_cast<Value&&>(value));
};
template <class List, class Marker, class Value>
concept AppendsWithNoProof = requires(List&& list, Marker marker, Value value) {
    static_cast<List&&>(list).append(marker, static_cast<Value&&>(value));
};
struct OtherTag final {};
static_assert(Appends<Numbers&, Proof, Alloc, int> && !AppendsWithNoProof<Numbers&, Alloc, int>);
static_assert(!Appends<Numbers&, OtherOwnerProof, Alloc, int> && !Appends<Numbers&, OtherRoleProof, Alloc, int>);
static_assert(!Appends<Numbers&, Proof, OtherTag, int> && !Appends<Numbers const&, Proof, Alloc, int>);
static_assert(!Appends<Numbers, Proof, Alloc, int>);
static_assert(Appends<LiveList<Box<int>, Proof>&, Proof, Alloc, Box<int>>);
static_assert(Appends<LiveList<MovesOnly, Proof>&, Proof, Alloc, MovesOnly>);
static_assert(!Appends<LiveList<Fixed, Proof>&, Proof, Alloc, Fixed>);

// append_with takes a make that gives a T for the position with no throw, as a
// SegmentList does.
template <class List, class Proof, class Make>
concept Builds =
    requires(List&& list, Proof const& proof, Alloc marker, Make make) { list.append_with(proof, marker, make); };
struct MakesFixed final {
    [[nodiscard]] Fixed operator()(std::size_t) const noexcept { return Fixed{}; }
};
struct MakesShort final {
    [[nodiscard]] short operator()(std::size_t) const noexcept { return 1; }
};
struct MakesInt final {
    [[nodiscard]] int operator()(std::size_t) const noexcept { return 1; }
};
struct MakesWithThrow final {
    [[nodiscard]] int operator()(std::size_t position) const noexcept(false);
};
static_assert(Builds<LiveList<Fixed, Proof>&, Proof, MakesFixed> && Builds<Numbers&, Proof, MakesInt>);
static_assert(!Builds<Numbers&, Proof, MakesShort> && !Builds<Numbers&, Proof, MakesWithThrow>);
static_assert(!Builds<Numbers&, OtherOwnerProof, MakesInt>);

// append_each copies with no throw, so an element that only moves and one whose
// copy can throw have none.
template <class List, class Proof, class Element>
concept AppendsEach = requires(List&& list, Proof const& proof, Alloc marker, View<Element const> run) {
    list.append_each(proof, marker, run);
};
static_assert(AppendsEach<Numbers&, Proof, int> && !AppendsEach<Numbers&, OtherOwnerProof, int>);
static_assert(AppendsEach<Live&, Proof, Event> && !AppendsEach<Live const&, Proof, Event>);
static_assert(!AppendsEach<LiveList<MovesOnly, Proof>&, Proof, MovesOnly>);
static_assert(!AppendsEach<LiveList<Throwing, Proof>&, Proof, Throwing>);

// Each read is a read of a prefix.  A list gives no list and no element itself,
// and a temporary gives no prefix.
template <class List>
concept GivesPrefix = requires(List&& list) {
    { static_cast<List&&>(list).prefix() } noexcept -> std::same_as<SegmentPrefix<int const>>;
};
template <class List>
concept GetsElement = requires(List& list) { list.get(0); };
template <class List>
concept Loops = requires(List& list) { list.begin(); };
template <class List>
concept Drains = requires(List&& list) { static_cast<List&&>(list).drain([](int) noexcept {}); };
static_assert(GivesPrefix<Numbers&> && GivesPrefix<Numbers const&>);
static_assert(!GivesPrefix<Numbers> && !GivesPrefix<Numbers const>);
static_assert(!GetsElement<Numbers> && !Loops<Numbers> && !Drains<Numbers> && !Drains<Numbers&>);

// A prefix is two words.  It copies and takes an assignment, as a View does.
// Only a list builds one: it has no default value, and no bytes become one.
using Prefix = SegmentPrefix<Event const>;
static_assert(sizeof(Prefix) == 2 * sizeof(std::size_t));
static_assert(std::is_trivially_copy_constructible_v<Prefix> && std::is_nothrow_copy_assignable_v<Prefix>);
static_assert(!std::is_trivially_copyable_v<Prefix> && !std::is_default_constructible_v<Prefix>);
static_assert(!std::is_constructible_v<Prefix, owner::SegmentListState<Event> const&, std::size_t>);
static_assert(!std::is_constructible_v<Prefix, SegmentList<Event> const&>);
static_assert(!std::is_constructible_v<Prefix, Live const&>);
template <class Const>
concept PrefixOf = requires { typename SegmentPrefix<Const>; };
static_assert(PrefixOf<int const> && !PrefixOf<int> && !PrefixOf<int const[2]> && !PrefixOf<int const&>);
static_assert(std::same_as<decltype(*std::declval<Prefix>().get(0).expect("an element")), Event const&>);
static_assert(std::same_as<decltype(*std::declval<Prefix>().last().expect("an element")), Event const&>);

// Two threads write one list at the same time, so a thread beside the caller
// can hold a reference to it, const or not.  A list behind a Box moves to
// another thread.
static_assert(::foundation::transfer::Synchronized<Live> && ::foundation::transfer::Shareable<Live>);
static_assert(::foundation::transfer::Synchronized<LiveSegments<Event, Proof>>);
static_assert(::foundation::transfer::SendableBeside<Live&> && ::foundation::transfer::SendableBeside<Live const&>);
static_assert(::foundation::transfer::Sendable<Live&> && ::foundation::transfer::SendableOwned<Box<Live>>);
static_assert(::foundation::transfer::Synchronized<LiveList<Atomic<int>, Proof>>);

// The proof of the one writer is no token of the door check, and the key of the
// segments is one.  No code builds the key from nothing.
using Key = LiveSegments<Event, Proof>::WriteKey;
static_assert(::foundation::transfer::detail::is_door_token(std::meta::dealias(^^Key))
              && !::foundation::transfer::detail::is_door_token(std::meta::dealias(^^Proof)));
static_assert(!std::is_default_constructible_v<Key> && !std::is_copy_constructible_v<Key>);
static_assert(std::is_constructible_v<Key, Proof const&> && !std::is_constructible_v<Key, OtherOwnerProof const&>);

// A prefix refers to const elements as a View of const does.
static_assert(::foundation::transfer::Sendable<Prefix> && ::foundation::transfer::Shareable<Prefix>);
static_assert(!::foundation::transfer::SendableBeside<Prefix> && !::foundation::transfer::SendableOwned<Prefix>);
static_assert(!::foundation::transfer::Sendable<SegmentPrefix<CountsItsReads const>>);
static_assert(!HoldsNoBorrow<Prefix>);

// A call of a function with no body in the unit ends no borrow of a list, which
// states that it keeps its storage and which no code can move.  The rule
// view-escape reads the body, and nothing calls it.
static_assert(::foundation::reflect::carries_annotation(std::meta::dealias(^^Live), ^^owns_its_storage));
static_assert(!std::is_move_constructible_v<Live> && !std::is_move_assignable_v<Live>);
void change_out_of_line(Live& list) noexcept;
[[nodiscard]] inline std::size_t read_across_a_call(Live& list) noexcept {
    Prefix const prefix = list.prefix();
    auto const first = prefix.get(0).expect("the list holds an element");
    change_out_of_line(list);
    return prefix.size() + first->words.size();
}

// The surface of the list and of a prefix is closed over the shared verbs and
// the names of the family.
inline constexpr TextView live_list_names{"element_type writer_type append_with append_each prefix"};
CRUCIBLE_CLOSED_SURFACE(live_list_names, Numbers);
CRUCIBLE_CLOSED_SURFACE(live_list_names, LiveList<Atomic<int>, Proof>);
inline constexpr TextView segment_prefix_names{"element_type last segments"};
CRUCIBLE_CLOSED_SURFACE(segment_prefix_names, SegmentPrefix<int const>);
CRUCIBLE_CLOSED_SURFACE(segment_prefix_names, Prefix);

}  // namespace detail::live_list_checks

}  // namespace foundation::core
