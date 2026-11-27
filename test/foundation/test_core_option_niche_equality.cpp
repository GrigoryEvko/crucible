// Tests of the == of an Option of a niche payload whose equal is the identity
// of its bytes (EqualsByBytes of foundation/core/Equatable.h).  The niche form
// compares the bytes of the two slots, so each served payload checks two
// facts:
//   - each door that ends or empties a slot leaves the bytes of the empty
//     Option: Option(none), take, the consuming expect, value_or and match,
//     the copy and the move of an empty Option, and the assignment of an
//     empty Option, of none and of {}
//   - the == agrees with the form of two states and one compare of the
//     payloads: two empty Options, an empty and a full one, two equal
//     payloads and two different ones, and the same in a constant evaluation.
// The default Option is the reference, because its member writes the empty
// value of the niche.  The packed word has a niche that takes more than one
// pattern, and its empty slot holds the one word of niche_empty.  The marked
// classes (Index, BrandedIndex and RunAddress) also check that their own ==
// agrees with the identity of their bytes over fixed and Philox values.

#include <foundation/core/Branded.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/DataAddress.h>
#include <foundation/core/Equatable.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/core/PackedWord.h>
#include <foundation/core/Region.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::BitField;
using ::foundation::core::EqualsByBytes;
using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::mint_run_address;
using ::foundation::core::none;
using ::foundation::core::Option;
using ::foundation::core::PackedWord;
using ::foundation::core::RunAddress;

struct IdTag final {};
using TestId = ::foundation::core::Id<IdTag, std::uint64_t>;

struct Tags final {};
struct Data final {};
// A layout of 15 bits in a word of 16, so the niche takes each word with the
// top bit set.
using Kinds = PackedWord<std::uint16_t, BitField<Tags, 10>, BitField<Data, 5>>;

// The owner of the keys of the test, the one class whose members make a key
// of a word.
struct KeyOwner {
    using Key = ::foundation::core::ForeignAddressKey<KeyOwner>;
    [[nodiscard]] static constexpr Key key_of_word(std::uintptr_t word) noexcept { return Key::of_word_(word); }
};
using TestKey = KeyOwner::Key;

using TestIndex = Index<64>;

static_assert(EqualsByBytes<TestId> && EqualsByBytes<TestKey> && EqualsByBytes<Kinds>);
static_assert(EqualsByBytes<TestIndex> && EqualsByBytes<RunAddress>);
static_assert(!EqualsByBytes<::foundation::core::TextView>);

// Runs body on the run of a line of 64 bytes, and gives the result of body.
template <class Body>
auto with_line(Body body) noexcept {
    auto minted = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<unsigned char, 64>);
    auto& [line, line_key] = minted;
    return line.with_mut(line_key, body);
}

// True when the two objects of one size hold the same bytes: two Options, two
// values of a marked class, or an Option and the word that its slot holds.
template <class Left, class Right>
    requires(sizeof(Left) == sizeof(Right))
[[nodiscard]] bool holds_same_bytes(Left const& left, Right const& right) noexcept {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the case compares the bytes of two objects, such as two slots of an Option")
    bool const is_same = __builtin_memcmp(__builtin_addressof(left), __builtin_addressof(right), sizeof(Left)) == 0;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return is_same;
}

// The == of two states and one compare of the payloads.
template <class T>
[[nodiscard]] constexpr bool equal_by_states(Option<T> const& left, Option<T> const& right) noexcept {
    if (left.is_some() != right.is_some()) return false;
    if (left.is_none()) return true;
    Option<T> left_copy = left;
    Option<T> right_copy = right;
    return std::move(left_copy).expect("the case holds a payload")
        == std::move(right_copy).expect("the case holds a payload");
}

// Each door that ends or empties a slot leaves the bytes of the default
// Option.  first is a full payload.
template <class T>
void doors_leave_the_empty_bytes(T const& first) noexcept {
    Option<T> const reference{};
    CRUCIBLE_FATAL_INVARIANT(reference.is_none());

    Option<T> const of_none{none};
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(of_none, reference));

    Option<T> taken = Option<T>::some(first);
    Option<T> const took = taken.take();
    CRUCIBLE_FATAL_INVARIANT(took.is_some() && holds_same_bytes(taken, reference));

    Option<T> expected = Option<T>::some(first);
    CRUCIBLE_FATAL_INVARIANT(std::move(expected).expect("the case holds a payload") == first);
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(expected, reference));

    Option<T> valued = Option<T>::some(first);
    CRUCIBLE_FATAL_INVARIANT(std::move(valued).value_or(first) == first);
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(valued, reference));

    Option<T> matched = Option<T>::some(first);
    bool const is_matched =
        std::move(matched).match([&first](T value) noexcept { return value == first; }, [] noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_matched && holds_same_bytes(matched, reference));

    Option<T> const copied = reference;
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(copied, reference));
    Option<T> source{};
    Option<T> const moved = std::move(source);
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(moved, reference));

    Option<T> assigned = Option<T>::some(first);
    assigned = reference;
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(assigned, reference));
    assigned = Option<T>::some(first);
    assigned = none;
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(assigned, reference));
    assigned = Option<T>::some(first);
    assigned = {};
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(assigned, reference));
}

// The == agrees with equal_by_states for the four cases of two payloads that
// differ.
template <class T>
void equality_agrees(T const& first, T const& second) noexcept {
    CRUCIBLE_FATAL_INVARIANT(!(first == second));
    Option<T> const empty{};
    Option<T> const full_first = Option<T>::some(first);
    Option<T> const also_first = Option<T>::some(first);
    Option<T> const full_second = Option<T>::some(second);
    FixedArray<Option<T>, 4> const cases{empty, full_first, also_first, full_second};
    for (Option<T> const& left : cases.view()) {
        for (Option<T> const& right : cases.view()) {
            CRUCIBLE_FATAL_INVARIANT((left == right) == equal_by_states(left, right));
            CRUCIBLE_FATAL_INVARIANT((left == right) == holds_same_bytes(left, right));
        }
    }
}

// The == in a constant evaluation takes the form of two states.
[[nodiscard]] consteval bool constant_equality_agrees() {
    Option<TestId> const empty{};
    Option<TestId> const one = Option<TestId>::some(TestId{1});
    Option<TestId> const two = Option<TestId>::some(TestId{2});
    Option<TestKey> const key = Option<TestKey>::some(KeyOwner::key_of_word(0x1000));
    return empty == Option<TestId>{} && !(empty == one) && one == Option<TestId>::some(TestId{1}) && !(one == two)
        && key == Option<TestKey>::some(KeyOwner::key_of_word(0x1000)) && !(key == Option<TestKey>{});
}
static_assert(constant_equality_agrees());

// The empty slot of the packed word holds the one word of niche_empty, all
// bits set, though the niche takes each word with the top bit set.
void packed_word_empty_is_one_pattern() noexcept {
    Option<Kinds> const empty{};
    std::uint16_t const all_bits = 0xFFFFu;
    CRUCIBLE_FATAL_INVARIANT(holds_same_bytes(empty, all_bits));
}

// The own == of each marked class agrees with the identity of its bytes, over
// fixed values and over Philox draws.
void marked_classes_compare_their_bytes() noexcept {
    crucible::test::PhiloxStream stream{0x5EEDBEEF5EEDBEEFull};
    for (int trial = 0; trial < 512; ++trial) {
        std::uint64_t const first = stream.below(64);
        std::uint64_t const second = trial % 4 == 0 ? first : stream.below(64);
        TestIndex const left = TestIndex::check(first).expect("the draw is below the count");
        TestIndex const right = TestIndex::check(second).expect("the draw is below the count");
        CRUCIBLE_FATAL_INVARIANT((left == right) == holds_same_bytes(left, right));
    }

    bool const is_address_right = with_line([&stream](auto run) noexcept {
        bool is_right = true;
        for (int trial = 0; trial < 512; ++trial) {
            std::uint64_t const first = stream.below(64);
            std::uint64_t const second = trial % 4 == 0 ? first : stream.below(64);
            RunAddress const left = mint_run_address(run, first).expect("the line holds 64 bytes");
            RunAddress const right = mint_run_address(run, second).expect("the line holds 64 bytes");
            is_right = is_right && (left == right) == holds_same_bytes(left, right);
        }
        return is_right;
    });
    CRUCIBLE_FATAL_INVARIANT(is_address_right);

    FixedArray<int, 64> const values{};
    bool const is_branded_right = ::foundation::core::mint_branded_scope(values.view(), [&stream](auto view) noexcept {
        bool is_right = true;
        for (int trial = 0; trial < 512; ++trial) {
            std::uint64_t const first = stream.below(64);
            std::uint64_t const second = trial % 4 == 0 ? first : stream.below(64);
            auto const left = view.index(first).expect("the draw is below the count");
            auto const right = view.index(second).expect("the draw is below the count");
            is_right = is_right && (left == right) == holds_same_bytes(left, right);
        }
        auto const low = view.index(0).expect("the view holds a first index");
        auto const high = view.index(63).expect("the view holds a last index");
        doors_leave_the_empty_bytes(low);
        equality_agrees(low, high);
        return is_right;
    });
    CRUCIBLE_FATAL_INVARIANT(is_branded_right);
}

}  // namespace

int main() {
    doors_leave_the_empty_bytes(TestId{7});
    equality_agrees(TestId{7}, TestId{8});

    doors_leave_the_empty_bytes(KeyOwner::key_of_word(0x1000));
    equality_agrees(KeyOwner::key_of_word(0x1000), KeyOwner::key_of_word(0x2000));

    Kinds const low_kinds = Kinds::from_bits(0x0001u).expect("the word holds only the bits of the fields");
    Kinds const high_kinds = Kinds::from_bits(0x7ABCu).expect("the word holds only the bits of the fields");
    doors_leave_the_empty_bytes(low_kinds);
    equality_agrees(low_kinds, high_kinds);
    packed_word_empty_is_one_pattern();

    TestIndex const low_index = TestIndex::check(std::size_t{3}).expect("3 is below the count");
    TestIndex const high_index = TestIndex::check(std::size_t{60}).expect("60 is below the count");
    doors_leave_the_empty_bytes(low_index);
    equality_agrees(low_index, high_index);

    with_line([](auto run) noexcept {
        RunAddress const low_address = mint_run_address(run, 0).expect("the line holds 64 bytes");
        RunAddress const high_address = mint_run_address(run, 4).expect("the line holds 64 bytes");
        doors_leave_the_empty_bytes(low_address);
        equality_agrees(low_address, high_address);
        return 0;
    });

    marked_classes_compare_their_bytes();
    ::foundation::core::report(::foundation::core::Sink::Out, "test_core_option_niche_equality: all tests passed\n");
    return 0;
}
