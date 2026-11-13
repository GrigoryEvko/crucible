// The compile-time checks of foundation/core/Ref.h.

#include <foundation/core/Ref.h>

#include <foundation/core/Array.h>
#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::ref_checks {

struct NoMarker {};

struct Abstract {
    virtual ~Abstract() = default;
    virtual void act() = 0;
};

struct OverAligned {
    alignas(128) int value = 0;
};

// One pointer, and nothing more.
static_assert(sizeof(Box<int>) == sizeof(void*));
static_assert(sizeof(Box<OverAligned>) == sizeof(void*));

// Move-only, and the move does not throw.
static_assert(!std::is_copy_constructible_v<Box<int>>);
static_assert(!std::is_copy_assignable_v<Box<int>>);
static_assert(std::is_nothrow_move_constructible_v<Box<int>>);
static_assert(std::is_nothrow_move_assignable_v<Box<int>>);
static_assert(!std::is_trivially_copyable_v<Box<int>>);
static_assert(!std::is_default_constructible_v<Box<int>>);

// No pointer converts to a Box.
static_assert(!std::is_constructible_v<Box<int>, int*>);
static_assert(!std::is_convertible_v<int*, Box<int>>);

// The constructor of a Box takes the key, and only BoxDoor builds a key.
// The key is not trivially copyable, so std::bit_cast builds no key either.
static_assert(!std::is_constructible_v<Box<int>, int>);
static_assert(!std::is_default_constructible_v<detail::BoxKey>);
static_assert(!std::is_copy_constructible_v<detail::BoxKey>);
static_assert(!std::is_copy_assignable_v<detail::BoxKey>);
static_assert(!std::is_trivially_copyable_v<detail::BoxKey>);

// The payload gate.
static_assert(BoxPayload<int>);
static_assert(BoxPayload<OverAligned>);
static_assert(!BoxPayload<int[4]>);
static_assert(!BoxPayload<int const>);
static_assert(!BoxPayload<int&>);
static_assert(!BoxPayload<void>);
static_assert(!BoxPayload<Abstract>);

// The gate of the mint.
static_assert(CanMintBox<int, Alloc>);
static_assert(CanMintBox<int, Alloc, int>);
static_assert(!CanMintBox<int, NoMarker, int>);
static_assert(!CanMintBox<int, Alloc, char const*>);
static_assert(!CanMintBox<int[2], Alloc>);

// ── Ref ────────────────────────────────────────────────────────────────

// Two brands of the checks.  A brand is an empty class (foundation/Brand.h).
struct CheckBrand {};
struct OtherBrand {};

// One pointer, and an Option of a Ref has no flag.
static_assert(sizeof(Ref<int, CheckBrand>) == sizeof(void*));
static_assert(sizeof(Ref<OverAligned const, CheckBrand>) == sizeof(void*));
static_assert(sizeof(Option<Ref<int, CheckBrand>>) == sizeof(void*));
static_assert(sizeof(Option<Ref<Abstract, CheckBrand>>) == sizeof(void*));

// A Ref copies as a pointer copies, and the seal makes it not trivially
// copyable, so std::bit_cast builds no Ref from the bytes of an address.
static_assert(std::is_trivially_copy_constructible_v<Ref<int, CheckBrand>>);
static_assert(std::is_trivially_move_constructible_v<Ref<int, CheckBrand>>);
static_assert(std::is_trivially_destructible_v<Ref<int, CheckBrand>>);
static_assert(std::is_copy_assignable_v<Ref<int, CheckBrand>>);
static_assert(!std::is_trivially_copyable_v<Ref<int, CheckBrand>>);

// No empty Ref, and no Ref from a pointer.
static_assert(!std::is_default_constructible_v<Ref<int, CheckBrand>>);
static_assert(!std::is_constructible_v<Ref<int, CheckBrand>, int*>);
static_assert(!std::is_constructible_v<Ref<int, CheckBrand>, std::nullptr_t>);

// A Ref of an object takes the key, and only RefDoor builds a key.  The key
// is not trivially copyable, so std::bit_cast builds no key either.
static_assert(!std::is_constructible_v<Ref<int, CheckBrand>, int&>);
static_assert(!std::is_default_constructible_v<detail::RefKey>);
static_assert(!std::is_copy_constructible_v<detail::RefKey>);
static_assert(!std::is_copy_assignable_v<detail::RefKey>);
static_assert(!std::is_trivially_copyable_v<detail::RefKey>);

// A const target comes from a mutable one with the same brand, and never
// the reverse.  No brand changes into a different brand.
static_assert(std::is_convertible_v<Ref<int, CheckBrand>, Ref<int const, CheckBrand>>);
static_assert(!std::is_convertible_v<Ref<int const, CheckBrand>, Ref<int, CheckBrand>>);
static_assert(!std::is_constructible_v<Ref<int, CheckBrand>, Ref<int const, CheckBrand>>);
static_assert(!std::is_constructible_v<Ref<long, CheckBrand>, Ref<int, CheckBrand>>);
static_assert(!std::is_constructible_v<Ref<int, OtherBrand>, Ref<int, CheckBrand>>);
static_assert(!std::is_constructible_v<Ref<int const, OtherBrand>, Ref<int, CheckBrand>>);

// The one conversion to the erased brand, with or without a const, and no
// conversion back.  Each of the two aliases spells the erased form on
// purpose, and the ledger of the rule erased-brand admits the two.
using ErasedRef = Ref<int, ::foundation::brand::DefaultBrand>;
using ErasedConstRef = Ref<int const, ::foundation::brand::DefaultBrand>;
static_assert(std::is_convertible_v<Ref<int, CheckBrand>, ErasedRef>);
static_assert(std::is_convertible_v<Ref<int, CheckBrand>, ErasedConstRef>);
static_assert(!std::is_constructible_v<Ref<int, CheckBrand>, ErasedRef>);

// Two calls of get of an array give two fresh brands.  The two Refs reach
// one element, so same_object accepts them, and each one converts to the
// erased brand.
consteval bool two_gets_reach_one_element() {
    FixedArray<int, 3> numbers{1, 2, 3};
    auto const first = numbers.get(1).expect("the position is below the count");
    auto const second = numbers.get(1).expect("the position is below the count");
    ErasedRef const erased_first = first;
    ErasedConstRef const erased_second = second;
    return !std::same_as<decltype(first), decltype(second)> && same_object(first, second)
        && same_object(erased_first, erased_second);
}
static_assert(two_gets_reach_one_element());

// A brand is an empty class.
template <class T, class Brand>
concept Spells = requires { sizeof(Ref<T, Brand>); };
static_assert(!Spells<int, int>);
static_assert(!Spells<int, Address>);

// to() takes an lvalue only, and refuses each explicit template argument,
// so no caller names the brand or the target.
template <class T>
concept ReachesTemporary = requires { ::foundation::core::to(T{}); };
template <class T>
concept ReachesLvalue = requires(T& object) { ::foundation::core::to(object); };
template <class T>
concept NamesTarget = requires(T& object) { ::foundation::core::to<T>(object); };
template <class T, class Brand>
concept NamesBrand = requires(T& object) { ::foundation::core::to<T, Brand>(object); };
static_assert(!ReachesTemporary<int>);
static_assert(!ReachesTemporary<int const>);
static_assert(ReachesLvalue<int>);
static_assert(ReachesLvalue<int const>);
static_assert(!NamesTarget<int>);
static_assert(!NamesBrand<int, CheckBrand>);

// Each call of to() gives a brand of its own, and the target keeps the
// const of the object.  target_of gives a tag of the target, because a
// function that gives a const int by value gives a plain int.
template <class T>
struct Target {};
template <class T, class Brand>
Brand brand_of(Ref<T, Brand>);
template <class T, class Brand>
Target<T> target_of(Ref<T, Brand>);
// Two objects for the unevaluated operands below, with no definition.
extern int reached_number;
extern int const reached_constant;
static_assert(!std::is_same_v<decltype(brand_of(to(reached_number))), decltype(brand_of(to(reached_number)))>);
static_assert(::foundation::brand::IsFreshBrand<decltype(brand_of(to(reached_number)))>);
static_assert(std::is_same_v<decltype(target_of(to(reached_number))), Target<int>>);
static_assert(std::is_same_v<decltype(target_of(to(reached_constant))), Target<int const>>);

// The target gate.
static_assert(RefTarget<int>);
static_assert(RefTarget<int const>);
static_assert(RefTarget<Abstract>);
static_assert(!RefTarget<int[2]>);
static_assert(!RefTarget<int volatile>);
static_assert(!RefTarget<int&>);
static_assert(!RefTarget<void>);

// A Ref has no compare: same_object() tells if two Refs reach one object.
static_assert(!std::equality_comparable<Ref<int, CheckBrand>>);
static_assert(!std::totally_ordered<Ref<int, CheckBrand>>);

// A Ref borrows with the dereference only, and has no get().
template <class R>
concept HasGet = requires(R ref) { ref.get(); };
static_assert(!HasGet<Ref<int, CheckBrand>>);

// The dereference and same_object() fold in a constant evaluation.
consteval bool reaches_in_constant_evaluation() {
    int first = 1;
    int second = 2;
    int const& first_read = first;
    auto const ref = to(first);
    *ref = 3;
    return first == 3 && same_object(ref, to(first_read)) && !same_object(ref, to(second));
}
static_assert(reaches_in_constant_evaluation());

// ── Address ────────────────────────────────────────────────────────────

// One word, and no address from an integer, from a pointer or with no object.
static_assert(sizeof(Address) == sizeof(std::uintptr_t));
static_assert(!std::is_default_constructible_v<Address>);
static_assert(!std::is_constructible_v<Address, std::uintptr_t>);
static_assert(!std::is_constructible_v<Address, int*>);
static_assert(!std::is_trivially_copyable_v<Address>);

// The constructor of an Address takes the key, and only AddressDoor builds
// a key.
static_assert(!std::is_default_constructible_v<detail::AddressKey>);
static_assert(!std::is_copy_constructible_v<detail::AddressKey>);
static_assert(!std::is_copy_assignable_v<detail::AddressKey>);
static_assert(!std::is_trivially_copyable_v<detail::AddressKey>);

// An address compares for equality and has no order.
static_assert(std::equality_comparable<Address>);
static_assert(!std::totally_ordered<Address>);

// The alignment query takes a power of two only.
template <class A, std::size_t Alignment>
concept AsksAlignment = requires(A const& address) { address.template is_aligned<Alignment>(); };
static_assert(AsksAlignment<Address, 1>);
static_assert(AsksAlignment<Address, 4096>);
static_assert(!AsksAlignment<Address, 0>);
static_assert(!AsksAlignment<Address, 24>);

// ── Box ────────────────────────────────────────────────────────────────

// A borrow of a temporary Box is refused.
template <class B>
concept BorrowsFromTemporary = requires(B&& box) { static_cast<B&&>(box).view(); };
template <class B>
concept BorrowsFromLvalue = requires(B& box) { box.view(); };
static_assert(!BorrowsFromTemporary<Box<int>>);
static_assert(BorrowsFromLvalue<Box<int>>);

// view() gives a Ref of the object, and a Ref of a const object from a
// const Box.  Each call gives a fresh brand, and no caller names it.  The
// Box borrows through view() and the dereference only.
static_assert(std::is_same_v<decltype(target_of(std::declval<Box<int>&>().view())), Target<int>>);
static_assert(std::is_same_v<decltype(target_of(std::declval<Box<int> const&>().view())), Target<int const>>);
static_assert(!std::is_same_v<decltype(std::declval<Box<int>&>().view()), decltype(std::declval<Box<int>&>().view())>);
static_assert(::foundation::brand::IsFreshBrand<decltype(brand_of(std::declval<Box<int>&>().view()))>);
template <class B>
concept ViewNamesBrand = requires(B& box) { box.template view<CheckBrand>(); };
static_assert(!ViewNamesBrand<Box<int>>);
static_assert(!HasGet<Box<int>>);

// ── The closed surface ─────────────────────────────────────────────────

// The family list of the Ref family (plan section 11.5).  Each public member
// of a type of the family is a shared verb, a language protocol or a name of
// this list.  Ref and Box have no empty state, so they take the dereference.
inline constexpr TextView ref_names{"Ref to same_object downcast KindFamily KindTagged Box mint_box BumpArena make "
                                    "array copy_text Slot emplace clear FnRef CallRef Address address_of is_aligned "
                                    "Hole"};
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<int, CheckBrand>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<int const, CheckBrand>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<Abstract, CheckBrand>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Box<int>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Box<OverAligned>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Address);

}  // namespace detail::ref_checks

}  // namespace foundation::core
