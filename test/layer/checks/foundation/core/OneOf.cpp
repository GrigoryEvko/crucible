// The compile-time checks of foundation/core/OneOf.h.

#include <foundation/core/OneOf.h>

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/core/Verbs.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::one_of_checks {

enum class Code8 : std::uint8_t {
    full,
    closed
};

enum class Code32 : std::uint32_t {
    full,
    closed
};

// A payload that owns something: its move and its destructor are not
// trivial.
struct Owner {
    int handle = 0;
    constexpr explicit Owner(int value) noexcept : handle{value} {}
    constexpr Owner(Owner&& other) noexcept : handle{other.handle} { other.handle = 0; }
    Owner(Owner const&) = delete;
    Owner& operator=(Owner const&) = delete;
    Owner& operator=(Owner&&) = delete;
    constexpr ~Owner() {}
};

// A linear payload.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Token {
    int handle = 0;
    constexpr explicit Token(int value) noexcept : handle{value} {}
    Token(Token&&) noexcept = default;
    Token(Token const&) = delete;
    constexpr Token& operator=(Token&& other) noexcept {
        handle = other.handle;
        return *this;
    }
};

// The gate: 2 to 255 payloads, with no type two times.
static_assert(OneOfAlternatives<int, long>);
static_assert(OneOfAlternatives<std::uint32_t, Code32, std::uint16_t>);
static_assert(!OneOfAlternatives<int>);
static_assert(!OneOfAlternatives<>);
static_assert(!OneOfAlternatives<int, int>);
static_assert(!OneOfAlternatives<int, long, int>);
static_assert(!OneOfAlternatives<int&, long>);
static_assert(!OneOfAlternatives<int const, long>);
static_assert(!OneOfAlternatives<NoValue, int>);
static_assert(!OneOfAlternatives<Err<Code8>, int>);

// The layout: the storage of the widest alternative and a tag.  A sum of
// plain alternatives is trivially copyable, so a small one goes back in
// registers.
static_assert(sizeof(OneOf<std::uint8_t, std::uint16_t>) == 4);
static_assert(sizeof(OneOf<std::uint32_t, Code32>) == 8);
static_assert(sizeof(OneOf<std::uint32_t, Code32, std::uint16_t>) == 8);
static_assert(sizeof(OneOf<std::uint64_t, double>) == 16);
static_assert(std::is_trivially_copyable_v<OneOf<std::uint32_t, Code32>>);
static_assert(std::is_trivially_copyable_v<OneOf<std::uint64_t, double>>);

// An alternative that owns something gives a move-only OneOf, and a linear
// alternative takes the owning form.
static_assert(!std::is_trivially_copyable_v<OneOf<Owner, int>>);
static_assert(std::is_nothrow_move_constructible_v<OneOf<Owner, int>>);
static_assert(!std::is_copy_constructible_v<OneOf<Owner, int>>);
static_assert(!std::is_copy_constructible_v<OneOf<Token, int>>);

// A linear alternative makes the OneOf linear, so the trait and the rules of
// the family linear read it as linear.  The union of the storage cannot move,
// so the OneOf carries the linear marker.
static_assert(::foundation::linear::is_linear_v<OneOf<Token, int>>);
static_assert(::foundation::linear::is_linear_v<OneOf<int, Code8, Token>>);
static_assert(!::foundation::linear::is_linear_v<OneOf<Owner, int>>);
static_assert(!::foundation::linear::is_linear_v<OneOf<std::uint32_t, Code32>>);

// Zero bytes are an alternative that no constructor made, so the checked
// lifetime start refuses each OneOf.
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<OneOf<std::uint32_t, Code32>>);

// No default constructor, no empty state, no dereference and no conversion
// to bool.
template <class O>
concept Dereferences = requires(O value) { *value; };
static_assert(!std::is_default_constructible_v<OneOf<int, long>>);
static_assert(!std::is_constructible_v<OneOf<int, long>, NoValue>);
static_assert(!Dereferences<OneOf<int, long>>);
static_assert(!std::is_convertible_v<OneOf<int, long>, bool>);
static_assert(!std::is_constructible_v<bool, OneOf<int, long>>);

// A value converts to the alternative of its own type, and to no other.
static_assert(std::is_convertible_v<int, OneOf<int, long>>);
static_assert(std::is_convertible_v<long, OneOf<int, long>>);
static_assert(!std::is_constructible_v<OneOf<int, long>, unsigned>);
static_assert(!std::is_constructible_v<OneOf<int, long>, short>);
static_assert(OneOf<int, long>{7}.is<int>() && !OneOf<int, long>{7}.is<long>());
static_assert(OneOf<int, long>{7L}.is<long>());

// which gives the index of the active alternative as an Index of the count of
// the alternatives.
static_assert(std::is_same_v<decltype(OneOf<int, Code8, long>{7L}.which()), Index<3>>);
static_assert(OneOf<int, Code8, long>{7L}.which().raw() == 2 && OneOf<int, Code8>{5}.which().raw() == 0);
static_assert(OneOf<Owner, int>{Owner{3}}.which().raw() == 0);

// is and expect name an alternative.
template <class O, class T>
concept AsksFor = requires(O const& value) { value.template is<T>(); };
static_assert(AsksFor<OneOf<int, long>, int> && !AsksFor<OneOf<int, long>, short>);

// The roads in a constant evaluation.
static_assert(OneOf<int, Code8>{5}.expect<int>("a constant five") == 5);
static_assert(OneOf<int, Code8>{Code8::closed}.expect<Code8>("a constant code") == Code8::closed);
static_assert(OneOf<int, Code8, long>{9L}.match([](int) noexcept { return 1; }, [](Code8) noexcept { return 2; },
                                                [](long value) noexcept { return static_cast<int>(value); })
              == 9);

[[nodiscard]] consteval int borrowed_match() noexcept {
    OneOf<int, Code8> const held{Code8::full};
    int const seen = held.match([](int const& value) noexcept { return value; },
                                [](Code8 const& code) noexcept { return code == Code8::full ? 10 : 11; });
    return held.is<Code8>() ? seen : 0;
}
static_assert(borrowed_match() == 10);

// The rvalue match of an owning OneOf gives the owner to its arm, and the
// source keeps the moved-from owner.
[[nodiscard]] consteval int moved_owner() noexcept {
    OneOf<Owner, int> source{Owner{4}};
    int const seen = static_cast<OneOf<Owner, int>&&>(source).match([](Owner owner) noexcept { return owner.handle; },
                                                                    [](int) noexcept { return 0; });
    return source.is<Owner>() ? seen : -1;
}
static_assert(moved_owner() == 4);

// The fatal unwrap of a different alternative is not a constant expression.
template <int Value>
constexpr bool expects_int =
    requires { typename std::integral_constant<int, OneOf<int, long>{Value}.expect<int>("int")>; };
static_assert(expects_int<3>);
template <long Value>
constexpr bool expects_int_of_long =
    requires { typename std::integral_constant<int, OneOf<int, long>{Value}.expect<int>("int")>; };
static_assert(!expects_int_of_long<3L>);

// A match needs one arm for each alternative, with one result type, and an
// rvalue match gives no reference.
template <class O>
concept MatchesWithTwoArms = requires(O value) {
    static_cast<O&&>(value).match([](int) noexcept { return 0; }, [](Code8) noexcept { return 0; });
};
template <class O>
concept MatchesWithTwoTypes = requires(O value) {
    static_cast<O&&>(value).match([](int) noexcept { return 0; }, [](Code8) noexcept { return 0L; },
                                  [](long) noexcept { return 0; });
};
inline constexpr int fallback_number = 0;
using NumberRef = int const&;
template <class O>
concept MovedMatchGivesReference = requires(O value) {
    static_cast<O&&>(value).match([](int) noexcept -> NumberRef { return fallback_number; },
                                  [](Code8) noexcept -> NumberRef { return fallback_number; },
                                  [](long) noexcept -> NumberRef { return fallback_number; });
};
template <class O>
concept BorrowedMatchGivesReference = requires(O const& value) {
    value.match([](int const&) noexcept -> NumberRef { return fallback_number; },
                [](Code8 const&) noexcept -> NumberRef { return fallback_number; },
                [](long const&) noexcept -> NumberRef { return fallback_number; });
};
static_assert(!MatchesWithTwoArms<OneOf<int, Code8, long>>);
static_assert(!MatchesWithTwoTypes<OneOf<int, Code8, long>>);
static_assert(!MovedMatchGivesReference<OneOf<int, Code8, long>>);
template <class O>
concept MovedMatchTakesReference = requires(O value) {
    static_cast<O&&>(value).match([](int) noexcept { return 0; }, [](Code8&&) noexcept { return 0; },
                                  [](auto const&) noexcept { return 0; });
};
static_assert(!MovedMatchTakesReference<OneOf<int, Code8, long>>);
static_assert(BorrowedMatchGivesReference<OneOf<int, Code8, long>>);

// Each arm of a borrow match takes the alternative at its position, as a
// value or a const reference.  Two classes that convert into each other
// cannot take arms in the wrong order, and a generic arm still passes.
struct Celsius;
struct Kelvin {
    double value = 0.0;
    constexpr Kelvin() noexcept = default;
    constexpr Kelvin(Celsius celsius) noexcept;
};
struct Celsius {
    double value = 0.0;
    constexpr Celsius() noexcept = default;
    constexpr Celsius(Kelvin kelvin) noexcept : value{kelvin.value - 273.15} {}
};
constexpr Kelvin::Kelvin(Celsius celsius) noexcept : value{celsius.value + 273.15} {}
template <class O>
concept BorrowedMatchSwapsArms = requires(O const& value) {
    value.match([](Kelvin kelvin) noexcept { return kelvin.value; },
                [](Celsius celsius) noexcept { return celsius.value; });
};
template <class O>
concept BorrowedMatchReadsExactly = requires(O const& value) {
    value.match([](Celsius const & celsius) noexcept { return celsius.value; },
                [](Kelvin kelvin) noexcept { return kelvin.value; });
    value.match([](auto const & reading) noexcept { return reading.value; },
                [](auto&& reading) noexcept { return reading.value; });
};
static_assert(!BorrowedMatchSwapsArms<OneOf<Celsius, Kelvin>> && BorrowedMatchReadsExactly<OneOf<Celsius, Kelvin>>);

// get<T> lends the active alternative as a Ref of T const, from a OneOf that
// is const or not, and no Ref from a temporary.  So no write and no move
// through the Ref changes the alternative that the OneOf keeps.  get names
// an alternative, and the pack refuses an explicit brand.
template <class O, class T, class Read>
concept LendsAs = requires(O& value) {
    { *value.template get<T>().expect("the alternative is active") } -> std::same_as<Read>;
};
template <class O, class T>
concept LendsFromTemporary = requires(O value) { static_cast<O&&>(value).template get<T>(); };
template <class O, class T>
concept LendsWithBrand = requires(O& value) { value.template get<T, ::foundation::brand::DefaultBrand>(); };
static_assert(LendsAs<OneOf<int, Code8>, int, int const&> && LendsAs<OneOf<int, Code8> const, int, int const&>);
static_assert(LendsAs<OneOf<Owner, int>, Owner, Owner const&>);
static_assert(LendsAs<OneOf<Token, Code8>, Token, Token const&>);
static_assert(!LendsFromTemporary<OneOf<int, Code8>, int> && !LendsFromTemporary<OneOf<int, Code8> const, int>);
static_assert(!LendsWithBrand<OneOf<int, Code8>, int>);
template <class O, class T>
concept Lends = requires(O& value) { value.template get<T>(); };
static_assert(Lends<OneOf<int, Code8>, Code8> && !Lends<OneOf<int, Code8>, long>);

// The Option of a Ref has the size of a pointer.  The frame door writes the
// active alternative in place, and the OneOf keeps it.
[[nodiscard]] consteval int lent_and_written() noexcept {
    OneOf<int, Code8> held{5};
    auto missed = held.get<Code8>();
    static_assert(sizeof(missed) == sizeof(int*));
    if (missed.is_some()) return -1;
    held.with_mut([](auto value) noexcept { *value = 8; }, [](auto) noexcept {});
    return static_cast<OneOf<int, Code8>&&>(held).expect<int>("the int is active");
}
static_assert(lent_and_written() == 8);

// The frame door takes an arm for each alternative, each with the RefMut of
// its alternative, on a OneOf that is not const.  A temporary, a const OneOf
// and a linear alternative have no frame door.
template <class O>
concept WritesInPlace = requires(O& value) { value.with_mut([](auto) noexcept {}, [](auto) noexcept {}); };
template <class O>
concept WritesTemporary =
    requires(O value) { static_cast<O&&>(value).with_mut([](auto) noexcept {}, [](auto) noexcept {}); };
static_assert(WritesInPlace<OneOf<int, Code8>> && WritesInPlace<OneOf<Owner, int>>);
static_assert(!WritesInPlace<OneOf<int, Code8> const> && !WritesInPlace<OneOf<Token, Code8>>);
static_assert(!WritesTemporary<OneOf<int, Code8>>);

// A OneOf holds a borrow when one of its alternatives holds one, in the plain
// slot and in the owning slot.
static_assert(HoldsNoBorrow<OneOf<int, Code8, long>> && HoldsNoBorrow<Option<OneOf<int, Code8>>>);
static_assert(!HoldsNoBorrow<OneOf<int, TextView>> && !HoldsNoBorrow<Result<OneOf<int*, Code8>, Code8>>);
static_assert(HoldsNoBorrow<OneOf<Owner, Code8>> && !HoldsNoBorrow<OneOf<Owner, int*>>);

// A OneOf of plain alternatives keeps its trivial assignment.  An owning
// OneOf has a move assignment, which drops the active alternative, so a
// linear alternative refuses it.
static_assert(std::is_trivially_copy_assignable_v<OneOf<std::uint32_t, Code32>>);
static_assert(std::is_nothrow_move_assignable_v<OneOf<Owner, int>>);
static_assert(!std::is_copy_assignable_v<OneOf<Owner, int>>);
static_assert(!std::is_move_assignable_v<OneOf<Token, int>>);

[[nodiscard]] consteval int assigned_one_of() noexcept {
    OneOf<Owner, int> target{Owner{7}};
    OneOf<Owner, int> source{8};
    target = static_cast<OneOf<Owner, int>&&>(source);
    if (!target.is<int>()) return -1;
    target = OneOf<Owner, int>{Owner{9}};
    return target.match([](Owner const& owner) noexcept { return owner.handle; },
                        [](int const&) noexcept { return 0; });
}
static_assert(assigned_one_of() == 9);

// Two OneOfs of one type are equal when the same alternative is active,
// with equal values.  A OneOf compares only with a OneOf of its own
// alternatives, and an alternative with no compare gives no compare.
static_assert(OneOf<int, Code8>{5} == OneOf<int, Code8>{5});
static_assert(OneOf<int, Code8>{5} != OneOf<int, Code8>{6});
static_assert(OneOf<int, Code8>{5} != OneOf<int, Code8>{Code8::full});
static_assert(OneOf<int, Code8, long>{4L} == OneOf<int, Code8, long>{4L});
template <class Left, class Right>
concept Compares = requires(Left const& left, Right const& right) { left == right; };
static_assert(Compares<OneOf<int, Code8>, OneOf<int, Code8>>);
static_assert(!Compares<OneOf<int, Code8>, int> && !Compares<int, OneOf<int, Code8>>);
static_assert(!Compares<OneOf<int, Code8>, OneOf<int, Code32>>);
// A double alternative compares by its bits, and a long double has no
// compare.
static_assert(!Compares<OneOf<Owner, int>, OneOf<Owner, int>> && Compares<OneOf<double, int>, OneOf<double, int>>);
static_assert(!Compares<OneOf<long double, int>, OneOf<long double, int>>);
static_assert(OneOf<double, int>{-0.0} != OneOf<double, int>{0.0});

// The closed surface: a OneOf gives only the shared verbs, the language
// protocols, and the names of the family list of plan section 11.2 that
// its API table holds.  The check file of Choice.h holds the full list, and
// each type of Choice.h is checked against it there.
inline constexpr TextView one_of_names{"is which with_mut"};
CRUCIBLE_CLOSED_SURFACE(one_of_names, OneOf<int, Code8>);
CRUCIBLE_CLOSED_SURFACE(one_of_names, OneOf<Owner, int>);
CRUCIBLE_CLOSED_SURFACE(one_of_names, OneOf<Token, Code8>);

}  // namespace foundation::core::detail::one_of_checks
