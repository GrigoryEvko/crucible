// The compile-time checks of foundation/core/Array.h.

#include <foundation/core/Array.h>

#include <foundation/core/Atomic.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::array_checks {

struct NoDefault {
    int value;
    constexpr explicit NoDefault(int from) noexcept : value{from} {}
    [[nodiscard]] friend constexpr bool operator==(NoDefault, NoDefault) noexcept = default;
};

struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// An array holds its elements and nothing else, and it copies as its
// elements do.
static_assert(sizeof(FixedArray<std::uint32_t, 4>) == 4 * sizeof(std::uint32_t));
static_assert(sizeof(FixedArray<Record, 3>) == 3 * sizeof(Record));
static_assert(std::is_trivially_copyable_v<FixedArray<std::uint64_t, 2>>);
static_assert(std::is_trivially_destructible_v<FixedArray<Record, 3>>);
static_assert(FixedArray<int, 7>::size() == 7);

// An array has the layout and the alignment of its elements, so a class
// with a fixed layout can hold one, and an alignment specifier on an
// instance reaches the first element.
static_assert(std::is_standard_layout_v<FixedArray<std::int64_t, 5>>);
static_assert(alignof(FixedArray<std::int64_t, 8>) == alignof(std::int64_t));
static_assert(alignof(FixedArray<char, 16>) == 1);

// The gates: no zero count, no array, no const and no reference element.
template <class T, std::size_t N>
concept HasFixedArray = requires { typename FixedArray<T, N>; };
static_assert(HasFixedArray<int, 1> && HasFixedArray<NoDefault, 2>);
static_assert(!HasFixedArray<int, 0>);
static_assert(!HasFixedArray<int const, 2>);
static_assert(!HasFixedArray<int[2], 2>);
static_assert(!HasFixedArray<int&, 2>);

// The default constructor value-initializes each element.  The list
// constructor takes exactly N arguments, and its form of one argument is
// explicit.
static_assert(std::is_default_constructible_v<FixedArray<int, 4>>);
static_assert(!std::is_default_constructible_v<FixedArray<NoDefault, 2>>);
static_assert(std::is_constructible_v<FixedArray<int, 3>, int, int, int>);
static_assert(!std::is_constructible_v<FixedArray<int, 3>, int, int>);
static_assert(!std::is_constructible_v<FixedArray<int, 3>, int, int, int, int>);
static_assert(std::is_constructible_v<FixedArray<int, 1>, int>);
static_assert(!std::is_convertible_v<int, FixedArray<int, 1>>);
static_assert(std::is_constructible_v<FixedArray<NoDefault, 2>, NoDefault, NoDefault>);

// The list constructor exists up to max_listed_array_count elements.  A
// larger array has only the default constructor, so its declaration costs no
// parameter for each element.  The fixture neg_core_fixed_array_list_past_bound
// holds the refusal of a list of 257 values.
template <std::size_t... Positions>
[[nodiscard]] consteval FixedArray<int, sizeof...(Positions)> listed_array(std::index_sequence<Positions...>) noexcept {
    return FixedArray<int, sizeof...(Positions)>{static_cast<int>(Positions)...};
}
inline constexpr FixedArray<int, max_listed_array_count> longest_list =
    listed_array(std::make_index_sequence<max_listed_array_count>{});
static_assert(longest_list.at(index<max_listed_array_count - 1>()) == 255);
static_assert(std::is_nothrow_default_constructible_v<FixedArray<Record, 65536>>);
static_assert(!std::is_constructible_v<FixedArray<int, 257>, int>);

// An array of a nested class with default member initializers can be a
// member of the enclosing class, at each count.  The constraint of the default
// constructor sits on FixedArray alone, so the class of the storage reads no
// trait of the element before the compiler reads the initializers.
struct Enclosing {
    struct Nested {
        std::uint32_t count = 0;
        std::uint64_t value = 7;
    };
    FixedArray<Nested, 4> small{};
};
struct LargeEnclosing {
    struct Nested {
        std::uint32_t count = 0;
    };
    FixedArray<Nested, 65536> large{};
};
static_assert(std::is_nothrow_default_constructible_v<Enclosing>);
static_assert(std::is_nothrow_default_constructible_v<LargeEnclosing>);
[[nodiscard]] consteval std::uint64_t nested_value() noexcept {
    Enclosing const enclosing{};
    return enclosing.small.at(index<3>()).value;
}
static_assert(nested_value() == 7);

// No subscript, no pointer and no data() reach an element.
template <class Array>
concept HasSubscript = requires(Array array) { array[0]; };
template <class Array>
concept HasData = requires(Array array) { array.data(); };
static_assert(!HasSubscript<FixedArray<int, 4>&> && !HasData<FixedArray<int, 4>&>);
static_assert(!std::is_convertible_v<FixedArray<int, 4>&, int*>);

// view() of an lvalue gives a View of the const elements, of the fixed extent,
// also for an array that is not const.  A view, a cursor and an element of a
// temporary are refused.
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4>&>().view()), View<int const, 4>>);
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4> const&>().view()), View<int const, 4>>);
template <class Array>
concept ViewsTemporary = requires { std::declval<Array>().view(); };
template <class Array>
concept LoopsOverTemporary = requires { std::declval<Array>().begin(); };
template <class Array>
concept ReadsTemporary = requires { std::declval<Array>().at(std::declval<Index<4>>()); };
static_assert(!ViewsTemporary<FixedArray<int, 4>> && !ViewsTemporary<FixedArray<int, 4> const>);
static_assert(!LoopsOverTemporary<FixedArray<int, 4>> && !ReadsTemporary<FixedArray<int, 4>>);

// A keyed FixedArray lends its run in place through the member with_mut with
// its key, and it lends no run with no key.  An array of Unkeyed lends no run.
// The free frame door takes no FixedArray.
struct RunDoorBrand final {};
template <class Array>
concept LendsRun = requires(Array&& array) { static_cast<Array&&>(array).with_mut([](auto) noexcept {}); };
template <class Array>
concept LendsRunWithKey = requires(Array&& array, LoanKey<RunDoorBrand>& key) {
    static_cast<Array&&>(array).with_mut(key, [](auto) noexcept {});
};
using RunDoorInts = FixedArray<int, 4, RunDoorBrand>;
static_assert(LendsRunWithKey<RunDoorInts&> && !LendsRunWithKey<RunDoorInts const&> && !LendsRunWithKey<RunDoorInts>);
static_assert(!LendsRun<RunDoorInts&> && !LendsRun<FixedArray<int, 4>&> && !LendsRunWithKey<FixedArray<int, 4>&>);
static_assert(!LendsRun<FixedArray<int, 4> const&> && !LendsRun<FixedArray<int, 4>>);
static_assert(!ArrayFrameOwner<FixedArray<int, 4>> && ArrayFrameOwner<HeapArray<int>>);
// The element door with no key lends an element that a body reads, and an
// element that two threads can write at the same time.  A temporary lends
// nothing.
template <class Array>
concept ReadsElement =
    requires(Array&& array) { static_cast<Array&&>(array).with_element(index<0>(), [](int const&) noexcept {}); };
template <class Array, class Element>
concept WritesElement = requires(Array&& array, void (&body)(Element&) noexcept) {
    static_cast<Array&&>(array).with_element(index<0>(), body);
};
static_assert(ReadsElement<FixedArray<int, 4>&> && ReadsElement<FixedArray<int, 4> const&>
              && !ReadsElement<FixedArray<int, 4>>);
static_assert(!WritesElement<FixedArray<int, 4>&, int>);
static_assert(WritesElement<FixedArray<Atomic<int>, 4>&, Atomic<int>>);

// The value doors: an array of Unkeyed has them as an lvalue that is not
// const, and a keyed array has none.  set and exchange take the element, and
// the doors of the run take an element that copies as its bytes.  A keyed
// array does not take an assignment, and it has the size of the plain array.
struct ValueDoorBrand final {};
using KeyedInts = FixedArray<int, 4, ValueDoorBrand>;
template <class Array>
concept SetsElement = requires(Array&& array) { static_cast<Array&&>(array).set(index<0>(), 2); };
template <class Array>
concept ExchangesElement =
    requires(Array&& array) { static_cast<void>(static_cast<Array&&>(array).exchange(index<0>(), 2)); };
template <class Array>
concept UpdatesElement =
    requires(Array&& array) { static_cast<Array&&>(array).update(index<0>(), [](int&) noexcept {}); };
template <class Array>
concept FillsRun = requires(Array&& array) { static_cast<Array&&>(array).fill(2); };
template <class Array>
concept CopiesRun =
    requires(Array&& array, View<int const, 4> source) { static_cast<Array&&>(array).copy_from(source); };
template <class Array>
concept CopiesWindow = requires(Array&& array, View<int const> source) {
    static_cast<void>(static_cast<Array&&>(array).copy_window(0, source));
};
template <class Array>
concept HasValueDoors = SetsElement<Array> && ExchangesElement<Array> && UpdatesElement<Array> && FillsRun<Array>
                     && CopiesRun<Array> && CopiesWindow<Array>;
template <class Array>
concept HasNoValueDoor = !SetsElement<Array> && !ExchangesElement<Array> && !UpdatesElement<Array> && !FillsRun<Array>
                      && !CopiesRun<Array> && !CopiesWindow<Array>;
static_assert(HasValueDoors<FixedArray<int, 4>&>);
static_assert(HasNoValueDoor<FixedArray<int, 4> const&> && HasNoValueDoor<FixedArray<int, 4>>);
static_assert(HasNoValueDoor<KeyedInts&> && HasNoValueDoor<KeyedInts const&>);
// The update door takes an element that copies and moves with no throw, also
// in constant evaluation and for an element with no assignment.  copy_from
// runs in constant evaluation too.
struct NoAssignment {
    int value = 0;
    constexpr NoAssignment() noexcept = default;
    constexpr NoAssignment(NoAssignment const&) noexcept = default;
    NoAssignment& operator=(NoAssignment const&) = delete("an element of the check has no assignment");
};
static_assert(UpdatableElement<int> && UpdatableElement<NoAssignment> && !UpdatableElement<HeapArray<int>>);
// An element whose end or move is code puts its copy back only with a nothrow
// assignment, because the code would run while the slot holds no element.
struct EndsWithCode {
    constexpr EndsWithCode() noexcept = default;
    constexpr EndsWithCode(EndsWithCode const&) noexcept = default;
    EndsWithCode& operator=(EndsWithCode const&) = delete("an element of the check has no assignment");
    constexpr ~EndsWithCode() {}
};
struct MovesWithCode {
    constexpr MovesWithCode() noexcept = default;
    constexpr MovesWithCode(MovesWithCode const&) noexcept = default;
    constexpr MovesWithCode(MovesWithCode&&) noexcept {}
    MovesWithCode& operator=(MovesWithCode const&) = delete("an element of the check has no assignment");
};
struct AssignedWithCode {
    constexpr AssignedWithCode() noexcept = default;
    constexpr AssignedWithCode(AssignedWithCode const&) noexcept {}
    constexpr AssignedWithCode& operator=(AssignedWithCode const&) noexcept { return *this; }
    constexpr ~AssignedWithCode() {}
};
static_assert(CopiedOutElement<EndsWithCode> && !PutBackElement<EndsWithCode> && !UpdatableElement<EndsWithCode>);
static_assert(CopiedOutElement<MovesWithCode> && !PutBackElement<MovesWithCode> && !UpdatableElement<MovesWithCode>);
static_assert(UpdatableElement<AssignedWithCode>);
[[nodiscard]] consteval int update_and_copy() {
    FixedArray<int, 4> numbers{1, 2, 3, 4};
    int const given = numbers.update(index<1>(), [](int& number) noexcept {
        number *= 10;
        return number + 1;
    });
    FixedArray<NoAssignment, 2> elements{};
    elements.update(index<1>(), [](NoAssignment& element) noexcept { element.value = 7; });
    FixedArray<int, 4> copied{};
    copied.copy_from(numbers.view());
    return given + copied.at(index<1>()) + elements.at(index<1>()).value;
}
static_assert(update_and_copy() == 21 + 20 + 7);
static_assert(!std::is_copy_assignable_v<KeyedInts> && !std::is_move_assignable_v<KeyedInts>);
static_assert(std::is_copy_constructible_v<KeyedInts> && std::is_trivially_copyable_v<KeyedInts>);
static_assert(!std::is_move_constructible_v<FixedArray<HeapArray<int>, 2, ValueDoorBrand>>);
static_assert(std::is_move_constructible_v<FixedArray<HeapArray<int>, 2>>);
static_assert(sizeof(KeyedInts) == sizeof(FixedArray<int, 4>) && std::is_standard_layout_v<KeyedInts>);

// build_fixed_array gives the array alone for a body that gives void, and a
// Record of the result and the array for each other body.
using GivesNoResult = decltype([](auto) noexcept {});
using GivesRunSize = decltype([](auto run) noexcept { return run.size(); });
static_assert(std::is_same_v<decltype(build_fixed_array<int, 4>(std::declval<GivesNoResult>())), FixedArray<int, 4>>);
static_assert(std::is_same_v<decltype(build_fixed_array<int, 4>(std::declval<GivesRunSize>())),
                             ::foundation::core::Record<std::size_t, FixedArray<int, 4>>>);

// The body of build_fixed_array could write the array through a reference, a
// pointer, a Ref or a View to an object that is not const and that holds the
// array by value, at each depth of its own parts.  A const borrow, an array of
// another type and a borrow of an element pass.
struct Nested {
    int first = 0;
    Option<FixedArray<int, 4>> part{};
};
struct OtherArray {
    FixedArray<int, 5> five{};
};
template <class Part>
struct BodyWith {
    Part part;
    void operator()(auto) const noexcept {}
};
template <class Part>
struct BodyInBody {
    BodyWith<Part> inner;
    void operator()(auto) const noexcept {}
};
template <class Part>
concept MayBuild = BuildsUnnamedArray<BodyWith<Part>, int, 4>;
static_assert(!MayBuild<FixedArray<int, 4>&> && !MayBuild<FixedArray<int, 4>*> && !MayBuild<Nested&>);
static_assert(!MayBuild<Ref<FixedArray<int, 4>, ValueDoorBrand>> && !MayBuild<View<FixedArray<int, 4>>>);
static_assert(!BuildsUnnamedArray<BodyInBody<Nested&>, int, 4>);
static_assert(MayBuild<FixedArray<int, 4> const&> && MayBuild<FixedArray<int, 4>> && MayBuild<Nested const&>);
static_assert(MayBuild<OtherArray&> && MayBuild<int&> && MayBuild<View<int>> && MayBuild<FixedArray<int, 4> const*>);

// The value door build_at: the element at a proven index becomes the value
// of fn(args...), built in its place.  fn holds no state: a function, a
// pointer to a function, or a lambda with no capture.  No argument names
// the array, an element of it or a borrow of one of them, or holds one of
// them in a direct member, as the owner of the array does.  A const array
// and a temporary array have no value door.
struct Slot {
    int value = 0;
    int twice = 0;
};
using Slots = FixedArray<Slot, 4>;
struct SlotOwner {
    Slots slots{};
};
struct SlotsByReference {
    Slots& slots;
};
struct Unrelated {
    int seed = 0;
};
// The brand of the Refs of the checks below.
struct SlotBrand final {};
[[nodiscard]] constexpr Slot make_slot(int value) noexcept { return Slot{.value = value, .twice = 2 * value}; }
using MakesSlot = decltype([](int value) noexcept { return Slot{.value = value, .twice = 2 * value}; });
using MakesSlotWithState = decltype([seed = 1](int value) noexcept { return Slot{.value = value + seed, .twice = 0}; });
// The elements whose result goes into the slot after the function returned, and
// the elements whose result takes the slot while the function runs.
struct ThreeWords {
    std::size_t first = 0;
    std::size_t second = 0;
    std::size_t third = 0;
};
struct CopiesByCode {
    int value = 0;
    CopiesByCode() noexcept = default;
    CopiesByCode(CopiesByCode const& other) noexcept : value{other.value} {}
    CopiesByCode& operator=(CopiesByCode const&) noexcept = default;
};
static_assert(BuiltBesideItsSlot<Slot> && BuiltBesideItsSlot<View<Slot const>> && BuiltBesideItsSlot<int>);
static_assert(!BuiltBesideItsSlot<ThreeWords> && !BuiltBesideItsSlot<CopiesByCode>);
using CopiesSlot =
    decltype([](auto const& from) noexcept { return Slot{.value = 1, .twice = sizeof(from) > 0 ? 1 : 0}; });
using GivesInt = decltype([](int value) noexcept { return value; });
using MayThrow = decltype([](int value) { return Slot{.value = value, .twice = 0}; });
template <class Array, class Fn, class... Args>
concept BuildsAt = requires(Array&& array, Fn&& fn, Args&&... args) {
    static_cast<Array&&>(array).build_at(index<0>(), static_cast<Fn&&>(fn), static_cast<Args&&>(args)...);
};
static_assert(BuildsAt<Slots&, MakesSlot, int> && BuildsAt<Slots&, decltype(make_slot)&, int>
              && BuildsAt<Slots&, decltype(&make_slot), int>);
static_assert(!BuildsAt<Slots&, MakesSlotWithState, int>);
static_assert(!BuildsAt<Slots const&, MakesSlot, int> && !BuildsAt<Slots, MakesSlot, int>);
static_assert(!BuildsAt<Slots&, GivesInt, int> && !BuildsAt<Slots&, MayThrow, int>);
static_assert(!BuildsAt<Slots&, CopiesSlot, Slot const&> && !BuildsAt<Slots&, CopiesSlot, Slot&>);
static_assert(!BuildsAt<Slots&, CopiesSlot, Slots&> && !BuildsAt<Slots&, CopiesSlot, Slots const&>);
static_assert(!BuildsAt<Slots&, CopiesSlot, Slot const*> && !BuildsAt<Slots&, CopiesSlot, Slots*>);
static_assert(!BuildsAt<Slots&, CopiesSlot, View<Slot const>>
              && !BuildsAt<Slots&, CopiesSlot, Ref<Slots const, SlotBrand>>);
static_assert(!BuildsAt<Slots&, CopiesSlot, SlotOwner&> && !BuildsAt<Slots&, CopiesSlot, Ref<SlotOwner, SlotBrand>>);
static_assert(!BuildsAt<Slots&, CopiesSlot, SlotsByReference const&>);
static_assert(BuildsAt<Slots&, CopiesSlot, Unrelated const&> && BuildsAt<Slots&, CopiesSlot, FixedArray<Slot, 2>&>);

// at takes an Index<N> or a constant index below N, and an Index of a
// different count does not convert down.
template <class Array, class Proof>
concept ReadsAt = requires(Array& array, Proof proof) { array.at(proof); };
static_assert(ReadsAt<FixedArray<int, 4>, Index<4>> && ReadsAt<FixedArray<int, 4>, StaticIndex<3>>);
static_assert(ReadsAt<FixedArray<int, 4>, Index<2>>);
static_assert(!ReadsAt<FixedArray<int, 4>, StaticIndex<4>>);
static_assert(!ReadsAt<FixedArray<int, 4>, Index<8>>);
static_assert(!ReadsAt<FixedArray<int, 4>, std::size_t>);

// The elements in a constant evaluation.
[[nodiscard]] consteval int sum_of(FixedArray<int, 4> const& array) noexcept {
    int total = 0;
    for (int const value : array) {
        total += value;
    }
    return total;
}

static_assert(sum_of(FixedArray<int, 4>{}) == 0);
static_assert(sum_of(FixedArray<int, 4>{1, 2, 3, 4}) == 10);
static_assert(FixedArray<int, 3>{4, 5, 6} == FixedArray<int, 3>{4, 5, 6});
static_assert(!(FixedArray<int, 3>{4, 5, 6} == FixedArray<int, 3>{4, 5, 7}));
static_assert(FixedArray<NoDefault, 2>{NoDefault{1}, NoDefault{2}}
              == FixedArray<NoDefault, 2>{NoDefault{1}, NoDefault{2}});

[[nodiscard]] consteval int write_and_read() noexcept {
    auto [array, key] = mint_keyed_owner(fixed_array_owner<int, 3>);
    array.with_element(key, index<0>(), [](int& element) noexcept { element = 10; });
    array.with_element(key, Index<3>::check(2).expect("2 is below 3"), [](int& element) noexcept { element = 30; });
    array.with_mut(key,
                   [](auto run) noexcept { fill(take_window(std::move(run), 1, 1).expect("one element fits"), 20); });
    int total = 0;
    for (Index<3> const position : array.indices()) {
        total += array.at(position) * static_cast<int>(position.raw() + 1);
    }
    return total;
}
static_assert(write_and_read() == 10 * 1 + 20 * 2 + 30 * 3);

// The View of an array reads each element at its proven index.
[[nodiscard]] consteval int view_reads() noexcept {
    FixedArray<int, 4> array{7, 8, 9, 10};
    View<int const, 4> const elements = array.view();
    int total = 0;
    for (Index<4> const position : elements.indices()) {
        total += elements.at(position);
    }
    return total + elements.at(index<3>());
}
static_assert(view_reads() == 7 + 8 + 9 + 10 + 10);

// get reads below the count and gives no value at the count, in a constant
// evaluation too.
[[nodiscard]] consteval int get_reads() noexcept {
    auto [array, key] = mint_keyed_owner(fixed_array_owner<int, 3>, FixedArray<int, 3>{5, 6, 7});
    int total = *array.get(2).expect("2 is below 3");
    array.with_mut(key, [](auto run) noexcept {
        auto element = run.get_mut(0).expect("0 is below 3");
        *element = 1;
    });
    total += array.at(index<0>());
    return array.get(3).is_none() ? total : -1;
}
static_assert(get_reads() == 8);

// enumerate() gives a Record of the proven index and a Ref to the element.
// Only a View of a fixed extent and a FixedArray have indices(), so only
// they have enumerate().  The loop of a temporary array is refused.
template <class Elements>
concept Enumerates = requires(Elements& elements) { elements.enumerate(); };
template <class Array>
concept EnumeratesTemporary = requires { std::declval<Array>().enumerate(); };
static_assert(Enumerates<View<int, 4>> && Enumerates<FixedArray<int, 4>> && Enumerates<FixedArray<int, 4> const>);
static_assert(!Enumerates<View<int>> && !Enumerates<HeapArray<int>>);
static_assert(!EnumeratesTemporary<FixedArray<int, 4>> && !EnumeratesTemporary<FixedArray<int, 4> const>);

// The target of a Ref of each brand.  Each call of enumerate() or zip gives
// its Refs a fresh brand, so a check reads the parts of a pair and the
// target of each Ref, and never names the brand.
template <class R>
struct RefTargetOf;
template <class T, class Brand>
struct RefTargetOf<Ref<T, Brand>> {
    using type = T;
};
template <class R>
using ref_target_t = typename RefTargetOf<R>::type;

// The aliases are local to the body, so no fresh brand leaves the scope of
// its call.  zip of two walks of one fixed extent gives the loop, and a pair
// holds two pointers.  When a walk has a dynamic extent, try_zip gives a
// Result (enumerate_and_zip).  The fixture neg_core_zip_different_fixed_extents
// holds the refusal of two different fixed extents.
[[nodiscard]] consteval bool pairs_hold_an_index_and_refs() noexcept {
    using ConstEnumeratedPair = decltype(*std::declval<FixedArray<int, 4> const&>().enumerate().begin());
    using EnumeratedPair = decltype(*std::declval<View<int, 4>>().enumerate().begin());
    using Zipped = decltype(std::declval<View<int, 4>>().walk().zip(std::declval<View<long const, 4>>().walk()));
    using ZippedPair = decltype(*std::declval<Zipped const&>().begin());
    using ::foundation::reflect::pack_at_t;
    return std::is_same_v<pack_at_t<0, typename ConstEnumeratedPair::types>, Index<4>>
        && std::is_same_v<ref_target_t<pack_at_t<1, typename ConstEnumeratedPair::types>>, int const>
        && std::is_same_v<ref_target_t<pack_at_t<1, typename EnumeratedPair::types>>, int>
        && std::is_same_v<ref_target_t<pack_at_t<0, typename ZippedPair::types>>, int>
        && std::is_same_v<ref_target_t<pack_at_t<1, typename ZippedPair::types>>, long const>
        && sizeof(ZippedPair) == 2 * sizeof(void*);
}
static_assert(pairs_hold_an_index_and_refs());

// enumerate and zip in a constant evaluation.
[[nodiscard]] consteval int enumerate_and_zip() noexcept {
    FixedArray<int, 4> const weights{1, 2, 3, 4};
    FixedArray<int, 4> const values = build_fixed_array<int, 4>([](auto run) noexcept {
        for (auto const [position, element] : run.view().enumerate()) {
            auto writer = run.get_mut(position.raw()).expect("the position is below the count");
            *writer = *element + static_cast<int>(position.raw()) + 10;
        }
    });
    int total = 0;
    for (auto const [weight, value] : weights.view().walk().zip(values.view().walk())) {
        total += *weight * *value;
    }
    View<int const> const head = values.view().window(0, 2).expect("two elements fit");
    return total + (head.walk().try_zip(weights.view().walk()).is_err() ? 1 : 0);
}
static_assert(enumerate_and_zip() == 1 * 10 + 2 * 11 + 3 * 12 + 4 * 13 + 1);

// Two calls of enumerate() or zip give two fresh brands, so a Ref of one
// loop is not a Ref of the other loop.
[[nodiscard]] consteval bool two_calls_give_two_brands() noexcept {
    FixedArray<int, 2> const values{1, 2};
    auto const first_loop = values.enumerate();
    auto const second_loop = values.enumerate();
    auto const first_pairs = values.view().walk().zip(values.view().walk());
    auto const second_pairs = values.view().walk().zip(values.view().walk());
    return !std::same_as<decltype(first_loop), decltype(second_loop)>
        && !std::same_as<decltype(first_pairs), decltype(second_pairs)>;
}
static_assert(two_calls_give_two_brands());

// A HeapArray holds a pointer and a count.  It moves, it does not copy, and
// the allocation capability is the first argument of its one door.
struct OtherTag final {
    using grants_allocation = void;
};
struct MoveOnlyElement final {
    MoveOnlyElement() noexcept = default;
    MoveOnlyElement(MoveOnlyElement&&) noexcept = default;
    MoveOnlyElement(MoveOnlyElement const&) = delete;
};
static_assert(sizeof(HeapArray<std::uint32_t>) == 2 * sizeof(std::size_t));
static_assert(sizeof(HeapArray<std::uint8_t, 4096>) == 2 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<HeapArray<int>>
              && std::is_nothrow_move_assignable_v<HeapArray<int>>);
static_assert(!std::is_copy_constructible_v<HeapArray<int>> && !std::is_copy_assignable_v<HeapArray<int>>);
static_assert(!std::is_trivially_copyable_v<HeapArray<int>>);
static_assert(CanMintHeapArray<int, alignof(int), Alloc>);
static_assert(!CanMintHeapArray<int, alignof(int), OtherTag>);
static_assert(!CanMintHeapArray<MoveOnlyElement, alignof(MoveOnlyElement), Alloc>);
static_assert(!CanMintHeapArray<int, 3, Alloc>);
static_assert(!CanMintHeapArray<std::uint64_t, 4, Alloc>);
static_assert(!::foundation::linear::is_linear_v<HeapArray<int>>);

// A HeapArray of a linear element is linear, so each path of its holder
// consumes it, and its destructor is no silent drop of the elements.
struct[[= ::foundation::linear::linear{}]] LinearToken final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    LinearToken() noexcept = default;
    LinearToken(LinearToken&&) noexcept = default;
    LinearToken& operator=(LinearToken&&) noexcept { return *this; }
    LinearToken(LinearToken const&) = delete;
};
static_assert(::foundation::linear::is_linear_v<HeapArray<LinearToken>>);

static_assert(ViewsTemporary<HeapArray<int>&> && !ViewsTemporary<HeapArray<int>>);
static_assert(!ViewsTemporary<HeapArray<int> const>);

// mint_heap_array_with builds each element in place from the result of
// init, which is exactly a T, so an element that does not copy or move
// passes.  An init that gives a type that converts to T, or that can throw,
// does not pass.
struct Anchored final {
    std::uint64_t position = 0;
    explicit Anchored(std::uint64_t from) noexcept : position{from} {}
    Anchored(Anchored const&) = delete;
    Anchored(Anchored&&) = delete;
};
struct BuildsAnchored final {
    Anchored operator()(std::size_t position) const noexcept { return Anchored{position}; }
};
struct BuildsLong final {
    long operator()(std::size_t position) const noexcept { return static_cast<long>(position); }
};
struct BuildsThrowing final {
    int operator()(std::size_t position) const { return static_cast<int>(position); }
};
static_assert(CanMintHeapArrayWith<Anchored, alignof(Anchored), Alloc, BuildsAnchored>);
static_assert(CanMintHeapArrayWith<Anchored, 64, Alloc, BuildsAnchored>);
static_assert(CanMintHeapArrayWith<long, alignof(long), Alloc, BuildsLong>);
static_assert(!CanMintHeapArrayWith<int, alignof(int), Alloc, BuildsLong>);
static_assert(!CanMintHeapArrayWith<int, alignof(int), Alloc, BuildsThrowing>);
static_assert(!CanMintHeapArrayWith<Anchored, alignof(Anchored), OtherTag, BuildsAnchored>);
static_assert(!CanMintHeapArrayWith<Anchored, 4, Alloc, BuildsAnchored>);

// mint_heap_array_zeroed takes an element of which each bit pattern is a
// value, whose lifetime starts over its bytes: an integer, a float and a
// record of them.  A bool, an enumeration with an enumerator, a pointer and a
// proof do not pass, because zero bytes can be no value of them.
enum class Mode : std::uint8_t {
    first = 1,
    second = 2
};
static_assert(CanMintHeapArrayZeroed<std::uint64_t, 64, Alloc>
              && CanMintHeapArrayZeroed<double, alignof(double), Alloc>);
static_assert(CanMintHeapArrayZeroed<Record, alignof(Record), Alloc>);
static_assert(!CanMintHeapArrayZeroed<bool, alignof(bool), Alloc>);
static_assert(!CanMintHeapArrayZeroed<Mode, alignof(Mode), Alloc>);
static_assert(!CanMintHeapArrayZeroed<int*, alignof(int*), Alloc>);
static_assert(!CanMintHeapArrayZeroed<::foundation::lifetime::detail::HoldsProof,
                                      alignof(::foundation::lifetime::detail::HoldsProof), Alloc>);
static_assert(!CanMintHeapArrayZeroed<std::uint64_t, 64, OtherTag>);

// mint_heap_array_written takes an implicit-lifetime element that a move of
// bytes writes, and a writer that takes a WriteLease with no throw.  A proof
// does not pass.  The lease has no read: no view(), no get(), no subscript
// and no data().  It does not copy or move, so it stays in the frame of the
// mint.
struct WritesNothing final {
    void operator()(WriteLease<int>& lease) const noexcept { static_cast<void>(lease.remaining()); }
};
struct WritesThrowing final {
    void operator()(WriteLease<int>& lease) const { static_cast<void>(lease.remaining()); }
};
static_assert(CanMintHeapArrayWritten<int, alignof(int), Alloc, WritesNothing>);
static_assert(CanMintHeapArrayWritten<int, 64, Alloc, WritesNothing>);
static_assert(!CanMintHeapArrayWritten<int, alignof(int), Alloc, WritesThrowing>);
static_assert(!CanMintHeapArrayWritten<int, alignof(int), OtherTag, WritesNothing>);
static_assert(!CanMintHeapArrayWritten<::foundation::lifetime::detail::HoldsProof,
                                       alignof(::foundation::lifetime::detail::HoldsProof), Alloc, WritesNothing>);
static_assert(!CanMintHeapArrayWritten<Anchored, alignof(Anchored), Alloc, WritesNothing>);
template <class Lease>
concept ReadsLease = requires(Lease& lease) { lease.view(); } || requires(Lease& lease) { lease.get(0); }
                  || requires(Lease& lease) { lease[0]; } || requires(Lease& lease) { lease.data(); };
static_assert(!ReadsLease<WriteLease<int>> && !ReadsLease<WriteLease<Record>>);
static_assert(!std::is_copy_constructible_v<WriteLease<int>> && !std::is_move_constructible_v<WriteLease<int>>);
static_assert(!std::is_copy_assignable_v<WriteLease<int>> && !std::is_move_assignable_v<WriteLease<int>>);
static_assert(sizeof(WriteLease<int>) == 3 * sizeof(std::size_t));

// A count that refuses a start over bytes, the shape of a provenance tag.
// Only its own constructor builds it, so the mints that copy a value or
// build in place take it, and the two mints whose elements start with
// the bytes of the allocation do not.
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] MarkedCount final {
    std::uint64_t count = 0;
};
struct BuildsMarkedCount final {
    MarkedCount operator()(std::size_t) const noexcept { return MarkedCount{}; }
};
struct WritesMarkedCount final {
    void operator()(WriteLease<MarkedCount>& lease) const noexcept { static_cast<void>(lease.remaining()); }
};
static_assert(CanMintHeapArray<MarkedCount, alignof(MarkedCount), Alloc>);
static_assert(CanMintHeapArrayWith<MarkedCount, alignof(MarkedCount), Alloc, BuildsMarkedCount>);
static_assert(!CanMintHeapArrayZeroed<MarkedCount, alignof(MarkedCount), Alloc>);
static_assert(!CanMintHeapArrayWritten<MarkedCount, alignof(MarkedCount), Alloc, WritesMarkedCount>);

// An element is an object type: no reference.
template <class T>
concept HasHeapArray = requires { typename HeapArray<T>; };
static_assert(HasHeapArray<int> && !HasHeapArray<int&> && !HasHeapArray<int const>);

// The empty array holds no storage, in a constant evaluation too.
[[nodiscard]] consteval std::size_t empty_reads() noexcept {
    HeapArray<int> const empty{};
    std::size_t turns = 0;
    for (int const element : empty) {
        turns += static_cast<std::size_t>(element) + 1;
    }
    return empty.size() + turns + (empty.get(0).is_none() ? 0 : 1);
}
static_assert(empty_reads() == 0);

// A MaskedArray holds a pointer, a count and a mask.  It moves and does not
// copy.  Its mint builds each element with the default constructor, so an
// element that does not copy passes, and an element with no default
// constructor does not.  No subscript and no data() reach an element.
struct PinnedElement final {
    PinnedElement() noexcept = default;
    PinnedElement(PinnedElement const&) = delete;
    PinnedElement(PinnedElement&&) = delete;
};
static_assert(sizeof(MaskedArray<std::uint64_t>) == 3 * sizeof(std::size_t));
static_assert(sizeof(MaskedArray<std::uint8_t, 4096>) == 3 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<MaskedArray<int>>
              && std::is_nothrow_move_assignable_v<MaskedArray<int>>);
static_assert(!std::is_copy_constructible_v<MaskedArray<int>> && !std::is_copy_assignable_v<MaskedArray<int>>);
static_assert(!std::is_trivially_copyable_v<MaskedArray<int>>);
// The array owns its elements alone, so it moves between threads as its
// element does, and a pointer element refuses.
static_assert(::foundation::transfer::Sendable<MaskedArray<int>>
              && ::foundation::transfer::Shareable<MaskedArray<int>>);
static_assert(!::foundation::transfer::Sendable<MaskedArray<int*>>);
static_assert(CanMintMaskedArray<int, alignof(int), Alloc>);
static_assert(CanMintMaskedArray<PinnedElement, 64, Alloc>);
static_assert(CanMintMaskedArray<MoveOnlyElement, alignof(MoveOnlyElement), Alloc>);
static_assert(!CanMintMaskedArray<int, alignof(int), OtherTag>);
static_assert(!CanMintMaskedArray<NoDefault, alignof(NoDefault), Alloc>);
static_assert(!CanMintMaskedArray<int, 3, Alloc>);
static_assert(!CanMintMaskedArray<std::uint64_t, 4, Alloc>);
static_assert(!HasSubscript<MaskedArray<int>&> && !HasData<MaskedArray<int>&>);
static_assert(!::foundation::linear::is_linear_v<MaskedArray<int>>);

// at_masked and view() give const elements, also of an array that is not
// const, and with_masked lends the element in place.  A view and an element of
// a temporary are refused.
template <class Array>
concept ReadsMaskedTemporary = requires { std::declval<Array>().at_masked(std::uint64_t{0}); };
static_assert(std::is_same_v<decltype(std::declval<MaskedArray<int>&>().at_masked(0)), int const&>);
static_assert(std::is_same_v<decltype(std::declval<MaskedArray<int> const&>().at_masked(0)), int const&>);
static_assert(std::is_same_v<decltype(std::declval<MaskedArray<int>&>().view()), View<int const>>);
static_assert(ViewsTemporary<MaskedArray<int>&> && !ViewsTemporary<MaskedArray<int>>);
static_assert(!ViewsTemporary<MaskedArray<int> const>);
static_assert(ReadsMaskedTemporary<MaskedArray<int>&> && !ReadsMaskedTemporary<MaskedArray<int>>);
static_assert(!ReadsMaskedTemporary<MaskedArray<int> const>);

// The empty array holds no storage, in a constant evaluation too.
[[nodiscard]] consteval std::size_t empty_masked_reads() noexcept {
    MaskedArray<int> const empty{};
    return empty.size() + empty.view().size();
}
static_assert(empty_masked_reads() == 0);

}  // namespace detail::array_checks

}  // namespace foundation::core

namespace foundation::lifetime {

namespace detail::array_start_checks {

using ::foundation::core::detail::array_checks::Record;

// The checked start over an address gives the address of the first object,
// and no View.  Its element is const when the storage or T is const.
// Volatile storage is refused, and so is an array element, because a View of
// the objects holds neither.
template <typename Storage, typename T>
concept can_start_as = requires(Storage* storage) {
    { start_as_array<T>(storage, 1) } -> std::same_as<element_for_storage_t<Storage, T>*>;
};
static_assert(can_start_as<unsigned char, Record> && can_start_as<void, Record>);
static_assert(can_start_as<const unsigned char, Record> && can_start_as<unsigned char, const Record>);
static_assert(!can_start_as<volatile unsigned char, Record> && !can_start_as<const volatile void, Record>,
              "volatile storage is refused, and a caller adds volatile to the address of an element");
static_assert(!can_start_as<unsigned char, HoldsProof> && !can_start_as<const void, ProofShape[1]>,
              "the checked start refuses a proof subobject through every storage qualifier");
static_assert(ImplicitLifetimeThroughout<std::uint32_t[4]> && !can_start_as<unsigned char, std::uint32_t[4]>,
              "a View holds no array element, so the start refuses an array type that the concept accepts");

}  // namespace detail::array_start_checks

}  // namespace foundation::lifetime
