// The compile-time checks of foundation/core/Equatable.h.

#include <foundation/core/Equatable.h>

#include <foundation/core/Array.h>

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

// A class whose <=> gives a partial order, which does not agree with ==.
struct PartialCompare {
    double value = 0.0;
    friend constexpr bool operator==(PartialCompare left, PartialCompare right) noexcept {
        return left.value <= right.value && right.value <= left.value;
    }
    friend constexpr std::partial_ordering operator<=>(PartialCompare left, PartialCompare right) noexcept {
        return left.value <=> right.value;
    }
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
static_assert(Equatable<PartialCompare> && !Orderable<PartialCompare>);
static_assert(!Equatable<ThrowingEqual>);
static_assert(!Equatable<HoldsBitField> && !Orderable<HoldsBitField>);
static_assert(Equatable<HoldsPart<int>> && Orderable<HoldsPart<int>> && Equatable<HoldsPart<Wide>>);
static_assert(std::is_lt(order(HoldsPart<int>{1}, HoldsPart<int>{2})) && equal(HoldsPart<int>{3}, HoldsPart<int>{3}));

// THE FLOAT RULE.  equal is the identity of the bits: +0 and -0 differ, and
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
static_assert(differs_after([](Wide& wide) {
    wide.lanes.with_element(index<2>(), [](int& lane) noexcept { lane = 1; });
}));
static_assert(differs_after([](Wide& wide) { wide.scale = -0.0f; }));
static_assert(differs_after([](Wide& wide) { wide.address = 1; }));
static_assert(differs_after([](Wide& wide) {
    wide.pad.with_element(index<3>(), [](unsigned char& byte) noexcept { byte = 1; });
}));

// The order is lexicographic: the base decides before the members, and the
// first member decides before the later ones.
[[nodiscard]] consteval bool base_orders_first() {
    Wide low_base{};
    low_base.lanes.with_element(index<0>(), [](int& lane) noexcept { lane = 9; });
    Wide high_base{};
    high_base.low = 1;
    return std::is_lt(order(low_base, high_base)) && std::is_gt(order(high_base, low_base));
}
static_assert(base_orders_first());
static_assert(std::is_lt(order(Sealed{1, Mode::on}, Sealed{2, Mode::off})));

}  // namespace detail::equatable_checks

}  // namespace foundation::core
