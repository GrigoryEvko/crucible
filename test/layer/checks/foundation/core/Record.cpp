// The compile-time checks of foundation/core/Record.h.

#include <foundation/core/Record.h>

#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::record_checks {

// The byte offset of the part at index I of the Record R: the offset of the
// flat base, plus the offset of the cell of the part in the flat base, plus
// the offset of the part in its cell.  The walk helpers of the Scalar family
// read the bases and the members by index.
template <class R, std::size_t I>
[[nodiscard]] consteval std::size_t part_offset() {
    std::meta::info const flat = base_type_at(^^R, 0);
    return base_offset_at(^^R, 0) + base_offset_at(flat, I) + member_offset_at(base_type_at(flat, I), 0);
}

// The byte offset of the data member at index I of the struct S.
template <class S, std::size_t I>
[[nodiscard]] consteval std::size_t member_offset() {
    return member_offset_at(^^S, I);
}

// True when the Record R and the struct S have one size and one alignment,
// and each part of R has the offset of the member of S at its index.
template <class R, class S>
[[nodiscard]] consteval bool has_the_layout_of() {
    if (sizeof(R) != sizeof(S) || alignof(R) != alignof(S)) return false;
    bool is_same_layout = true;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        is_same_layout = ((part_offset<R, Is>() == member_offset<S, Is>()) && ...);
    }(std::make_index_sequence<R::size()>{});
    return is_same_layout;
}

struct TokenA final {};
struct TokenB final {};
struct Word8 final {
    std::uint8_t value = 0;
};
struct Triple final {
    std::uint32_t first = 0;
    std::uint16_t second = 0;
    std::uint8_t third = 0;
};

// The twelve shapes.  Each hand-written struct has the members of its
// Record, in the same order.  Only an empty member has
// [[no_unique_address]], as only an empty part of a Record has it.
struct ShapeOne final {
    std::uint32_t first;
};
struct ShapeTwoWords final {
    std::uint64_t first;
    std::uint64_t second;
};
struct ShapeSmallThenLarge final {
    std::uint8_t first;
    std::uint64_t second;
};
struct ShapeLargeThenSmall final {
    std::uint64_t first;
    std::uint8_t second;
};
struct ShapeMixed final {
    std::uint16_t first;
    std::uint32_t second;
    std::uint8_t third;
    std::uint64_t fourth;
};
struct ShapeSameType final {
    std::uint32_t first;
    std::uint32_t second;
    std::uint32_t third;
};
struct ShapeFloats final {
    float first;
    double second;
};
struct ShapeTokenFirst final {
    [[no_unique_address]] TokenA first;
    std::uint32_t second;
};
struct ShapeTokenLast final {
    std::uint32_t first;
    [[no_unique_address]] TokenA second;
};
struct ShapeNested final {
    Triple first;
    Word8 second;
};
struct ShapeBytes final {
    std::uint8_t first;
    std::uint8_t second;
    std::uint8_t third;
};
struct ShapeTwoTokens final {
    [[no_unique_address]] TokenA first;
    [[no_unique_address]] TokenB second;
};

static_assert(has_the_layout_of<Record<std::uint32_t>, ShapeOne>());
static_assert(has_the_layout_of<Record<std::uint64_t, std::uint64_t>, ShapeTwoWords>());
static_assert(has_the_layout_of<Record<std::uint8_t, std::uint64_t>, ShapeSmallThenLarge>());
static_assert(has_the_layout_of<Record<std::uint64_t, std::uint8_t>, ShapeLargeThenSmall>());
static_assert(has_the_layout_of<Record<std::uint16_t, std::uint32_t, std::uint8_t, std::uint64_t>, ShapeMixed>());
static_assert(has_the_layout_of<Record<std::uint32_t, std::uint32_t, std::uint32_t>, ShapeSameType>());
static_assert(has_the_layout_of<Record<float, double>, ShapeFloats>());
static_assert(has_the_layout_of<Record<TokenA, std::uint32_t>, ShapeTokenFirst>());
static_assert(has_the_layout_of<Record<std::uint32_t, TokenA>, ShapeTokenLast>());
static_assert(has_the_layout_of<Record<Triple, Word8>, ShapeNested>());
static_assert(has_the_layout_of<Record<std::uint8_t, std::uint8_t, std::uint8_t>, ShapeBytes>());
static_assert(has_the_layout_of<Record<TokenA, TokenB>, ShapeTwoTokens>());

// A part with tail padding keeps its padding: no part shares a byte with a
// different part.  Triple has a default member initializer, so the ABI can
// put the next member of a [[no_unique_address]] layout in its last byte.
// A copy of the bytes of the first part would then write the second part.
struct PlainPair final {
    Triple first;
    std::uint8_t second;
};
static_assert(sizeof(Record<Triple, std::uint8_t>) == sizeof(PlainPair));
static_assert(part_offset<Record<Triple, std::uint8_t>, 1>() == sizeof(Triple));

// Empty parts of different types collapse.  Two empty parts of one type
// are two objects, so they have two addresses.
static_assert(sizeof(Record<TokenA, TokenB>) == 1);
static_assert(sizeof(Record<TokenA, TokenA>) == 2);
static_assert(sizeof(Record<>) == 1);

// A part that only moves, a part that cannot move, and a part with no
// default constructor.
struct MoveOnly final {
    MoveOnly() = default;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    MoveOnly(MoveOnly const&) = delete;
    MoveOnly& operator=(MoveOnly const&) = delete;
};
struct Fixed final {
    Fixed() = default;
    Fixed(Fixed&&) = delete;
};
struct NoDefault final {
    explicit constexpr NoDefault(int from) noexcept : value{from} {}
    int value;
};

// An aggregate, trivially copyable, standard layout of no kind, and an
// implicit-lifetime type, when each part is trivially copyable.
using Plain = Record<std::uint32_t, double, TokenA>;
static_assert(std::is_aggregate_v<Plain>);
static_assert(std::is_trivially_copyable_v<Plain>);
static_assert(std::is_implicit_lifetime_v<Plain>);
static_assert(std::is_nothrow_move_constructible_v<Plain> && std::is_nothrow_destructible_v<Plain>);

// The part at index I of a Record, through a binding pack.
template <std::size_t I, class R>
[[nodiscard]] consteval auto part_of(R const& record) {
    auto const& [... parts] = record;
    return parts...[I];
}

// A structural type: a Record of plain parts is a template argument.
template <Plain Value>
inline constexpr std::uint32_t first_part_of = part_of<0>(Value);
static_assert(first_part_of<Plain{7u, 1.5, {}}> == 7u);

// A move-only part makes the Record move-only, and a part that cannot move
// makes the Record unable to move.
static_assert(std::is_nothrow_move_constructible_v<Record<MoveOnly, int>>);
static_assert(!std::is_copy_constructible_v<Record<MoveOnly, int>>);
static_assert(!std::is_move_constructible_v<Record<Fixed, int>>);

// A part with no default constructor gets no default member initializer,
// so the Record has no default constructor, and no hard error stops the
// trait.
static_assert(!std::is_default_constructible_v<Record<NoDefault, int>>);
static_assert(std::is_default_constructible_v<Record<int, int>>);
static_assert(part_of<0>(Record<NoDefault, int>{NoDefault{3}, 4}).value == 3);

// The default member initializer gives each part its value initialization.
static_assert(part_of<0>(Record<int, int>{}) == 0 && part_of<1>(Record<int, int>{}) == 0);

// The aggregate initialization and the deduction guide.
static_assert(std::is_same_v<decltype(Record{1, 2.0}), Record<int, double>>);
static_assert(part_of<1>(Record<int, int>{4, 5}) == 5);

// The gate of a part.
static_assert(RecordElement<int> && RecordElement<MoveOnly> && RecordElement<Fixed> && RecordElement<TokenA>);
static_assert(!RecordElement<int&> && !RecordElement<int const> && !RecordElement<int volatile>);
static_assert(!RecordElement<int[2]> && !RecordElement<void>);

// The tuple protocol of a binding: the binding pack sees each part, with its
// type.
static_assert(std::is_same_v<decltype(part_of<1>(Record<int, long, char>{})), long>);
static_assert(part_of<2>(Record<int, long, char>{1, 2, 'c'}) == 'c');

// The compares, at compile time.
static_assert(Record<int, int>{1, 2} == Record<int, int>{1, 2});
static_assert(Record<int, int>{1, 2} != Record<int, int>{1, 3});
static_assert(std::is_lt(Record<int, int>{1, 2} <=> Record<int, int>{2, 0}));
static_assert(std::is_same_v<decltype(Record<int>{} <=> Record<int>{}), std::strong_ordering>);
static_assert(std::is_same_v<decltype(Record<int, float>{} <=> Record<int, float>{}), std::partial_ordering>);
static_assert(std::is_same_v<decltype(Record<>{} <=> Record<>{}), std::strong_ordering>);

// The operations of a Pack.
using Three = Pack<int, long, char>;
static_assert(PackType<Three> && PackType<Pack<>>);
static_assert(!PackType<int> && !PackType<Record<int>> && !PackType<Three const>);
static_assert(std::is_empty_v<Three>);
static_assert(pack_size_v<Three> == 3 && pack_size_v<Pack<>> == 0);
static_assert(std::is_same_v<pack_at_t<0, Three>, int> && std::is_same_v<pack_at_t<2, Three>, char>);
static_assert(pack_contains_v<long, Three> && !pack_contains_v<short, Three>);
static_assert(pack_index_of_v<int, Three> == 0 && pack_index_of_v<char, Three> == 2);
static_assert(std::is_same_v<pack_concat_t<>, Pack<>>);
static_assert(std::is_same_v<pack_concat_t<Three>, Three>);
static_assert(std::is_same_v<pack_concat_t<Pack<int>, Pack<>, Pack<long, char>>, Three>);
static_assert(std::is_same_v<pack_apply_t<Record, Three>, Record<int, long, char>>);
static_assert(pack_unique_v<Three> && pack_unique_v<Pack<>> && !pack_unique_v<Pack<int, long, int>>);
static_assert(std::is_same_v<Record<int, long>::types, Pack<int, long>>);

// A type in a nested Pack only is no part of the outer Pack.
static_assert(!pack_contains_v<int, Pack<Pack<int>, long>>);
static_assert(pack_index_of_v<Pack<int>, Pack<Pack<int>, long>> == 0);

// A class that only looks like a Pack is no Pack.
template <class... Ts>
struct NotAPack final {};
static_assert(!PackType<NotAPack<int>> && !PackType<Pack<int>&>);

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.3, and part, the one data member of a
// cell, which a public base of each Record holds.
inline constexpr TextView record_names{
    "Record Pack types part pack_size_v pack_at_t pack_contains_v pack_index_of_v pack_concat_t pack_apply_t "
    "pack_unique_v"};

// Each form of a cell, and a Pack, give only the shared verbs, the language
// protocols and the names of the list.
CRUCIBLE_CLOSED_SURFACE(record_names, Record<>);
CRUCIBLE_CLOSED_SURFACE(record_names, Record<int, long>);
CRUCIBLE_CLOSED_SURFACE(record_names, Record<TokenA, TokenB>);
CRUCIBLE_CLOSED_SURFACE(record_names, Record<MoveOnly, NoDefault>);
CRUCIBLE_CLOSED_SURFACE(record_names, Record<Fixed, int>);
CRUCIBLE_CLOSED_SURFACE(record_names, Pack<>);
CRUCIBLE_CLOSED_SURFACE(record_names, Pack<int, long>);

// The walk reads the member of each cell through the public bases: a list
// with no name part refuses a Record of one part and a Record of no part
// passes.
static_assert(!closed_surface<Record<int>>(TextView{"Record Pack types"}).is_closed());
static_assert(closed_surface<Record<>>(TextView{"Record Pack types"}).is_closed());

}  // namespace detail::record_checks

}  // namespace foundation::core
