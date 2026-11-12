// Tests of foundation/core/Record.h: Record and Pack.

#include <foundation/core/Record.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>

#include "philox_stream.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::Pack;
using ::foundation::core::Record;

// The count of live CountedPart objects.  A Record must destroy each part
// one time and leak none.
int live_parts = 0;

// A part that only moves, and that counts its live objects.
class CountedPart {
public:
    explicit CountedPart(int label) noexcept : label_{label} { ++live_parts; }
    CountedPart(CountedPart&& other) noexcept : label_{other.label_} {
        other.label_ = 0;
        ++live_parts;
    }
    CountedPart& operator=(CountedPart&& other) noexcept {
        label_ = other.label_;
        other.label_ = 0;
        return *this;
    }
    CountedPart(CountedPart const&) = delete;
    CountedPart& operator=(CountedPart const&) = delete;
    ~CountedPart() { --live_parts; }

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};

// A part with a stable address.  A prvalue builds it in place.
struct Anchored : ::foundation::Pinned<Anchored> {
    int label = 0;
};

[[nodiscard]] Record<Anchored, int> make_anchored(int label, int next) noexcept { return {Anchored{{}, label}, next}; }

[[nodiscard]] Record<std::uint32_t, std::uint64_t> make_pair(std::uint32_t first, std::uint64_t second) noexcept {
    return {first, second};
}

void test_initialization() {
    Record<std::uint32_t, std::uint64_t> const built{7u, 9u};
    auto const& [first, second] = built;
    CRUCIBLE_FATAL_INVARIANT(first == 7u && second == 9u);

    // The default member initializer of each cell gives each part its value
    // initialization.
    Record<int, int> zeros;
    auto const& [left, right] = zeros;
    CRUCIBLE_FATAL_INVARIANT(left == 0 && right == 0);

    // The deduction guide gives the parts the types of the arguments.
    Record guided{std::uint32_t{3}, 4.5};
    static_assert(std::is_same_v<decltype(guided), Record<std::uint32_t, double>>);
    static_assert(Record<int, int, int>::size() == 3);
    static_assert(std::is_same_v<Record<int, double>::types, Pack<int, double>>);
    CRUCIBLE_FATAL_INVARIANT(guided.size() == 2);
}

void test_bindings() {
    Record<int, int> held{1, 2};

    // A binding by value binds the names to a copy.
    auto [copied_first, copied_second] = held;
    copied_first = 10;
    CRUCIBLE_FATAL_INVARIANT(copied_second == 2);

    // A binding by reference binds the names to the parts.
    auto& [first, second] = held;
    first = 11;
    second = 12;
    auto const& [read_first, read_second] = held;
    CRUCIBLE_FATAL_INVARIANT(read_first == 11 && read_second == 12 && copied_first == 10);

    // A binding of a temporary by a const reference lives to the end of the
    // block.
    auto const& [low, high] = make_pair(5u, 6u);
    CRUCIBLE_FATAL_INVARIANT(low == 5u && high == 6u);
}

void test_binding_moves_each_part_one_time() {
    {
        Record<CountedPart, CountedPart> owners{CountedPart{1}, CountedPart{2}};
        CRUCIBLE_FATAL_INVARIANT(live_parts == 2);
        auto [left, right] = std::move(owners);
        CountedPart const taken_left{std::move(left)};
        CountedPart const taken_right{std::move(right)};
        CRUCIBLE_FATAL_INVARIANT(taken_left.label() == 1 && taken_right.label() == 2);
    }
    CRUCIBLE_FATAL_INVARIANT(live_parts == 0);
}

}  // namespace

// A namespace of a part with an unconstrained get.  Argument-dependent
// lookup finds it beside the hook of the Record.
namespace record_hijack {

struct Part final {
    int value = 0;
};

template <std::size_t I, class T>
[[nodiscard]] constexpr int get(T&&) noexcept {
    return -1;
}

}  // namespace record_hijack

namespace {

// The hook of the Record is more specialized than the foreign get, so each
// binding takes the hook.  A binding through the foreign get would give an
// int, and part.value would not compile.
void test_hook_wins_against_a_foreign_get() {
    Record<record_hijack::Part, int> held{record_hijack::Part{5}, 6};
    auto const& [part, number] = held;
    CRUCIBLE_FATAL_INVARIANT(part.value == 5 && number == 6);
    auto [moved_part, moved_number] = std::move(held);
    CRUCIBLE_FATAL_INVARIANT(moved_part.value == 5 && moved_number == 6);
}

// A binding pack takes the place of std::apply.  GCC 16 permits a binding
// pack only in a template.
template <class R>
[[nodiscard]] int sum_of_parts(R const& record) noexcept {
    auto const& [... parts] = record;
    return (0 + ... + parts);
}

template <class R>
[[nodiscard]] std::size_t count_of_parts(R const& record) noexcept {
    [[maybe_unused]] auto const& [... parts] = record;
    return sizeof...(parts);
}

void test_binding_pack() {
    CRUCIBLE_FATAL_INVARIANT(sum_of_parts(Record<int, int, int>{1, 2, 3}) == 6);
    static_assert(Record<>::size() == 0);
    CRUCIBLE_FATAL_INVARIANT(count_of_parts(Record<>{}) == 0);
    CRUCIBLE_FATAL_INVARIANT(count_of_parts(Record<int, int, int>{}) == 3);
}

void test_compare() {
    Record<int, int> const low{1, 9};
    Record<int, int> const high{2, 0};
    CRUCIBLE_FATAL_INVARIANT(low == low && low != high);
    CRUCIBLE_FATAL_INVARIANT(std::is_lt(low <=> high) && std::is_gt(high <=> low));
    CRUCIBLE_FATAL_INVARIANT(std::is_eq(low <=> Record<int, int>{1, 9}));
    static_assert(std::is_same_v<decltype(low <=> high), std::strong_ordering>);

    // Each part takes part in the compare, the last part too.
    Record<int, int, int> const first_three{1, 2, 3};
    Record<int, int, int> const last_differs{1, 2, 4};
    CRUCIBLE_FATAL_INVARIANT(first_three != last_differs && std::is_lt(first_three <=> last_differs));

    // A Record of no part equals each other Record of no part.
    CRUCIBLE_FATAL_INVARIANT(Record<>{} == Record<>{} && std::is_eq(Record<>{} <=> Record<>{}));

    // A floating part gives no ==, and <=> gives a partial order.  A NaN
    // part makes the two Records unordered: no order and no equivalence.
    double const huge = 1e308;
    double const infinity = huge * huge;
    double const not_a_number = infinity - infinity;
    Record<int, double> const holds_nan{1, not_a_number};
    static_assert(std::is_same_v<decltype(holds_nan <=> holds_nan), std::partial_ordering>);
    std::partial_ordering const order = holds_nan <=> holds_nan;
    CRUCIBLE_FATAL_INVARIANT(!std::is_lt(order) && !std::is_eq(order) && !std::is_gt(order));
    Record<int, double> const ordered{1, 2.5};
    CRUCIBLE_FATAL_INVARIANT(std::is_eq(ordered <=> Record<int, double>{1, 2.5}));
}

void test_pinned_part() {
    Record<Anchored, int> const held = make_anchored(4, 5);
    auto const& [anchored, next] = held;
    CRUCIBLE_FATAL_INVARIANT(anchored.label == 4 && next == 5);
}

// A cursor that gives a Record by value, as an iteration verb of a region
// gives the pair of an index and an element.
struct IndexedCursor final {
    std::size_t index = 0;

    [[nodiscard]] Record<std::size_t, CountedPart> operator*() const noexcept {
        return {index, CountedPart{static_cast<int>(index) * 10}};
    }
    IndexedCursor& operator++() noexcept {
        ++index;
        return *this;
    }
    [[nodiscard]] friend bool operator==(IndexedCursor const&, IndexedCursor const&) noexcept = default;
};

struct IndexedRange final {
    std::size_t count = 0;

    [[nodiscard]] IndexedCursor begin() const noexcept { return IndexedCursor{0}; }
    [[nodiscard]] IndexedCursor end() const noexcept { return IndexedCursor{count}; }
};

void test_pairs_of_an_iteration() {
    {
        int total = 0;
        std::size_t visits = 0;
        for (auto [index, element] : IndexedRange{4}) {
            CountedPart const taken{std::move(element)};
            CRUCIBLE_FATAL_INVARIANT(taken.label() == static_cast<int>(index) * 10);
            total += taken.label();
            ++visits;
        }
        CRUCIBLE_FATAL_INVARIANT(visits == 4 && total == 60);
    }
    CRUCIBLE_FATAL_INVARIANT(live_parts == 0);
}

// The lexicographic key of a Record whose parts each hold 0, 1 or 2: the
// parts are the digits of a number in base 3, the first part the most
// significant.  The order of two keys is the lexicographic order of the
// parts, so the key is a model that does not share the code of the compares.
template <class R>
[[nodiscard]] std::uint64_t lexicographic_key(R const& record) noexcept {
    auto const& [... parts] = record;
    std::uint64_t key = 0;
    ((key = key * 3u + static_cast<std::uint64_t>(parts)), ...);
    return key;
}

// The property: for Records of random parts, a binding gives back each part
// in order, == and <=> agree with the model, and a copy is equal.  A second
// stream with the same seed gives the values again.  Linear in the count of
// trials.
template <class... Ts>
void check_against_the_model(std::uint64_t seed, int trials) {
    ::foundation::test::PhiloxStream stream{seed};
    ::foundation::test::PhiloxStream replay{seed};
    for (int trial = 0; trial < trials; ++trial) {
        Record<Ts...> const left{static_cast<Ts>(stream.below(3))...};
        Record<Ts...> const right{static_cast<Ts>(stream.below(3))...};
        auto const& [... left_parts] = left;
        bool const are_left_parts_back = ((left_parts == static_cast<Ts>(replay.below(3))) && ...);
        auto const& [... right_parts] = right;
        bool const are_right_parts_back = ((right_parts == static_cast<Ts>(replay.below(3))) && ...);
        CRUCIBLE_FATAL_INVARIANT(are_left_parts_back && are_right_parts_back);

        std::uint64_t const left_key = lexicographic_key(left);
        std::uint64_t const right_key = lexicographic_key(right);
        std::strong_ordering const order = left <=> right;
        CRUCIBLE_FATAL_INVARIANT((left == right) == (left_key == right_key));
        CRUCIBLE_FATAL_INVARIANT(std::is_lt(order) == (left_key < right_key));
        CRUCIBLE_FATAL_INVARIANT(std::is_gt(order) == (left_key > right_key));
        Record<Ts...> const copy = left;
        CRUCIBLE_FATAL_INVARIANT(copy == left && std::is_eq(copy <=> left));
    }
}

void test_agrees_with_the_model() {
    constexpr int trials = 17000;
    check_against_the_model<std::uint8_t>(0x5EEDC0DE0000A001u, trials);
    check_against_the_model<std::uint8_t, std::uint16_t>(0x5EEDC0DE0000A002u, trials);
    check_against_the_model<std::uint8_t, std::uint16_t, std::uint32_t>(0x5EEDC0DE0000A003u, trials);
    check_against_the_model<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t>(0x5EEDC0DE0000A004u, trials);
    check_against_the_model<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, std::int8_t>(0x5EEDC0DE0000A005u,
                                                                                                    trials);
    check_against_the_model<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, std::int8_t, std::int32_t>(
        0x5EEDC0DE0000A006u, trials);
}

}  // namespace

int main() {
    test_initialization();
    test_bindings();
    test_binding_moves_each_part_one_time();
    test_binding_pack();
    test_compare();
    test_pinned_part();
    test_pairs_of_an_iteration();
    test_hook_wins_against_a_foreign_get();
    test_agrees_with_the_model();
    return 0;
}
