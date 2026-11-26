// The compile-time checks of foundation/core/Equatable.h.

#include <foundation/core/Equatable.h>

#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/List.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Record.h>

#include <compare>
#include <cstdint>

namespace foundation::core {

namespace detail::equatable_checks {

enum class Mode : std::uint8_t {
    off,
    on,
};

// A class that takes its operations from its parts.  A private member is a
// part too, because the annotation consents to the walk.
class[[= derive_protocol{}]] Sealed {
public:
    constexpr Sealed() noexcept = default;
    constexpr Sealed(std::uint32_t word, Mode mode) noexcept : word_{word}, mode_{mode} {}

private:
    std::uint32_t word_ = 0;
    Mode mode_ = Mode::off;
};

struct[[= derive_protocol{}]] Low {
    std::uint16_t low = 0;
};

// A base comes first, then the members in declaration order, with an array,
// a float, a local part and a pad part.  test_core_protocol holds the parts
// of a C array and of a pointer.
struct[[= derive_protocol{}]] Wide : Low {
    Sealed sealed{};
    FixedArray<std::int32_t, 3> lanes{};
    float scale = 0.0f;
    [[= local_part{}]] std::uint64_t address = 0;
    [[= pad_part{}]] FixedArray<std::uint8_t, 4> pad{};
};

// A class with no annotation and no ==.
struct Plain {
    int value = 0;
};

// A class with no annotation and its own operators.
struct OwnCompare {
    int value = 0;
    friend constexpr bool operator==(OwnCompare, OwnCompare) noexcept = default;
    friend constexpr std::strong_ordering operator<=>(OwnCompare, OwnCompare) noexcept = default;
};

// A class whose == compares a double by IEEE 754 on purpose, and whose <=>
// gives a partial order, which does not agree with ==.
struct[[= ieee_equality{}]] PartialCompare {
    double value = 0.0;
    friend constexpr bool operator==(PartialCompare left, PartialCompare right) noexcept {
        return left.value <= right.value && right.value <= left.value;
    }
    friend constexpr std::partial_ordering operator<=>(PartialCompare left, PartialCompare right) noexcept {
        return left.value <=> right.value;
    }
};

// A class whose defaulted == compares a float by IEEE 754 and states no rule.
struct IeeeCompare {
    float value = 0.0f;
    friend constexpr bool operator==(IeeeCompare, IeeeCompare) noexcept = default;
};

// The walk reads each floating type, not only float and double.  The types
// of the two literals are std::float16_t and std::bfloat16_t.
using Half = decltype(0.0f16);
using BrainHalf = decltype(0.0bf16);

struct HoldsHalf {
    Half value = 0.0f16;
    friend constexpr bool operator==(HoldsHalf, HoldsHalf) noexcept = default;
};

struct HoldsBrainHalf {
    BrainHalf value = 0.0bf16;
    friend constexpr bool operator==(HoldsBrainHalf, HoldsBrainHalf) noexcept = default;
};

// A long double member, and the same members under the mark ieee_equality:
// a PartialEquatable class.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the float rule of Equatable reads a long double member")
struct HoldsLongDouble {
    long double value = 0.0L;
    friend constexpr bool operator==(HoldsLongDouble const&, HoldsLongDouble const&) noexcept = default;
};

struct[[= ieee_equality{}]] StatesWideFloats {
    long double wide = 0.0L;
    Half half = 0.0f16;
    BrainHalf brain = 0.0bf16;
    friend constexpr bool operator==(StatesWideFloats const&, StatesWideFloats const&) noexcept = default;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The walk reads a part of a part and a base.  test_core_protocol holds the
// walk over a C array.
struct HoldsIeeeCompare {
    std::uint32_t word = 0;
    IeeeCompare inner{};
    friend constexpr bool operator==(HoldsIeeeCompare const&, HoldsIeeeCompare const&) noexcept = default;
};

struct DerivesIeeeCompare : IeeeCompare {
    friend constexpr bool operator==(DerivesIeeeCompare const&, DerivesIeeeCompare const&) noexcept = default;
};

// derive_protocol does not hide an own == that compares a float by IEEE 754,
// because a caller of == gets that compare and not the float rule.
struct[[= derive_protocol{}]] DerivesWithIeeeCompare {
    double value = 0.0;
    friend constexpr bool operator==(DerivesWithIeeeCompare, DerivesWithIeeeCompare) noexcept = default;
};

// A FixedArray carries equal_parts: its == compares the elements with equal,
// so the walk stops at it.
struct HoldsFloatLanes {
    FixedArray<double, 2> lanes{};
    friend constexpr bool operator==(HoldsFloatLanes const&, HoldsFloatLanes const&) noexcept = default;
};

// A class of the base with the mark equal_parts and its own == over a double.
// The walk trusts the mark in the base, so the class is Equatable.
// test_core_protocol and a fixture hold the same mark outside the base.
struct[[= equal_parts{}]] StatesEqualParts {
    double value = 0.0;
    friend constexpr bool operator==(StatesEqualParts const& left, StatesEqualParts const& right) noexcept {
        return equal(left.value, right.value);
    }
};

// The error of a Result in the pins of the float rule.
enum class PinFault : std::uint8_t {
    lost,
};

// An own == can read a float member of a union part, so the walk reads each
// member of a union: a double in a named union, in an unnamed union, or in a
// union with its own ==.  Each == is the == of IEEE 754, written as two
// orders.  A union of integers stays Equatable.
struct HoldsUnionDouble {
    union Cell {
        double value;
        std::uint64_t word;
    };
    Cell cell{.value = 0.0};
    friend constexpr bool operator==(HoldsUnionDouble const& left, HoldsUnionDouble const& right) noexcept {
        return left.cell.value <= right.cell.value && right.cell.value <= left.cell.value;
    }
};

struct HoldsUnnamedUnion {
    union {
        double value = 0.0;
        std::uint64_t word;
    };
    friend constexpr bool operator==(HoldsUnnamedUnion const& left, HoldsUnnamedUnion const& right) noexcept {
        return left.value <= right.value && right.value <= left.value;
    }
};

union ComparesOwnDouble {
    double value;
    std::uint64_t word;
    friend constexpr bool operator==(ComparesOwnDouble const& left, ComparesOwnDouble const& right) noexcept {
        return left.value <= right.value && right.value <= left.value;
    }
};

struct HoldsUnionWords {
    union Cell {
        std::uint32_t low;
        std::uint64_t word;
    };
    Cell cell{.word = 0};
    friend constexpr bool operator==(HoldsUnionWords const& left, HoldsUnionWords const& right) noexcept {
        return left.cell.word == right.cell.word;
    }
};

// The same union under the mark ieee_equality: a PartialEquatable union.
union[[= ieee_equality{}]] StatesOwnDouble {
    double value;
    std::uint64_t word;
    friend constexpr bool operator==(StatesOwnDouble const& left, StatesOwnDouble const& right) noexcept {
        return left.value <= right.value && right.value <= left.value;
    }
};

// Option, Result and OneOf keep their storage unions inside a class of the
// base with the mark equal_parts, so the walk of the float rule stops at the
// sum before it reads the union.
struct HoldsFloatSums {
    Option<double> maybe{};
    Result<double, PinFault> outcome{0.0};
    OneOf<double, int> either{0.0};
    friend constexpr bool operator==(HoldsFloatSums const&, HoldsFloatSums const&) noexcept = default;
};

// A class whose == can throw.
struct ThrowingEqual {
    int value = 0;
    friend constexpr bool operator==(ThrowingEqual left, ThrowingEqual right) { return left.value == right.value; }
};

// A named bit-field has no byte offset, so the walk refuses it.
struct[[= derive_protocol{}]] HoldsBitField {
    std::uint32_t flag : 1 = 0;
};

// Each instance of a class template with the annotation carries it, so the
// walk reads its parts.
template <class Part>
struct[[= derive_protocol{}]] HoldsPart {
    Part part{};
};

// The leaves.
static_assert(Equatable<int> && Equatable<bool> && Equatable<char> && Equatable<Mode> && Equatable<int const>);
static_assert(Orderable<int> && Orderable<bool> && Orderable<char> && Orderable<Mode>);
static_assert(Equatable<float> && Equatable<double> && Orderable<float> && Orderable<double>);
static_assert(!Equatable<long double> && !Orderable<long double>);
static_assert(Equatable<int*> && Equatable<decltype(nullptr)> && !Orderable<int*>);
static_assert(!Equatable<int&> && !Equatable<void> && !Equatable<int()>);
static_assert(Equatable<int[4]> && Orderable<double[2][3]> && !Equatable<int[]>);

// A plain char orders by its bits as a value with no sign, so the order is
// the same on x86_64, where char has a sign, and on aarch64.
static_assert(std::is_gt(order(static_cast<char>(-1), char{0})));
static_assert(std::is_gt(order(static_cast<wchar_t>(-1), wchar_t{0})));
static_assert(std::is_lt(order(static_cast<signed char>(-1), static_cast<signed char>(0))));

// The classes.
static_assert(Equatable<Sealed> && Orderable<Sealed> && Equatable<Wide> && Orderable<Wide>);
static_assert(!Equatable<Plain> && !Orderable<Plain>);
static_assert(Equatable<OwnCompare> && Orderable<OwnCompare>);
static_assert(!Equatable<ThrowingEqual>);

// The float rule of a class with its own ==.  A class that compares a float
// by IEEE 754 is no Equatable type.  With ieee_equality it is PartialEquatable.
static_assert(!Equatable<PartialCompare> && PartialEquatable<PartialCompare> && !Orderable<PartialCompare>);
static_assert(!Equatable<IeeeCompare> && !PartialEquatable<IeeeCompare>);
static_assert(!Equatable<HoldsLongDouble> && !Equatable<HoldsHalf> && !Equatable<HoldsBrainHalf>);
static_assert(!Equatable<StatesWideFloats> && PartialEquatable<StatesWideFloats>);
static_assert(!Equatable<HoldsIeeeCompare> && !Equatable<DerivesIeeeCompare> && !Equatable<DerivesWithIeeeCompare>);
static_assert(Equatable<HoldsPart<double>> && Equatable<HoldsPart<HoldsFloatLanes>>);
static_assert(Equatable<HoldsFloatLanes> && Equatable<FixedArray<double, 2>> && !Equatable<FixedArray<IeeeCompare, 1>>);
static_assert(PartialEquatable<int> && PartialEquatable<Sealed> && PartialEquatable<OwnCompare>);
static_assert(!PartialEquatable<Plain> && !PartialEquatable<ThrowingEqual>);
static_assert(!Equatable<HoldsBitField> && !Orderable<HoldsBitField>);
static_assert(Equatable<HoldsPart<int>> && Orderable<HoldsPart<int>> && Equatable<HoldsPart<Wide>>);
static_assert(std::is_lt(order(HoldsPart<int>{1}, HoldsPart<int>{2})) && equal(HoldsPart<int>{3}, HoldsPart<int>{3}));

// The float rule.  equal is the identity of the bits: +0 and -0 differ, and
// the canonical NaN equals itself.  order is the totalOrder of IEEE 754 and
// agrees with equal.
static_assert(!equal(0.0, -0.0) && !equal(0.0f, -0.0f));
static_assert(equal(quiet_nan_of<double>, quiet_nan_of<double>) && equal(quiet_nan_of<float>, quiet_nan_of<float>));
static_assert(!equal(quiet_nan_of<double>, signaling_nan_of<double>));
static_assert(std::is_lt(order(-0.0, 0.0)) && std::is_gt(order(0.0f, -0.0f)));
static_assert(std::is_eq(order(quiet_nan_of<double>, quiet_nan_of<double>)));
static_assert(std::is_gt(order(quiet_nan_of<double>, infinity_of<double>)));
static_assert(std::is_lt(order(-quiet_nan_of<double>, -infinity_of<double>)));
static_assert(std::is_lt(order(-infinity_of<float>, -1.0f)) && std::is_lt(order(-1.0f, -0.0f)));
static_assert(std::is_lt(order(1.0, 2.0)) && std::is_lt(order(-2.0, -1.0))
              && std::is_lt(order(2.0, infinity_of<double>)));
static_assert(std::is_lt(order(min_subnormal_of<double>, min_normal_of<double>)));

// The mark equal_parts on a class of the base: the walk stops at it, and its
// == compares the double by the float rule.
static_assert(Equatable<StatesEqualParts>);
static_assert(Equatable<Option<double>> && Equatable<Result<double, PinFault>> && Equatable<OneOf<double, int>>);
static_assert(Equatable<HoldsFloatSums>);

// The walk reads each member of a union part, and a union with its own ==.
static_assert(!Equatable<HoldsUnionDouble> && !Equatable<HoldsUnnamedUnion> && !Equatable<ComparesOwnDouble>);
static_assert(Equatable<HoldsUnionWords>);
static_assert(!Equatable<StatesOwnDouble> && PartialEquatable<StatesOwnDouble>);
static_assert(StatesEqualParts{0.0} != StatesEqualParts{-0.0});
static_assert(StatesEqualParts{quiet_nan_of<double>} == StatesEqualParts{quiet_nan_of<double>});

// The pins of the float rule for each class of foundation with the mark
// equal_parts: the == of the class tells +0 from -0, and the canonical NaN
// equals itself.  The walk trusts the mark on these classes because of the
// pins.  HeapArray needs an allocator, so test_core_heap_array holds its pin.
// The check files of Tagged, Budgeted and EpochVersioned hold the pins of fixy.
static_assert(Record<int, double>{1, 0.0} != Record<int, double>{1, -0.0});
static_assert(Record<int, double>{1, quiet_nan_of<double>} == Record<int, double>{1, quiet_nan_of<double>});
static_assert(Option<double>::some(0.0) != Option<double>::some(-0.0));
static_assert(Option<double>::some(quiet_nan_of<double>) == Option<double>::some(quiet_nan_of<double>));
static_assert(Result<double, PinFault>{0.0} != Result<double, PinFault>{-0.0});
static_assert(Result<double, PinFault>{quiet_nan_of<double>} == Result<double, PinFault>{quiet_nan_of<double>});
static_assert(OneOf<double, int>{0.0} != OneOf<double, int>{-0.0});
static_assert(OneOf<double, int>{quiet_nan_of<double>} == OneOf<double, int>{quiet_nan_of<double>});
static_assert(FixedArray<double, 2>{1.0, 0.0} != FixedArray<double, 2>{1.0, -0.0});
static_assert(FixedArray<double, 2>{1.0, quiet_nan_of<double>} == FixedArray<double, 2>{1.0, quiet_nan_of<double>});

// A list of one double.
[[nodiscard]] consteval FixedList<double, 2> list_of_one(double value) {
    FixedList<double, 2> list{};
    static_cast<void>(list.append(value).expect("the list has room"));
    return list;
}
static_assert(list_of_one(0.0) != list_of_one(-0.0));
static_assert(list_of_one(quiet_nan_of<double>) == list_of_one(quiet_nan_of<double>));

// The walk reads the base first, then each member in declaration order, the
// private members, the array, the local part and the pad part too.
[[nodiscard]] consteval bool differs_after(auto change) {
    Wide changed{};
    change(changed);
    return !equal(changed, Wide{}) && equal(changed, changed) && std::is_neq(order(changed, Wide{}));
}

static_assert(equal(Wide{}, Wide{}) && std::is_eq(order(Wide{}, Wide{})));
static_assert(differs_after([](Wide& wide) { wide.low = 1; }));
static_assert(differs_after([](Wide& wide) { wide.sealed = Sealed{1, Mode::off}; }));
static_assert(differs_after([](Wide& wide) { wide.sealed = Sealed{0, Mode::on}; }));
static_assert(differs_after([](Wide& wide) { wide.lanes.set(index<2>(), 1); }));
static_assert(differs_after([](Wide& wide) { wide.scale = -0.0f; }));
static_assert(differs_after([](Wide& wide) { wide.address = 1; }));
static_assert(differs_after([](Wide& wide) { wide.pad.set(index<3>(), 1); }));

// The order is lexicographic: the base decides before the members, and the
// first member decides before the later ones.
[[nodiscard]] consteval bool base_orders_first() {
    Wide low_base{};
    low_base.lanes.set(index<0>(), 9);
    Wide high_base{};
    high_base.low = 1;
    return std::is_lt(order(low_base, high_base)) && std::is_gt(order(high_base, low_base));
}
static_assert(base_orders_first());
static_assert(std::is_lt(order(Sealed{1, Mode::on}, Sealed{2, Mode::off})));

}  // namespace detail::equatable_checks

}  // namespace foundation::core
