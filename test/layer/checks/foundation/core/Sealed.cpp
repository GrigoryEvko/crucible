// The compile-time checks of foundation/core/Sealed.h.

#include <foundation/core/Sealed.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/SegmentList.h>
#include <foundation/core/SmallList.h>
#include <foundation/core/Verbs.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::sealed_checks {

// A linear object: the end of a Sealed would drop it.
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

// A class that a read writes: two threads cannot read it at the same time.
struct CountsItsReads final {
    mutable int reads = 0;
};

// A class that is Shareable only through its statement: it tells that two
// threads can read it, and a read writes its counter.
struct[[= ::foundation::transfer::shareable{}]] StatesItsReads final {
    mutable Atomic<int> reads{0};
    StatesItsReads() noexcept = default;
    StatesItsReads(StatesItsReads&&) noexcept {}
    StatesItsReads& operator=(StatesItsReads&&) = delete("the object of the check has one identity");
};

// A class that states that two threads cannot read it, with no part that shows
// it.
struct[[= ::foundation::transfer::not_shareable{}]] StatesOneReader final {
    int value = 0;
};

// A class whose move can throw.
struct ThrowingMove final {
    int value = 0;
    ThrowingMove() noexcept = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

// A Shareable object seals.  A linear object, an object whose read writes, a
// const object, an array and a reference do not.
static_assert(SealableObject<int> && SealableObject<HeapArray<int>> && SealableObject<SegmentList<int>>);
static_assert(SealableObject<SegmentList<SegmentList<int>>>);
static_assert(!SealableObject<LinearToken> && !SealableObject<SegmentList<LinearToken>>);
static_assert(!SealableObject<SmallList<int, 2>> && !SealableObject<CountsItsReads> && !SealableObject<ThrowingMove>);
static_assert(!SealableObject<SegmentList<int*>> && !SealableObject<int const> && !SealableObject<int[2]>);
static_assert(!SealableObject<int&>);

// The statement of a class decides: a class that states shareable{} seals, and
// its Sealed is Synchronized.  A class that states not_shareable{} does not
// seal, and neither does a holder of it, such as a list.
static_assert(SealableObject<StatesItsReads> && ::foundation::transfer::Synchronized<Sealed<StatesItsReads>>);
static_assert(SealableObject<SegmentList<StatesItsReads>>
              && ::foundation::transfer::Synchronized<Sealed<SegmentList<StatesItsReads>>>);
static_assert(!SealableObject<StatesOneReader> && !SealableObject<SegmentList<StatesOneReader>>);

// A Sealed holds its object and no other byte.  It takes the object by move,
// and it does not copy, move or take an assignment.
using SealedList = Sealed<SegmentList<int>>;
static_assert(sizeof(SealedList) == sizeof(SegmentList<int>) && sizeof(Sealed<int>) == sizeof(int));
static_assert(std::is_nothrow_constructible_v<SealedList, SegmentList<int>>);
static_assert(!std::is_constructible_v<SealedList, SegmentList<int>&> && !std::is_constructible_v<Sealed<int>, int&>);
static_assert(!std::is_default_constructible_v<SealedList> && !std::is_convertible_v<SegmentList<int>, SealedList>);
static_assert(!std::is_copy_constructible_v<SealedList> && !std::is_move_constructible_v<SealedList>);
static_assert(!std::is_copy_assignable_v<SealedList> && !std::is_move_assignable_v<SealedList>);
static_assert(std::same_as<decltype(Sealed{std::declval<SegmentList<int>>()}), SealedList>);

// Each read gives the const object, also of a Sealed that is not const, and a
// temporary gives none.
template <class Object>
concept Dereferences = requires(Object&& object) { *static_cast<Object&&>(object); };
template <class Object>
concept Arrows = requires(Object&& object) { static_cast<Object&&>(object).operator->(); };
static_assert(std::same_as<decltype(*std::declval<SealedList&>()), SegmentList<int> const&>);
static_assert(std::same_as<decltype(std::declval<SealedList const&>().operator->()), SegmentList<int> const*>);
static_assert(Dereferences<SealedList&> && Dereferences<SealedList const&> && Arrows<SealedList&>);
static_assert(!Dereferences<SealedList> && !Dereferences<SealedList const> && !Arrows<SealedList>);

// A list takes an append, and the object of its Sealed takes none.
template <class List>
concept Appends = requires(List& list, Alloc alloc) { list.append(alloc, 1); };
static_assert(Appends<SegmentList<int>> && !Appends<std::remove_reference_t<decltype(*std::declval<SealedList&>())>>);

// A thread beside the caller can hold a reference to a Sealed, const or not.
// It can hold no reference to the list itself, because the caller can append
// to it.
static_assert(::foundation::transfer::Synchronized<SealedList>
              && !::foundation::transfer::Synchronized<SegmentList<int>>);
static_assert(::foundation::transfer::SendableBeside<SealedList const&>
              && ::foundation::transfer::SendableBeside<SealedList&>);
static_assert(!::foundation::transfer::SendableBeside<SegmentList<int> const&>
              && ::foundation::transfer::Sendable<SegmentList<int> const&>);
static_assert(::foundation::transfer::SendableOwned<SealedList>
              && ::foundation::transfer::SendableOwned<Box<SealedList>>);

// The surface is closed over the dereference and one name.
inline constexpr TextView sealed_names{"element_type"};
CRUCIBLE_CLOSED_SURFACE(sealed_names, Sealed<int>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(sealed_names, SealedList, DereferenceProtocol);

// A Sealed in a constant evaluation.
[[nodiscard]] consteval int read_of_a_sealed_number() {
    Sealed<int> const sealed{7};
    return *sealed + *sealed.operator->();
}
static_assert(read_of_a_sealed_number() == 14);

// The build constructor takes a body that gives the object by value and does
// not throw, and the type of the result names the Sealed.  A body that gives
// another type, can throw or holds a linear value builds none.
using BuildsSeven = decltype([] noexcept { return 7; });
using BuildsWide = decltype([] noexcept { return 7L; });
using MayThrow = decltype([] { return 7; });
using HoldsToken = decltype([token = LinearToken{7}] noexcept { return token.id; });
static_assert(std::is_nothrow_constructible_v<Sealed<int>, BuildSealed, BuildsSeven>);
static_assert(!std::is_constructible_v<Sealed<int>, BuildSealed, BuildsWide>);
static_assert(!std::is_constructible_v<Sealed<int>, BuildSealed, MayThrow>);
static_assert(!::foundation::linear::HoldsNoLinearValue<HoldsToken>);
static_assert(!std::is_constructible_v<Sealed<int>, BuildSealed, HoldsToken>);
static_assert(std::same_as<decltype(Sealed{build_sealed, BuildsSeven{}}), Sealed<int>>);

// A built Sealed in a constant evaluation: the body writes each field of the
// object over some steps, and the object is const after the build.
struct BuiltPair final {
    int first = 0;
    int second = 0;
};
[[nodiscard]] consteval int sum_of_a_built_pair() {
    Sealed const sealed{build_sealed, [] noexcept {
                            BuiltPair pair{};
                            pair.first = 3;
                            pair.second = pair.first + 1;
                            return pair;
                        }};
    return sealed->first + sealed->second;
}
static_assert(sum_of_a_built_pair() == 7);

}  // namespace detail::sealed_checks

}  // namespace foundation::core
