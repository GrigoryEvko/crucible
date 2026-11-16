// Tests of foundation/core/Ref.h: to, Ref, same_object, address_of, prefetch
// and Option<Ref<T, Brand>>.

#include <foundation/core/Ref.h>

#include <foundation/Platform.h>
#include <foundation/core/Choice.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

using ::foundation::core::Address;
using ::foundation::core::address_of;
using ::foundation::core::AddressKey;
using ::foundation::core::mint_address_key;
using ::foundation::core::none;
using ::foundation::core::Option;
using ::foundation::core::prefetch;
using ::foundation::core::prefetch_byte;
using ::foundation::core::PrefetchAccess;
using ::foundation::core::Ref;
using ::foundation::core::same_object;
using ::foundation::core::to;

// The brand of a Ref, for a decltype only.
template <class T, class Brand>
Brand brand_of(Ref<T, Brand>);

struct Pair {
    int first = 0;
    int second = 0;
};

// A class whose operator& gives no address.  to() takes the address of the
// object and not the result of the operator.
struct HidesAddress {
    int value = 0;
    int operator&() const noexcept { return 0; }
};

struct Shape {
    virtual ~Shape() = default;
    [[nodiscard]] virtual int corners() const noexcept = 0;
};

struct Square final : Shape {
    [[nodiscard]] int corners() const noexcept override { return 4; }
};

void test_reach_the_object() {
    int number = 41;
    auto const ref = to(number);
    *ref += 1;
    CRUCIBLE_FATAL_INVARIANT(number == 42);
    CRUCIBLE_FATAL_INVARIANT(*ref == 42);

    Pair pair{3, 4};
    auto const pair_ref = to(pair);
    pair_ref->second = 5;
    CRUCIBLE_FATAL_INVARIANT(pair.second == 5 && (*pair_ref).first == 3);
}

// Each call of to() gives a brand of its own, and no call gives the erased
// brand.
void test_fresh_brand() {
    int number = 1;
    auto const first = to(number);
    auto const second = to(number);
    using FirstBrand = decltype(brand_of(first));
    using SecondBrand = decltype(brand_of(second));
    static_assert(!std::is_same_v<FirstBrand, SecondBrand>);
    static_assert(::foundation::brand::IsFreshBrand<FirstBrand>);
    static_assert(!std::is_convertible_v<decltype(first), decltype(second)>);
    CRUCIBLE_FATAL_INVARIANT(same_object(first, second));
}

// A const target keeps the brand.
void test_const_conversion() {
    int number = 7;
    auto const ref = to(number);
    using Brand = decltype(brand_of(ref));
    Ref<int const, Brand> const fixed = ref;
    CRUCIBLE_FATAL_INVARIANT(*fixed == 7);
    number = 8;
    CRUCIBLE_FATAL_INVARIANT(*fixed == 8);
    CRUCIBLE_FATAL_INVARIANT(same_object(ref, fixed));
    static_assert(!std::is_convertible_v<Ref<int const, Brand>, Ref<int, Brand>>);

    int const constant = 3;
    auto const constant_ref = to(constant);
    static_assert(std::is_same_v<decltype(*constant_ref), int const&>);
}

void test_same_object() {
    int first = 1;
    int second = 1;
    auto const first_ref = to(first);
    auto const first_again = to(first);
    auto const second_ref = to(second);
    CRUCIBLE_FATAL_INVARIANT(same_object(first_ref, first_again));
    CRUCIBLE_FATAL_INVARIANT(!same_object(first_ref, second_ref));

    HidesAddress hidden{9};
    auto const hidden_ref = to(hidden);
    CRUCIBLE_FATAL_INVARIANT(hidden_ref->value == 9);
    CRUCIBLE_FATAL_INVARIANT(same_object(hidden_ref, to(hidden)));
}

// A copy reaches the same object, and an assignment of a Ref of the same
// brand makes the target reach the object of the source.  No function gives
// back a fresh brand, so the two Refs of one brand come from one call.
void test_copy_and_assign() {
    int number = 1;
    auto ref = to(number);
    auto const copy = ref;
    CRUCIBLE_FATAL_INVARIANT(same_object(ref, copy));
    ref = copy;
    *ref = 2;
    CRUCIBLE_FATAL_INVARIANT(*copy == 2 && number == 2);
}

// A Ref of an abstract class reaches the object of the derived class.
void test_abstract_target() {
    Square square{};
    Shape const& shape_of_square = square;
    auto const shape = to(shape_of_square);
    CRUCIBLE_FATAL_INVARIANT(shape->corners() == 4);
}

void test_option_of_ref() {
    int number = 5;
    auto const ref = to(number);
    using IntRef = std::remove_const_t<decltype(ref)>;
    Option<IntRef> empty{none};
    CRUCIBLE_FATAL_INVARIANT(!empty.is_some());
    Option<IntRef> held = Option<IntRef>::some(ref);
    CRUCIBLE_FATAL_INVARIANT(held.is_some());
    int turns = 0;
    for (IntRef const& reached : held) {
        *reached = 6;
        ++turns;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 1 && number == 6);
    IntRef const taken = static_cast<Option<IntRef>&&>(held).expect("the Option holds a Ref");
    CRUCIBLE_FATAL_INVARIANT(same_object(taken, ref));
}

struct alignas(64) LineAligned {
    std::uint64_t value = 0;
};

// Two adjacent bytes: one of the two is not aligned to 2.
struct TwoBytes {
    unsigned char low = 0;
    unsigned char high = 0;
};

// The address of an object answers the alignment of its type, and the
// address of one object compares equal only with itself.
void test_address() {
    LineAligned line{};
    Address const line_address = address_of(to(line));
    CRUCIBLE_FATAL_INVARIANT(line_address.is_aligned<1>());
    CRUCIBLE_FATAL_INVARIANT(line_address.is_aligned<8>());
    CRUCIBLE_FATAL_INVARIANT(line_address.is_aligned<64>());

    TwoBytes bytes{};
    bool const low_is_even = address_of(to(bytes.low)).is_aligned<2>();
    bool const high_is_even = address_of(to(bytes.high)).is_aligned<2>();
    CRUCIBLE_FATAL_INVARIANT(low_is_even != high_is_even);

    int number = 1;
    int other = 2;
    int const& number_read = number;
    CRUCIBLE_FATAL_INVARIANT(address_of(to(number)) == address_of(to(number_read)));
    CRUCIBLE_FATAL_INVARIANT(!(address_of(to(number)) == address_of(to(other))));
    CRUCIBLE_FATAL_INVARIANT(address_of(to(number)).is_aligned<alignof(int)>());
}

// The key of an address is equal to each key of the same address, through a
// typed pointer, a void pointer, a const pointer or a copy, and it gives the
// same hash.  The keys of two objects differ, and the key of the first
// member of a class is the key of the class.  The key of a null pointer is
// equal only to the key of a null pointer, and its hash is zero.
void test_address_key() {
    Pair pair{3, 4};
    Pair const& pair_read = pair;
    AddressKey const of_pair = mint_address_key(&pair);
    AddressKey const of_read = mint_address_key(&pair_read);
    AddressKey const of_void = mint_address_key(static_cast<void const*>(&pair));
    AddressKey const of_first = mint_address_key(&pair.first);
    AddressKey const of_second = mint_address_key(&pair.second);
    AddressKey copy = of_second;
    copy = of_pair;
    CRUCIBLE_FATAL_INVARIANT(of_pair == of_read && of_pair == of_void && of_pair == of_first && copy == of_pair);
    CRUCIBLE_FATAL_INVARIANT(of_pair.hash() == of_read.hash() && of_pair.hash() == of_void.hash());
    CRUCIBLE_FATAL_INVARIANT(copy.hash() == of_pair.hash());
    CRUCIBLE_FATAL_INVARIANT(!(of_first == of_second));

    AddressKey const of_null = mint_address_key(static_cast<int const*>(nullptr));
    CRUCIBLE_FATAL_INVARIANT(of_null == mint_address_key(static_cast<void const*>(nullptr)));
    CRUCIBLE_FATAL_INVARIANT(!(of_null == of_pair));
    CRUCIBLE_FATAL_INVARIANT(of_null.hash() == 0);

    // A key reads no object, so the pointer one past the end of an object
    // gives a key too, and it differs from the key of each member.
    AddressKey const past_end = mint_address_key(&pair + 1);
    CRUCIBLE_FATAL_INVARIANT(!(past_end == of_first) && !(past_end == of_second));
}

// True when a prefetch of a named object of type T compiles.
template <PrefetchAccess Access, int Locality, class T>
concept PrefetchesObject = requires(T const& object) { prefetch<Access, Locality>(object); };

// True when a prefetch of a temporary of type T compiles.
template <PrefetchAccess Access, int Locality, class T>
concept PrefetchesTemporary = requires { prefetch<Access, Locality>(T{}); };

// True when a prefetch of the byte at Offset of a named object of type T
// compiles.
template <PrefetchAccess Access, int Locality, std::size_t Offset, class T>
concept PrefetchesByte = requires(T const& object) { prefetch_byte<Access, Locality, Offset>(object); };

// True when a prefetch of the byte at Offset of a temporary compiles.
template <PrefetchAccess Access, int Locality, std::size_t Offset, class T>
concept PrefetchesByteOfTemporary = requires { prefetch_byte<Access, Locality, Offset>(T{}); };

// A prefetch compiles for each access and each locality from 0 to 3, and it
// changes no value.  A locality outside 0 to 3, an access outside the two
// enumerators and a temporary do not compile.
void test_prefetch() {
    Pair pair{3, 4};
    LineAligned line{};
    line.value = 7;
    HidesAddress hidden{9};
    prefetch<PrefetchAccess::read, 0>(pair);
    prefetch<PrefetchAccess::read, 1>(pair.first);
    prefetch<PrefetchAccess::read, 2>(line);
    prefetch<PrefetchAccess::read, 3>(hidden);
    prefetch<PrefetchAccess::write, 0>(pair.second);
    prefetch<PrefetchAccess::write, 1>(line.value);
    prefetch<PrefetchAccess::write, 2>(hidden);
    prefetch<PrefetchAccess::write, 3>(line);
    CRUCIBLE_FATAL_INVARIANT(pair.first == 3 && pair.second == 4);
    CRUCIBLE_FATAL_INVARIANT(line.value == 7 && hidden.value == 9);

    static_assert(PrefetchesObject<PrefetchAccess::read, 0, Pair>);
    static_assert(PrefetchesObject<PrefetchAccess::write, 3, Pair>);
    static_assert(!PrefetchesObject<PrefetchAccess::read, 4, Pair>);
    static_assert(!PrefetchesObject<PrefetchAccess::write, -1, Pair>);
    static_assert(!PrefetchesObject<static_cast<PrefetchAccess>(2), 3, Pair>);
    static_assert(!PrefetchesTemporary<PrefetchAccess::read, 3, Pair>);
    static_assert(!PrefetchesTemporary<PrefetchAccess::write, 0, Pair>);
}

// Two lines: the second line starts at byte 64.
struct TwoLines {
    LineAligned first{};
    LineAligned second{};
};

// A prefetch of a byte of an object compiles for each offset below the size
// of the object, and it changes no value.  An offset at or past the size, a
// locality outside 0 to 3 and a temporary do not compile.
void test_prefetch_byte() {
    TwoLines lines{};
    lines.first.value = 11;
    lines.second.value = 12;
    Pair pair{3, 4};
    prefetch_byte<PrefetchAccess::read, 3, 64>(lines);
    prefetch_byte<PrefetchAccess::write, 0, 127>(lines);
    prefetch_byte<PrefetchAccess::read, 1, 0>(pair);
    prefetch_byte<PrefetchAccess::read, 2, sizeof(Pair) - 1>(pair);
    CRUCIBLE_FATAL_INVARIANT(lines.first.value == 11 && lines.second.value == 12);
    CRUCIBLE_FATAL_INVARIANT(pair.first == 3 && pair.second == 4);

    static_assert(PrefetchesByte<PrefetchAccess::read, 3, 64, TwoLines>);
    static_assert(PrefetchesByte<PrefetchAccess::write, 0, sizeof(TwoLines) - 1, TwoLines>);
    static_assert(!PrefetchesByte<PrefetchAccess::read, 3, sizeof(TwoLines), TwoLines>);
    static_assert(!PrefetchesByte<PrefetchAccess::read, 3, sizeof(Pair), Pair>);
    static_assert(!PrefetchesByte<PrefetchAccess::read, 4, 0, Pair>);
    static_assert(!PrefetchesByteOfTemporary<PrefetchAccess::read, 3, 0, Pair>);
}

// Four slots, which a plain index selects.
struct Slots {
    std::uint64_t zero = 0;
    std::uint64_t one = 3;
    std::uint64_t two = 6;
    std::uint64_t three = 9;
};

[[nodiscard]] std::uint64_t& slot_object(Slots& slots, std::uint64_t index) noexcept {
    switch (index) {
        case 0:
            return slots.zero;
        case 1:
            return slots.one;
        case 2:
            return slots.two;
        default:
            return slots.three;
    }
}

// The property: Refs to random slots reach the slot that a plain index
// selects, and same_object agrees with the equality of the indices.  The
// second Ref reaches its slot through a const view of the slot.
void test_agrees_with_plain_model() {
    Slots slots{};
    ::crucible::test::PhiloxStream stream{0x5EEDB0C5000000C3u};
    for (int trial = 0; trial < 512; ++trial) {
        std::uint64_t const first_index = stream.below(4);
        std::uint64_t const second_index = stream.below(4);
        auto const first = to(slot_object(slots, first_index));
        std::uint64_t const& second_slot = slot_object(slots, second_index);
        auto const second = to(second_slot);
        CRUCIBLE_FATAL_INVARIANT(*first == first_index * 3);
        CRUCIBLE_FATAL_INVARIANT(same_object(first, second) == (first_index == second_index));
    }
}

}  // namespace

int main() {
    test_reach_the_object();
    test_fresh_brand();
    test_const_conversion();
    test_same_object();
    test_copy_and_assign();
    test_abstract_target();
    test_option_of_ref();
    test_address();
    test_address_key();
    test_prefetch();
    test_prefetch_byte();
    test_agrees_with_plain_model();
    return 0;
}
