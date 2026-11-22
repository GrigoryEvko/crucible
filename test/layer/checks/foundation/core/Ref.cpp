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

// A Box of a linear payload is linear, and it has no move assignment, so the
// rule must-consume refuses its drop.  A Box of a plain payload is not
// linear.  A Hole is linear.
static_assert(::foundation::linear::is_linear_v<Box<Hole<8, 8>>> && !std::is_move_assignable_v<Box<Hole<8, 8>>>);
static_assert(std::is_nothrow_move_constructible_v<Box<Hole<8, 8>>>);
static_assert(!::foundation::linear::is_linear_v<Box<int>> && !::foundation::linear::is_linear_v<Box<OverAligned>>);
static_assert(!DroppableBoxPayload<Hole<8, 8>> && DroppableBoxPayload<int>);

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

// The word of a Box is one pointer that a ring cell copies: trivially
// copyable and trivially destructible.  Only a member of the authority
// builds its key, and no byte copy builds one.  A word of one authority is
// no word of another, and only a Box builds a word that holds an object.
struct WordAuthority {};
struct OtherAuthority {};
static_assert(sizeof(BoxWord<int, WordAuthority>) == sizeof(void*));
static_assert(std::is_trivially_copyable_v<BoxWord<int, WordAuthority>>);
static_assert(std::is_trivially_destructible_v<BoxWord<int, WordAuthority>>);
static_assert(std::is_nothrow_default_constructible_v<BoxWord<int, WordAuthority>>);
static_assert(!std::is_constructible_v<BoxWord<int, WordAuthority>, int*>);
static_assert(!std::is_same_v<BoxWord<int, WordAuthority>, BoxWord<int, OtherAuthority>>);
static_assert(!std::is_default_constructible_v<box_word_key<WordAuthority>>);
static_assert(!std::is_copy_constructible_v<box_word_key<WordAuthority>>);
static_assert(!std::is_copy_assignable_v<box_word_key<WordAuthority>>);
static_assert(!std::is_trivially_copyable_v<box_word_key<WordAuthority>>);
static_assert(!std::is_constructible_v<Box<int>, BoxWord<int, WordAuthority>>);
static_assert(!std::is_constructible_v<Box<int>, BoxWord<int, WordAuthority>, box_word_key<OtherAuthority> const&>);

// The payload gate.
static_assert(BoxPayload<int>);
static_assert(BoxPayload<OverAligned>);
static_assert(!BoxPayload<int[4]>);
static_assert(!BoxPayload<int const>);
static_assert(!BoxPayload<int&>);
static_assert(!BoxPayload<void>);

// A Box of a type that is only declared is a type of one pointer.  The
// mint and the end of the object need the complete type, so no unit makes
// or ends the object of such a Box.  An abstract class is a Box type that no
// mint makes.
struct OnlyDeclared;
static_assert(BoxPayload<OnlyDeclared>);
static_assert(sizeof(Box<OnlyDeclared>) == sizeof(void*));
static_assert(!CompleteBoxPayload<OnlyDeclared>);
static_assert(BoxPayload<Abstract>);
static_assert(!CompleteBoxPayload<Abstract>);
static_assert(CompleteBoxPayload<int>);
static_assert(CompleteBoxPayload<OverAligned>);

// The gate of the mint.
static_assert(CanMintBox<int, Alloc>);
static_assert(CanMintBox<int, Alloc, int>);
static_assert(!CanMintBox<int, NoMarker, int>);
static_assert(!CanMintBox<int, Alloc, char const*>);
static_assert(!CanMintBox<int[2], Alloc>);
static_assert(!CanMintBox<OnlyDeclared, Alloc>);
static_assert(!CanMintBox<Abstract, Alloc>);

// ── Ref ────────────────────────────────────────────────────────────────

// Two brands of the checks.  A brand is an empty class (foundation/Brand.h).
struct CheckBrand {};
struct OtherBrand {};

// One pointer, and an Option of a Ref has no flag.
static_assert(sizeof(Ref<int, CheckBrand>) == sizeof(void*));
static_assert(sizeof(Ref<OverAligned const, CheckBrand>) == sizeof(void*));
static_assert(sizeof(Option<Ref<int, CheckBrand>>) == sizeof(void*));
static_assert(sizeof(Option<Ref<Abstract, CheckBrand>>) == sizeof(void*));

// The niche of a Ref states that no door builds the null pointer, and the
// statement passes the three checks of the statement of
// foundation/core/Choice.h: the constructor from a reference takes the key,
// and the widening is a constructor template.  The members of Ref read the
// object and write no Ref, and the statement stands in this header.
static_assert(detail::BuildsNoEmptyValue<Ref<int, CheckBrand>>
              && detail::BuildsNoEmptyValue<Ref<int const, CheckBrand>>);
static_assert(detail::BuildsNoEmptyValue<Ref<Abstract, CheckBrand>>);
static_assert(detail::WritesNoEmptyValue<Ref<int, CheckBrand>> && detail::StatedInBase<Ref<int, CheckBrand>>);
static_assert(niche<Ref<int, CheckBrand>>::is_empty_never_built.is_in_base());
static_assert(detail::CheckedStatement<Ref<Abstract, CheckBrand>>
              && detail::CheckedStatement<Ref<OverAligned const, CheckBrand>>);

// The statement gives an Option of a Ref the frame door that writes the Ref
// in place.
template <class O>
concept WritesRefInPlace = requires(O& option) { option.with_mut([](auto) noexcept {}, [] noexcept {}); };
static_assert(WritesRefInPlace<Option<Ref<int, CheckBrand>>>);

// The word protocol of a Ref states that only the null Ref of the niche gives
// the empty word (the word of a stated payload of foundation/core/Atomic.h),
// so the store of a full Option of a Ref tests no word.
static_assert(detail::word_statement_of(^^Ref<int, CheckBrand>) == detail::WordStatement::holds);
static_assert(AtomicValue<Option<Ref<int, CheckBrand>>>);

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

// No conversion reaches the erased brand, with or without a const, and no
// conversion comes back from it.  erase_brand is the one door.  Each of the
// two aliases spells the erased form on purpose, and the ledger of the rule
// erased-brand admits the two.
using ErasedRef = Ref<int, ::foundation::brand::DefaultBrand>;
using ErasedConstRef = Ref<int const, ::foundation::brand::DefaultBrand>;
static_assert(!std::is_convertible_v<Ref<int, CheckBrand>, ErasedRef>);
static_assert(!std::is_constructible_v<ErasedRef, Ref<int, CheckBrand>>);
static_assert(!std::is_convertible_v<Ref<int, CheckBrand>, ErasedConstRef>);
static_assert(!std::is_constructible_v<ErasedConstRef, Ref<int const, CheckBrand>>);
static_assert(!std::is_constructible_v<Ref<int, CheckBrand>, ErasedRef>);
static_assert(std::is_convertible_v<ErasedRef, ErasedConstRef>);

// Two calls of get of an array give two fresh brands.  The two Refs reach
// one element, so same_object accepts them, and erase_brand gives each one
// on the erased brand.
consteval bool two_gets_reach_one_element() {
    FixedArray<int, 3> numbers{1, 2, 3};
    auto const first = numbers.get(1).expect("the position is below the count");
    auto const second = numbers.get(1).expect("the position is below the count");
    ErasedConstRef const erased_first = erase_brand(first);
    ErasedConstRef const erased_second = erase_brand(second);
    return !std::same_as<decltype(first), decltype(second)> && same_object(first, second)
        && same_object(erased_first, erased_second);
}
static_assert(two_gets_reach_one_element());

// erase_brand drops a brand of one instance, and no scope brand: the rule
// brand-escape reads a scope brand, and an erased Ref names none.  An erased
// Ref has no brand to drop.  The fixture neg_core_ref_erase_scope_brand shows
// the refusal of a scope brand at a call.  The Ref of the door reaches the
// same object, on the erased brand.
template <class Inner, std::size_t Position>
struct WrapsBrand final {};
template <class... Refused, class Scope = CRUCIBLE_SCOPE_BRAND>
consteval bool scope_brand_erases() {
    return ErasableBrand<Scope> || ErasableBrand<WrapsBrand<Scope, 0>>
        || ErasableBrand<WrapsBrand<WrapsBrand<Scope, 1>, 0>>;
}
static_assert(ErasableBrand<WrapsBrand<CheckBrand, 0>>);
static_assert(ErasableBrand<CheckBrand> && !ErasableBrand<::foundation::brand::DefaultBrand>);
static_assert(!scope_brand_erases());
consteval bool erases_to_the_same_object() {
    int number = 4;
    int const& read = number;
    auto const branded = to(number);
    auto const erased = erase_brand(branded);
    auto const erased_read = erase_brand(to(read));
    *erased = 5;
    return std::same_as<decltype(erased), ErasedRef const> && std::same_as<decltype(erased_read), ErasedConstRef const>
        && number == 5 && same_object(branded, erased) && same_object(erased, erased_read);
}
static_assert(erases_to_the_same_object());

// same_target compares two Options of a Ref: two empty Options, or two Refs
// of one object, with or without a const and with any two brands.
consteval bool compares_the_targets_of_two_options() {
    int first = 1;
    int second = 2;
    int const& first_read = first;
    Option<Ref<int, CheckBrand>> const empty{};
    Option<ErasedConstRef> const other_empty{};
    auto const full = some(to(first));
    auto const same = some(erase_brand(to(first_read)));
    auto const different = some(to(second));
    return same_target(empty, other_empty) && same_target(full, same) && !same_target(full, different)
        && !same_target(full, empty) && !same_target(other_empty, same);
}
static_assert(compares_the_targets_of_two_options());

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

// as_ref gives the payload of a named niche or plain Option as an Option of
// a Ref of a const payload with a fresh brand, one brand for each call.  The
// Option of a Ref has the size of one pointer.  A temporary Option takes the
// deleted overload, because the Ref would dangle.  An owning Option, such as
// an Option of a linear handle, takes a deleted overload too, because the Ref
// would outlive each lend that the Option counts.  The annotation
// no_start_over_bytes keeps the checked lifetime start from forging a handle.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] LinearHandle {
    int handle = 0;
    constexpr explicit LinearHandle(int value) noexcept : handle{value} {}
    LinearHandle(LinearHandle&&) noexcept = default;
    LinearHandle(LinearHandle const&) = delete("a linear handle has one owner");
    constexpr LinearHandle& operator=(LinearHandle&& other) noexcept {
        handle = other.handle;
        return *this;
    }
};
struct PlainPair {
    int first = 0;
    int second = 0;
};
template <class Held>
Held ref_of(Option<Held>);
extern Option<int> const held_number;
extern Option<PlainPair> const held_pair;
static_assert(std::is_same_v<decltype(target_of(ref_of(as_ref(held_number)))), Target<int const>>);
static_assert(::foundation::brand::IsFreshBrand<decltype(brand_of(ref_of(as_ref(held_number))))>);
static_assert(!std::is_same_v<decltype(as_ref(held_number)), decltype(as_ref(held_number))>);
static_assert(sizeof(decltype(as_ref(held_number))) == sizeof(void*));
static_assert(std::is_same_v<decltype(target_of(ref_of(as_ref(held_pair)))), Target<PlainPair const>>);
template <class O>
constexpr bool borrows_with_as_ref = requires(O option) { as_ref(static_cast<O&&>(option)); };
static_assert(borrows_with_as_ref<Option<int>&> && borrows_with_as_ref<Option<int> const&>);
static_assert(!borrows_with_as_ref<Option<int>> && !borrows_with_as_ref<Option<int> const>);
static_assert(!borrows_with_as_ref<Option<LinearHandle>&> && !borrows_with_as_ref<Option<LinearHandle> const&>);

// The Ref reads the payload in a constant evaluation, and an empty Option
// gives no Ref.
consteval bool reads_through_as_ref() {
    Option<int> const full = Option<int>::some(9);
    Option<int> const empty{none};
    return as_ref(full).is_some_and([](auto const& ref) noexcept { return *ref == 9; }) && as_ref(empty).is_none();
}
static_assert(reads_through_as_ref());

// ── Address ────────────────────────────────────────────────────────────

// One word, and no address from an integer, from a pointer or with no object.
static_assert(sizeof(Address) == sizeof(std::uintptr_t));
static_assert(!std::is_default_constructible_v<Address>);
static_assert(!std::is_constructible_v<Address, std::uintptr_t>);
static_assert(!std::is_constructible_v<Address, int*>);
static_assert(!std::is_trivially_copyable_v<Address>);

// The constructor of an Address takes the key, and only AddressDoor builds
// a key.
static_assert(!std::is_default_constructible_v<detail::AddressOfKey>);
static_assert(!std::is_copy_constructible_v<detail::AddressOfKey>);
static_assert(!std::is_copy_assignable_v<detail::AddressOfKey>);
static_assert(!std::is_trivially_copyable_v<detail::AddressOfKey>);

// An address compares for equality and has no order.
static_assert(std::equality_comparable<Address>);
static_assert(!std::totally_ordered<Address>);

// ── AddressKey ─────────────────────────────────────────────────────────

// One word, and no key from an integer, from a pointer or with no address.
// No key is trivially copyable, so std::bit_cast reads no integer from a key.
static_assert(sizeof(AddressKey) == sizeof(std::uintptr_t));
static_assert(!std::is_default_constructible_v<AddressKey>);
static_assert(!std::is_constructible_v<AddressKey, std::uintptr_t>);
static_assert(!std::is_constructible_v<AddressKey, int*>);
static_assert(!std::is_trivially_copyable_v<AddressKey>);
static_assert(std::is_nothrow_copy_constructible_v<AddressKey>);
static_assert(std::is_nothrow_copy_assignable_v<AddressKey>);

// A key compares for equality, gives a hash of 64 bits, and has no order.
static_assert(std::equality_comparable<AddressKey>);
static_assert(!std::totally_ordered<AddressKey>);
static_assert(std::is_same_v<decltype(std::declval<AddressKey const&>().hash()), std::uint64_t>);

// The key of the null address is a constant, which a constinit table holds.
// Its hash is zero, as the hash of each key of a null pointer is.
static_assert(AddressKey::of_null() == AddressKey::of_null() && AddressKey::of_null().hash() == 0);

// A key comes from a pointer to an object or a void pointer, and not from a
// pointer to a function.
static_assert(AddressKeyTarget<int>);
static_assert(AddressKeyTarget<int const>);
static_assert(AddressKeyTarget<void>);
static_assert(AddressKeyTarget<int volatile>);
static_assert(!AddressKeyTarget<void(int)>);
static_assert(!AddressKeyTarget<int&>);
static_assert(std::is_same_v<decltype(mint_address_key(std::declval<int*>())), AddressKey>);
static_assert(std::is_same_v<decltype(mint_address_key(std::declval<void const*>())), AddressKey>);

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

// ── Hole ───────────────────────────────────────────────────────────────

// One pointer.  A Hole is linear: move-only, with a move and an end that do
// not throw, and not trivially copyable.
static_assert(sizeof(Hole<8, 8>) == sizeof(void*));
static_assert(sizeof(Hole<4096, 4096>) == sizeof(void*));
static_assert(::foundation::linear::is_linear_v<Hole<8, 8>>);
static_assert(!std::is_copy_constructible_v<Hole<8, 8>>);
static_assert(!std::is_copy_assignable_v<Hole<8, 8>>);
static_assert(std::is_nothrow_move_constructible_v<Hole<8, 8>>);
static_assert(std::is_nothrow_move_assignable_v<Hole<8, 8>>);
static_assert(!std::is_trivially_copyable_v<Hole<8, 8>>);
static_assert(!std::is_default_constructible_v<Hole<8, 8>>);

// No pointer becomes a Hole, and each constructor takes the key, which only
// HoleDoor builds.
static_assert(!std::is_constructible_v<Hole<8, 8>, void*>);
static_assert(!std::is_constructible_v<Hole<8, 8>, Box<std::uint64_t>&&>);
static_assert(!std::is_default_constructible_v<detail::HoleKey>);
static_assert(!std::is_copy_constructible_v<detail::HoleKey>);
static_assert(!std::is_copy_assignable_v<detail::HoleKey>);
static_assert(!std::is_trivially_copyable_v<detail::HoleKey>);

// The shape of a Hole: a size of one byte or more, and an alignment that is
// a power of two.
static_assert(HoleShape<1, 1>);
static_assert(HoleShape<24, 8>);
static_assert(!HoleShape<0, 8>);
static_assert(!HoleShape<8, 0>);
static_assert(!HoleShape<8, 12>);

// The gate of the mint of a Hole takes the allocation tag only.
static_assert(CanMintHole<8, 8, Alloc>);
static_assert(!CanMintHole<8, 8, NoMarker>);
static_assert(!CanMintHole<8, 3, Alloc>);

// A Box builds in a Hole of its exact size and alignment only.
static_assert(CanMintBoxInHole<std::uint64_t, sizeof(std::uint64_t), alignof(std::uint64_t), std::uint64_t>);
static_assert(!CanMintBoxInHole<std::uint64_t, 16, alignof(std::uint64_t)>);
static_assert(!CanMintBoxInHole<std::uint64_t, sizeof(std::uint64_t), 16>);
static_assert(!CanMintBoxInHole<std::uint32_t, sizeof(std::uint64_t), alignof(std::uint64_t)>);
static_assert(!CanMintBoxInHole<std::uint64_t, sizeof(std::uint64_t), alignof(std::uint64_t), char const*>);

// The mint of a Hole from a Box gives a Hole of the size and the alignment
// of the object.  The fixture neg_core_hole_of_lvalue_box shows that the
// mint takes an rvalue Box only.
static_assert(std::is_same_v<decltype(mint_hole(std::declval<Box<std::uint64_t>>())),
                             Hole<sizeof(std::uint64_t), alignof(std::uint64_t)>>);

// ── The closed surface ─────────────────────────────────────────────────

// The family list of the Ref family (plan section 11.5).  Each public member
// of a type of the family is a shared verb, a language protocol or a name of
// this list.  Ref and Box have no empty state, so they take the dereference.
inline constexpr TextView ref_names{
    "Ref to as_ref same_object downcast KindFamily KindTagged Box mint_box BumpArena make "
    "array copy_text Slot emplace clear FnRef CallRef Address address_of is_aligned "
    "AddressKey mint_address_key of_null hash lower_by Hole mint_hole BoxWord box_word_key "
    "release_to_word claim_box is_empty"};
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<int, CheckBrand>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<int const, CheckBrand>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Ref<Abstract, CheckBrand>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Box<int>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Box<OverAligned>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(ref_names, Address);
CRUCIBLE_CLOSED_SURFACE(ref_names, AddressKey);
CRUCIBLE_CLOSED_SURFACE(ref_names, Hole<8, 8>);
CRUCIBLE_CLOSED_SURFACE(ref_names, BoxWord<int, WordAuthority>);

}  // namespace detail::ref_checks

}  // namespace foundation::core
