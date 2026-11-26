// The compile-time checks of foundation/core/KeyedArray.h.

#include <foundation/core/KeyedArray.h>

#include <foundation/core/Array.h>
#include <foundation/core/Frame.h>
#include <foundation/reflect/Annotation.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::keyed_array_checks {

struct KeyedCheckBrand final {};
using KeyedWords = KeyedHeapArray<int, KeyedCheckBrand>;
using KeyedTable = KeyedMaskedArray<int, KeyedCheckBrand>;
using CheckKey = LoanKey<KeyedCheckBrand>;

// A holder is the array that it holds, with no other byte.
static_assert(sizeof(KeyedWords) == sizeof(HeapArray<int>));
static_assert(sizeof(KeyedTable) == sizeof(MaskedArray<int>));

// A holder does not copy and does not move, so no second name holds its
// storage.  The empty holder and a holder of an array are the two
// constructors.
static_assert(!std::is_copy_constructible_v<KeyedWords> && !std::is_move_constructible_v<KeyedWords>);
static_assert(!std::is_copy_assignable_v<KeyedWords> && !std::is_move_assignable_v<KeyedWords>);
static_assert(!std::is_copy_constructible_v<KeyedTable> && !std::is_move_constructible_v<KeyedTable>);
static_assert(!std::is_copy_assignable_v<KeyedTable> && !std::is_move_assignable_v<KeyedTable>);
static_assert(std::is_nothrow_default_constructible_v<KeyedWords>);
static_assert(std::is_nothrow_constructible_v<KeyedWords, HeapArray<int>&&>);
static_assert(!std::is_convertible_v<HeapArray<int>&&, KeyedWords>);
static_assert(std::is_nothrow_constructible_v<KeyedTable, MaskedArray<int>&&>);
static_assert(!std::is_constructible_v<KeyedWords, HeapArray<int> const&>);

// A holder carries the mark of a keyed owner, and it takes only a key brand.
static_assert(::foundation::reflect::carries_annotation(std::meta::dealias(^^KeyedWords), ^^loan_key_mark));
static_assert(::foundation::reflect::carries_annotation(std::meta::dealias(^^KeyedTable), ^^loan_key_mark));
template <class T, class Brand>
concept HasKeyedHeapArray = requires { typename KeyedHeapArray<T, Brand>; };
static_assert(HasKeyedHeapArray<int, KeyedCheckBrand>);
static_assert(!HasKeyedHeapArray<int, Unkeyed> && !HasKeyedHeapArray<int const, KeyedCheckBrand>);

// The reads give const elements.
static_assert(std::is_same_v<decltype(std::declval<KeyedWords&>().view()), View<int const>>);
static_assert(std::is_same_v<decltype(std::declval<KeyedTable&>().at_masked(3)), int const&>);

inline constexpr auto write_word = [](int& word) noexcept { word = 2; };
using WriteWord = decltype(write_word);
inline constexpr auto give_none = [] noexcept {};
using GiveNone = decltype(give_none);

// A body that reaches the key of the brand through a reference.  The checks
// read its type only, and no code builds one.
struct KeyReachingBody {
    CheckKey& key;
    void operator()(int& word) const noexcept { word = 1; }
};

// The element doors of a holder take the key, and the gate refuses a body
// that reaches it.
template <class Holder, class Body>
concept LendsElementWithKey = requires(Holder& holder, CheckKey& key, Body const& body) {
    holder.with_element(key, std::size_t{0}, body, give_none);
};
template <class Holder, class Body>
concept LendsElementWithNoKey =
    requires(Holder& holder, Body const& body) { holder.with_element(std::size_t{0}, body, give_none); };
template <class Holder, class Body>
concept LendsMaskedWithKey =
    requires(Holder& holder, CheckKey& key, Body const& body) { holder.with_masked(key, std::uint64_t{0}, body); };
template <class Holder, class Body>
concept LendsMaskedWithNoKey =
    requires(Holder& holder, Body const& body) { holder.with_masked(std::uint64_t{0}, body); };
static_assert(LendsElementWithKey<KeyedWords, WriteWord> && !LendsElementWithNoKey<KeyedWords, WriteWord>);
static_assert(!LendsElementWithKey<KeyedWords, KeyReachingBody>);
static_assert(!LendsElementWithKey<KeyedWords const, WriteWord>);
static_assert(LendsMaskedWithKey<KeyedTable, WriteWord> && !LendsMaskedWithNoKey<KeyedTable, WriteWord>);
static_assert(!LendsMaskedWithKey<KeyedTable, KeyReachingBody>);
static_assert(!LendsMaskedWithKey<KeyedTable const, WriteWord>);
// The control of LendsElementWithNoKey: the element door of a HeapArray.
static_assert(LendsElementWithNoKey<HeapArray<int>, WriteWord>);

// A holder has no value door, and fill, replace and take take the key.
template <class Holder>
concept SetsWithNoKey = requires(Holder& holder) { holder.set(std::size_t{0}, 2); };
template <class Holder>
concept SetsMaskedWithNoKey = requires(Holder& holder) { holder.set_masked(std::uint64_t{0}, 2); };
template <class Holder>
concept FillsWithNoKey = requires(Holder& holder) { holder.fill(2); };
template <class Holder, class Array>
concept ReplacesWithNoKey = requires(Holder& holder, Array&& fresh) { holder.replace(std::move(fresh)); };
template <class Holder>
concept TakesWithNoKey = requires(Holder& holder) { holder.take(); };
template <class Holder>
concept TakesBorrowedKey = requires(Holder& holder, CheckKey& key) { holder.take(key); };
static_assert(!SetsWithNoKey<KeyedWords> && !SetsMaskedWithNoKey<KeyedTable>);
static_assert(!FillsWithNoKey<KeyedWords> && !FillsWithNoKey<KeyedTable>);
static_assert(!ReplacesWithNoKey<KeyedWords, HeapArray<int>> && !ReplacesWithNoKey<KeyedTable, MaskedArray<int>>);
static_assert(!TakesWithNoKey<KeyedWords> && !TakesBorrowedKey<KeyedWords> && !TakesBorrowedKey<KeyedTable>);
static_assert(std::is_same_v<decltype(std::declval<KeyedWords&>().take(std::declval<CheckKey>())), HeapArray<int>>);
static_assert(std::is_same_v<decltype(std::declval<KeyedTable&>().take(std::declval<CheckKey>())), MaskedArray<int>>);

// The makers give a holder of the brand of the mint.
static_assert(KeyedOwnerMake<KeyedHeapArrayOwner<int>, KeyedCheckBrand, HeapArray<int>>);
static_assert(KeyedOwnerMake<KeyedMaskedArrayOwner<int>, KeyedCheckBrand, MaskedArray<int>>);
static_assert(!KeyedOwnerMake<KeyedHeapArrayOwner<int>, KeyedCheckBrand, HeapArray<int>&>);

}  // namespace detail::keyed_array_checks

}  // namespace foundation::core
