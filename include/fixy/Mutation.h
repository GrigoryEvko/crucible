#pragma once

#include <fixy/GradedFacade.h>
#include <foundation/ByteSeal.h>
#include <foundation/Lifetime.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Number.h>
#include <foundation/reflect/Instance.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy {

template <typename T>
class WriteOnce;
template <typename Ptr>
class WriteOnceNonNull;

// The type of an AtomicMonotonic names the writer of the counter (THE WRITER
// OF A COUNTER, at AtomicMonotonic).  void names a counter that each thread
// can advance.  A SoleWriterProof<Owner, Role> names a counter of one writer.
//
// Monotonic names AtomicMonotonic as a friend, for the checked publish that
// steps the copy of the counter (advance_sole_writer_from with a copy).
template <typename T, typename Writer = void, typename Cmp = std::less<T>>
    requires std::is_trivially_copyable_v<T>
class AtomicMonotonic;

// The claims the carriers below make that no lattice grades.  Each is a
// declared-only identity that foundation/diag/RowHash.h folds, so a
// carrier here takes a cache slot of its own rather than the zero every
// bare payload shares.  AppendOnly and Monotonic grade on a lattice and
// need none.
namespace row_discipline {
template <typename KeyFn, typename Cmp>
struct ordered_append_only;
template <auto Max>
struct bounded_monotonic;
template <std::uint64_t MaxStep>
struct wide_monotonic;
struct write_once;
struct write_once_non_null;
template <typename Cmp>
struct atomic_monotonic;
}  // namespace row_discipline

// Both traits are the one reflection query of foundation/reflect/Instance.h
// asked of a different template.  The struct forms stay, because callers
// read `::value` off them.  Struct and value are each derived from the
// concept, so neither is a door: a specialization of one changes what
// its own readers see and nothing the concept answers.
template <typename T>
struct is_writeonce : std::bool_constant<::foundation::reflect::IsInstanceOf<T, ^^WriteOnce>> {};
template <typename T>
inline constexpr bool is_writeonce_v = ::foundation::reflect::IsInstanceOf<T, ^^WriteOnce>;

template <typename T>
struct is_writeoncenonnull : std::bool_constant<::foundation::reflect::IsInstanceOf<T, ^^WriteOnceNonNull>> {};
template <typename T>
inline constexpr bool is_writeoncenonnull_v = ::foundation::reflect::IsInstanceOf<T, ^^WriteOnceNonNull>;

// Every constructor below is private.  The mint_* function declared
// beside each class is its one door, and the concept on that door
// names what the class asks of its type arguments.

// The container operations AppendOnly forwards to, and the size the
// lattice derives its grade from.
template <typename T, template <typename...> class Storage>
concept AppendOnlyStorage =
    std::default_initializable<Storage<T>> && std::move_constructible<Storage<T>>
    && requires(Storage<T>& mutable_storage, Storage<T> const& storage, T item, std::size_t index) {
           typename Storage<T>::const_iterator;
           mutable_storage.emplace_back(std::move(item));
           { storage.size() } -> std::convertible_to<std::size_t>;
           { storage.empty() } -> std::convertible_to<bool>;
           { storage[index] } -> std::convertible_to<T const&>;
           { storage.front() } -> std::convertible_to<T const&>;
           { storage.back() } -> std::convertible_to<T const&>;
           { storage.begin() } -> std::same_as<typename Storage<T>::const_iterator>;
           { storage.end() } -> std::same_as<typename Storage<T>::const_iterator>;
       };

// Cmp{}(a, b) must mean "a is strictly less than b" on the carrier, and
// the carrier moves into and out of the lattice.
template <typename T, typename Cmp>
concept MonotoneCarrier =
    std::move_constructible<T> && std::default_initializable<Cmp> && std::strict_weak_order<Cmp, T const&, T const&>;

// The two orders whose direction a step of one can read.  std::less moves
// a cell up, and std::greater moves it down.  The bump of each monotone
// cell steps in that direction, and a cell over another order has no bump.
template <typename T, typename Cmp>
concept AscendingOrder = std::same_as<Cmp, std::less<T>> || std::same_as<Cmp, std::less<>>;

template <typename T, typename Cmp>
concept DescendingOrder = std::same_as<Cmp, std::greater<T>> || std::same_as<Cmp, std::greater<>>;

// The integer types that std::cmp_less and std::in_range accept: every
// integral type except bool and the character types.
template <typename T>
concept ComparableInteger =
    std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool> && !std::same_as<std::remove_cv_t<T>, char>
    && !std::same_as<std::remove_cv_t<T>, wchar_t> && !std::same_as<std::remove_cv_t<T>, char8_t>
    && !std::same_as<std::remove_cv_t<T>, char16_t> && !std::same_as<std::remove_cv_t<T>, char32_t>;

// The carrier and the bound are integers, and the carrier holds the
// bound exactly.  A conversion to a carrier that cannot hold the bound
// changes it: 300 becomes 44 in a uint8_t, and -1 becomes the maximum
// of a uint32_t.  Every comparison against the bound is a std::cmp_*
// call, so a signed bound and an unsigned carrier compare by value.
template <typename T, auto Max, typename Cmp>
concept BoundedMonotoneCarrier =
    MonotoneCarrier<T, Cmp> && ComparableInteger<T> && ComparableInteger<decltype(Max)> && (std::in_range<T>(Max));

// The nullptr sentinel exists only for a pointer.
template <typename Ptr>
concept SlotPointer = std::is_pointer_v<Ptr>;

template <typename T, template <typename...> class Storage = std::vector>
class AppendOnly;

template <typename T, template <typename...> class Storage = std::vector>
    requires AppendOnlyStorage<T, Storage>
[[nodiscard]] constexpr AppendOnly<T, Storage>
mint_append_only() noexcept(std::is_nothrow_default_constructible_v<Storage<T>>);

template <typename T, template <typename...> class Storage>
class [[nodiscard]] AppendOnly
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                           ::foundation::algebra::lattices::SeqPrefixLattice<T>, Storage<T>> {
    static_assert(!is_writeonce_v<T>, "AppendOnly<WriteOnce<T>> is redundant: AppendOnly already guarantees "
                                      "that emplaced elements are never mutated, reassigned, or removed. "
                                      "Use AppendOnly<T> directly — the WriteOnce layer adds no invariant "
                                      "and doubles per-element storage by one std::optional tag byte.");
    static_assert(!is_writeoncenonnull_v<T>,
                  "[AppendOnly_Over_WriteOnceNonNull_Redundant] AppendOnly<WriteOnceNonNull<T*>> "
                  "is redundant: AppendOnly already guarantees that emplaced elements "
                  "are never mutated, reassigned, or removed, which subsumes "
                  "WriteOnceNonNull's single-set guarantee.  Use AppendOnly<T*> directly "
                  "when the elements are pointers, and rely on a per-insertion "
                  "non-null contract at the call site if that is the real invariant. "
                  "(symmetric with the AppendOnly<WriteOnce<T>> rejection above)");

public:
    // The one wrapper whose value_type is not the substrate's: it wraps
    // a container and grades the container, while what it holds is the
    // element.  Declaring value_type here hides the base's, and the
    // member below is what tells GradedWrapper to admit the mismatch.
    // It is a member, because a class body cannot be reopened from
    // another translation unit the way a trait can be specialized from
    // one.
    // modality and the two name forwarders still arrive from
    // graded_facade.
    using value_type = T;
    static constexpr bool value_type_decoupled = true;
    using storage_type = Storage<T>;
    using const_iterator = typename Storage<T>::const_iterator;

    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::SeqPrefixLattice<T>, Storage<T>>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

private:
    graded_type impl_;

    using key_ = ::foundation::algebra::grade_key<AppendOnly>;

    constexpr AppendOnly() noexcept(std::is_nothrow_default_constructible_v<Storage<T>>) : impl_{Storage<T>{}} {}

    template <typename U, template <typename...> class S>
        requires AppendOnlyStorage<U, S>
    friend constexpr AppendOnly<U, S> mint_append_only() noexcept(std::is_nothrow_default_constructible_v<S<U>>);

public:
    // An assignment would drop every element the log holds.
    AppendOnly(AppendOnly const&) = default;
    AppendOnly(AppendOnly&&) = default;
    AppendOnly& operator=(AppendOnly const&) =
        delete("AppendOnly never removes an element. An assignment would drop every element it holds.");
    AppendOnly& operator=(AppendOnly&&) =
        delete("AppendOnly never removes an element. An assignment would drop every element it holds.");

    // The lattice grade is derived from the container size, so growing
    // the tail updates it with no separate field to maintain.  Growing is
    // the one write this class makes, which keeps the derived grade
    // moving up the order.
    template <typename... Args>
    void emplace(Args&&... args) {
        impl_.peek_mut(key_{}).emplace_back(std::forward<Args>(args)...);
    }

    void append(T item) { impl_.peek_mut(key_{}).emplace_back(std::move(item)); }

    // Each access checks its index in each build.  The bounds check of the
    // standard library is on only in Debug, so it is no check of a Release
    // build.  OrderedAppendOnly reads through these members, so it gets the
    // same checks.
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept {
        CRUCIBLE_PRE(i < size());
        return impl_.peek()[i];
    }
    [[nodiscard]] const T& front() const noexcept {
        CRUCIBLE_PRE(!empty());
        return impl_.peek().front();
    }
    [[nodiscard]] const T& back() const noexcept {
        CRUCIBLE_PRE(!empty());
        return impl_.peek().back();
    }
    [[nodiscard]] std::size_t size() const noexcept { return impl_.peek().size(); }
    [[nodiscard]] bool empty() const noexcept { return impl_.peek().empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return impl_.peek().begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return impl_.peek().end(); }

    [[nodiscard]] Storage<T> drain() && noexcept(std::is_nothrow_move_constructible_v<Storage<T>>) {
        return std::move(impl_).consume();
    }

    // value_type_name reports the storage type, not the element type.
    // The graded value here is the container.
};

template <typename T, template <typename...> class Storage>
    requires AppendOnlyStorage<T, Storage>
[[nodiscard]] constexpr AppendOnly<T, Storage>
mint_append_only() noexcept(std::is_nothrow_default_constructible_v<Storage<T>>) {
    return AppendOnly<T, Storage>{};
}

// KeyFn and Cmp must be empty and default-constructible, and the two
// static_asserts below reject anything else.
//
// The requirement is semantic before it is structural. append compares
// each item against the one already at the back and against nothing
// else, so the ordering claim of the whole sequence is the conjunction
// of those pairwise answers. That conjunction means "sorted" only while
// one fixed comparator gives every answer. A comparator carrying state
// can answer differently for the same pair at two different appends, and
// the accepted sequence is then ordered under no single comparator --
// the invariant the wrapper exists to hold stops being a property of the
// contents. The same argument applies to a projection that reads state:
// the key of an element would depend on when it was appended.
//
// Emptiness is also what the layout pin in the check file of this header
// relies on. Both
// members collapse into the AppendOnly through [[no_unique_address]]
// only while they have nothing to store.
//
// append reads the members, and it does not construct a fresh KeyFn{}
// and Cmp{} for each call. The two are equivalent for an empty functor.
// Reading the members keeps the code honest if the constraint is ever
// relaxed, because a stateful functor is then read and not ignored.

template <typename T, typename KeyFn = std::identity, typename Cmp = std::less<>,
          template <typename...> class Storage = std::vector>
class OrderedAppendOnly;

template <typename T, typename KeyFn = std::identity, typename Cmp = std::less<>,
          template <typename...> class Storage = std::vector>
    requires AppendOnlyStorage<T, Storage>
[[nodiscard]] constexpr OrderedAppendOnly<T, KeyFn, Cmp, Storage>
mint_ordered_append_only() noexcept(std::is_nothrow_default_constructible_v<Storage<T>>);

template <typename T, typename KeyFn, typename Cmp, template <typename...> class Storage>
class [[nodiscard]] OrderedAppendOnly {
    static_assert(std::is_empty_v<KeyFn> && std::is_default_constructible_v<KeyFn>,
                  "OrderedAppendOnly: KeyFn must be empty and default-constructible. A projection that reads state "
                  "makes an element's key depend on when it was appended, so the stored order stops being a property "
                  "of the contents.");
    static_assert(std::is_empty_v<Cmp> && std::is_default_constructible_v<Cmp>,
                  "OrderedAppendOnly: Cmp must be empty and default-constructible. append compares only against the "
                  "back element, so a comparator that changes its answer between calls admits a sequence that is "
                  "ordered under no single comparator.");

    AppendOnly<T, Storage> inner_ = mint_append_only<T, Storage>();
    [[no_unique_address]] KeyFn key_{};
    [[no_unique_address]] Cmp cmp_{};

    constexpr OrderedAppendOnly() = default;

    template <typename U, typename K, typename C, template <typename...> class S>
        requires AppendOnlyStorage<U, S>
    friend constexpr OrderedAppendOnly<U, K, C, S>
    mint_ordered_append_only() noexcept(std::is_nothrow_default_constructible_v<S<U>>);

public:
    using value_type = T;
    using row_discipline = ::fixy::row_discipline::ordered_append_only<KeyFn, Cmp>;
    using row_payload = AppendOnly<T, Storage>;
    using key_type = std::invoke_result_t<KeyFn, const T&>;
    using key_fn_type = KeyFn;
    using comparator = Cmp;
    using storage_type = Storage<T>;
    using const_iterator = typename AppendOnly<T, Storage>::const_iterator;

    OrderedAppendOnly(OrderedAppendOnly const&) = default;
    OrderedAppendOnly(OrderedAppendOnly&&) = default;
    OrderedAppendOnly& operator=(OrderedAppendOnly const&) =
        delete("OrderedAppendOnly never removes an element. An assignment would drop every element it holds.");
    OrderedAppendOnly& operator=(OrderedAppendOnly&&) =
        delete("OrderedAppendOnly never removes an element. An assignment would drop every element it holds.");

    void append(T item) {
        CRUCIBLE_PRE(inner_.empty() || !cmp_(key_(item), key_(inner_.back())));
        inner_.append(std::move(item));
    }

    // The key of an element cannot be projected before the element
    // exists, so emplace builds the element first and then checks it.
    template <typename... Args>
    void emplace(Args&&... args) {
        append(T{std::forward<Args>(args)...});
    }

    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return inner_[i]; }
    [[nodiscard]] const T& front() const noexcept { return inner_.front(); }
    [[nodiscard]] const T& back() const noexcept { return inner_.back(); }
    [[nodiscard]] std::size_t size() const noexcept { return inner_.size(); }
    [[nodiscard]] bool empty() const noexcept { return inner_.empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return inner_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return inner_.end(); }

    [[nodiscard]] Storage<T> drain() && noexcept(std::is_nothrow_move_constructible_v<Storage<T>>) {
        return std::move(inner_).drain();
    }
};

template <typename T, typename KeyFn, typename Cmp, template <typename...> class Storage>
    requires AppendOnlyStorage<T, Storage>
[[nodiscard]] constexpr OrderedAppendOnly<T, KeyFn, Cmp, Storage>
mint_ordered_append_only() noexcept(std::is_nothrow_default_constructible_v<Storage<T>>) {
    return OrderedAppendOnly<T, KeyFn, Cmp, Storage>{};
}

// The limits of a WideMonotonic (THE WIDE COUNTER below): its start is below
// 2^62, and its bound of one step is at most 2^32.
inline constexpr std::uint64_t wide_start_limit = std::uint64_t{1} << 62;
inline constexpr std::uint64_t wide_step_limit = std::uint64_t{1} << 32;

// The bound of one step of a WideMonotonic: at least one, so that each step
// moves the count, and at most wide_step_limit.
template <std::uint64_t MaxStep>
concept WideStepBound = MaxStep >= 1 && MaxStep <= wide_step_limit;

// Monotonic names WideMonotonic as a friend, for the step that its type
// proves.
template <std::uint64_t MaxStep>
    requires WideStepBound<MaxStep>
class WideMonotonic;

// Cmp fixes the direction of travel. With the default std::less,
// advance accepts a value that is not less than the current one.
// std::greater gives decreasing-only semantics.

template <typename T, typename Cmp = std::less<T>>
class Monotonic;

template <typename T, typename Cmp = std::less<T>>
    requires MonotoneCarrier<T, Cmp>
[[nodiscard]] constexpr Monotonic<T, Cmp> mint_monotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>);

template <typename T, typename Cmp>
class [[nodiscard]] Monotonic : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                                     ::foundation::algebra::lattices::MonotoneLattice<T, Cmp>, T> {
public:
    using comparator_type = Cmp;

    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the two names this
    // class body uses unqualified are re-declared here rather than
    // found by lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::MonotoneLattice<T, Cmp>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

private:
    graded_type impl_;

    constexpr explicit Monotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(initial)} {}

    template <typename U, typename C>
        requires MonotoneCarrier<U, C>
    friend constexpr Monotonic<U, C> mint_monotonic(U initial) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <typename U, typename W, typename C>
        requires std::is_trivially_copyable_v<U>
    friend class AtomicMonotonic;

    // A WideMonotonic writes the value of its step with no order check.
    template <std::uint64_t MaxStep>
        requires WideStepBound<MaxStep>
    friend class WideMonotonic;

    // The step of a copy of a counter after the checked publish of
    // AtomicMonotonic proved that the copy holds current.  The order check
    // compares two values in registers, so it reads the value of the copy no
    // second time after the store of the counter.
    constexpr void advance_from_checked_(T current, T new_value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        CRUCIBLE_PRE(lattice_type::leq(current, new_value));
        impl_ = graded_type{std::move(new_value)};
    }

public:
    // An assignment writes any value over the current one, so it would
    // move the counter backward.  reset_under_quiescence is the one
    // backward step, and it says so at the call site.
    constexpr Monotonic(Monotonic const&) = default;
    constexpr Monotonic(Monotonic&&) = default;
    Monotonic& operator=(Monotonic const&) = delete(
        "Monotonic only advances. An assignment would move it backward. Use advance, or use reset_under_quiescence to "
        "go back.");
    Monotonic& operator=(Monotonic&&) = delete(
        "Monotonic only advances. An assignment would move it backward. Use advance, or use reset_under_quiescence to "
        "go back.");

    [[nodiscard]] constexpr const T& get() const noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr const T& current() const noexcept { return impl_.peek(); }

    // lattice_type::leq(current, new_value) is exactly
    // !Cmp{}(new_value, current): the new value does not go backward.
    constexpr void advance(T new_value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        CRUCIBLE_PRE(lattice_type::leq(impl_.peek(), new_value));
        impl_ = graded_type{std::move(new_value)};
    }

    constexpr bool try_advance(T new_value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (!lattice_type::leq(impl_.peek(), new_value)) return false;
        impl_ = graded_type{std::move(new_value)};
        return true;
    }

    // A step of one in the direction of Cmp: std::less adds one, and
    // std::greater takes one away.  A step past the end of T would wrap the
    // value to the other end, so the precondition refuses a value at the end
    // of T.  The step goes from the value that the precondition read, so the
    // order check of the step compares two registers, and the optimizer
    // folds it into the precondition.
    constexpr void bump() noexcept
        requires ::foundation::core::Integer<T> && (AscendingOrder<T, Cmp> || DescendingOrder<T, Cmp>)
    {
        T const prior = impl_.peek();
        CRUCIBLE_PRE(prior
                     != ((AscendingOrder<T, Cmp>) ? ::foundation::core::max_of<T> : ::foundation::core::min_of<T>));
        T const stepped = AscendingOrder<T, Cmp> ? static_cast<T>(prior + T{1}) : static_cast<T>(prior - T{1});
        advance_from_checked_(prior, stepped);
    }

    // The only way to move the value backward. The caller must ensure
    // nothing else observes or advances it across this call. The type
    // system cannot prove quiescence.
    constexpr void reset_under_quiescence(T value = T{}) noexcept(std::is_nothrow_move_constructible_v<T>) {
        impl_ = graded_type{std::move(value)};
    }
};

template <typename T, typename Cmp>
    requires MonotoneCarrier<T, Cmp>
[[nodiscard]] constexpr Monotonic<T, Cmp> mint_monotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Monotonic<T, Cmp>{std::move(initial)};
}

// This is not Refined<bounded_above<Max>, Monotonic<T>> because Refined
// checks its predicate once at construction, so every later advance on
// the inner Monotonic escapes the bound and the alias fails silently.
// Nesting a Monotonic and re-applying the bound at each mutation site
// is the form that holds.

template <typename T, auto Max, typename Cmp = std::less<T>>
class BoundedMonotonic;

template <typename T, auto Max, typename Cmp = std::less<T>>
    requires BoundedMonotoneCarrier<T, Max, Cmp>
[[nodiscard]] constexpr BoundedMonotonic<T, Max, Cmp>
mint_bounded_monotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>);

template <typename T, auto Max, typename Cmp>
class [[nodiscard]] BoundedMonotonic {
    Monotonic<T, Cmp> inner_;
    // No byte route builds a counter, so no counter holds a value above
    // its bound.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // Exact: the door's concept proved that T holds the bound.
    static constexpr T kMax = static_cast<T>(Max);

    // The bound is checked before the member takes the value, as a
    // precondition of the constructor.
    [[nodiscard]] static constexpr T within_bound_(T initial) noexcept(std::is_nothrow_move_constructible_v<T>) {
        CRUCIBLE_PRE(std::cmp_less_equal(initial, Max));
        return initial;
    }

    constexpr explicit BoundedMonotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>)
        : inner_{mint_monotonic<T, Cmp>(within_bound_(std::move(initial)))} {}

    template <typename U, auto M, typename C>
        requires BoundedMonotoneCarrier<U, M, C>
    friend constexpr BoundedMonotonic<U, M, C>
    mint_bounded_monotonic(U initial) noexcept(std::is_nothrow_move_constructible_v<U>);

public:
    using value_type = T;
    using comparator_type = Cmp;
    using row_discipline = ::fixy::row_discipline::bounded_monotonic<Max>;
    using row_payload = Monotonic<T, Cmp>;
    static constexpr T max() noexcept { return kMax; }

    constexpr BoundedMonotonic(BoundedMonotonic const&) = default;
    constexpr BoundedMonotonic(BoundedMonotonic&&) = default;
    BoundedMonotonic& operator=(BoundedMonotonic const&) =
        delete("BoundedMonotonic only advances. An assignment would move it backward.");
    BoundedMonotonic&
    operator=(BoundedMonotonic&&) = delete("BoundedMonotonic only advances. An assignment would move it backward.");

    [[nodiscard]] constexpr const T& get() const noexcept { return inner_.get(); }
    [[nodiscard]] constexpr const T& current() const noexcept { return inner_.current(); }

    constexpr void advance(T new_value) noexcept(std::is_nothrow_move_assignable_v<T>) {
        CRUCIBLE_PRE(std::cmp_less_equal(new_value, Max));
        inner_.advance(std::move(new_value));
    }

    constexpr bool try_advance(T new_value) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (std::cmp_greater(new_value, Max)) return false;
        return inner_.try_advance(std::move(new_value));
    }

    // A step of one in the direction of Cmp, as the bump of Monotonic.  An
    // ascending counter stays at most Max.  A descending counter starts at
    // most Max and moves down, so the bound holds for each step.  The step
    // always inlines, so a caller that tests the bound first folds the check
    // of the precondition.
    CRUCIBLE_INLINE constexpr void bump() noexcept
        requires(AscendingOrder<T, Cmp> || DescendingOrder<T, Cmp>)
    {
        CRUCIBLE_PRE((DescendingOrder<T, Cmp>) || std::cmp_less(inner_.get(), Max));
        inner_.bump();
    }

    // The only way to move the value backward. The caller must ensure
    // nothing else observes or advances it across this call. The type
    // system cannot prove quiescence.  No other thread reads the counter
    // during the call.  The value stays at most Max, as at construction.
    constexpr void reset_under_quiescence(T value = T{}) noexcept(std::is_nothrow_move_constructible_v<T>) {
        CRUCIBLE_PRE(std::cmp_less_equal(value, Max));
        inner_.reset_under_quiescence(std::move(value));
    }
};

template <typename T, auto Max, typename Cmp>
    requires BoundedMonotoneCarrier<T, Max, Cmp>
[[nodiscard]] constexpr BoundedMonotonic<T, Max, Cmp>
mint_bounded_monotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return BoundedMonotonic<T, Max, Cmp>{std::move(initial)};
}

// THE WIDE COUNTER
//   A WideMonotonic is a count of 64 bits that only goes up, and its step
//   holds no check.  Two facts carry the order of the step:
//     - The count starts below wide_start_limit (2^62).  A start is a
//       WideStart, and its one door from a value checks the limit, one time
//       for each mint or reset.
//     - A step adds at least one and at most MaxStep (WideStepBound).  bump
//       adds one.  A step of a plain integer does not compile, because its
//       size has no bound.
//   Each unit of the count stands for one item of work of the caller, such as
//   an iteration of a region or a slot of a ring.  Each item takes at least
//   one store, and a core does at most a few stores each nanosecond.  From a
//   start below 2^62, the count gets to the end of 64 bits only after more
//   than 3 * 2^62 items, which is more than 100 years of such work.  Thus no
//   step wraps the count, and a bump is one add.

// The start of a WideMonotonic: a value below wide_start_limit.  Each door
// gives a value below the limit, and no copy of bytes makes a start.
class [[nodiscard]] WideStart {
    std::uint64_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit WideStart(std::uint64_t value) noexcept : value_{value} {}

public:
    // The start of a new count.
    [[nodiscard]] static constexpr WideStart zero() noexcept { return WideStart{0}; }

    // The start at a value that the caller computed, such as the position of
    // a reset.  The check runs one time for each start, in each build and
    // under each contract semantic, because the unchecked step of the count
    // is sound only below the limit.
    [[nodiscard]] static constexpr WideStart check_below_limit(std::uint64_t value) noexcept {
        CRUCIBLE_FATAL_INVARIANT(value < wide_start_limit);
        return WideStart{value};
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
};

template <std::uint64_t MaxStep>
    requires WideStepBound<MaxStep>
[[nodiscard]] constexpr WideMonotonic<MaxStep> mint_wide_monotonic(WideStart start = WideStart::zero()) noexcept;

template <std::uint64_t MaxStep>
    requires WideStepBound<MaxStep>
class [[nodiscard]] WideMonotonic {
    Monotonic<std::uint64_t> inner_;
    // No byte route builds a count, so no count holds a value that no door
    // gave.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit WideMonotonic(WideStart start) noexcept : inner_{mint_monotonic<std::uint64_t>(start.value())} {}

    template <std::uint64_t S>
        requires WideStepBound<S>
    friend constexpr WideMonotonic<S> mint_wide_monotonic(WideStart start) noexcept;

public:
    using value_type = std::uint64_t;
    using row_discipline = ::fixy::row_discipline::wide_monotonic<MaxStep>;
    using row_payload = Monotonic<std::uint64_t>;
    static constexpr std::uint64_t max_step() noexcept { return MaxStep; }

    constexpr WideMonotonic(WideMonotonic const&) = default;
    constexpr WideMonotonic(WideMonotonic&&) = default;
    WideMonotonic& operator=(WideMonotonic const&) = delete(
        "WideMonotonic only goes up. An assignment would move it backward. Use reset_under_quiescence to go back");
    WideMonotonic& operator=(WideMonotonic&&) = delete(
        "WideMonotonic only goes up. An assignment would move it backward. Use reset_under_quiescence to go back");

    [[nodiscard]] constexpr const std::uint64_t& get() const noexcept { return inner_.get(); }
    [[nodiscard]] constexpr const std::uint64_t& current() const noexcept { return inner_.current(); }

    // A step of one.  THE WIDE COUNTER proves that the step does not wrap,
    // so the step is one add, with no compare.
    CRUCIBLE_INLINE constexpr void bump() noexcept {
        inner_.impl_ = Monotonic<std::uint64_t>::graded_type{inner_.get() + std::uint64_t{1}};
    }

    template <class Count>
        requires std::integral<Count>
    void step(Count) =
        delete("WideMonotonic: a step of a plain integer has no bound in its type, so it can wrap the count. Use bump");

    // The only way to move the count backward.  No other thread reads or
    // steps the count during the call.  The type system cannot prove that.
    // The start is below the limit, as at the mint.
    constexpr void reset_under_quiescence(WideStart start) noexcept { inner_.reset_under_quiescence(start.value()); }
};

template <std::uint64_t MaxStep>
    requires WideStepBound<MaxStep>
[[nodiscard]] constexpr WideMonotonic<MaxStep> mint_wide_monotonic(WideStart start) noexcept {
    return WideMonotonic<MaxStep>{start};
}

template <typename T>
    requires std::move_constructible<T>
[[nodiscard]] constexpr WriteOnce<T> mint_write_once() noexcept;

template <typename T>
class [[nodiscard]] WriteOnce {
    std::optional<T> value_;

    constexpr WriteOnce() = default;

    template <typename U>
        requires std::move_constructible<U>
    friend constexpr WriteOnce<U> mint_write_once() noexcept;

public:
    using value_type = T;
    using row_discipline = ::fixy::row_discipline::write_once;
    using row_payload = T;

    // An assignment would empty a set slot, and set would then succeed a
    // second time.
    constexpr WriteOnce(WriteOnce const&) = default;
    constexpr WriteOnce(WriteOnce&&) = default;
    WriteOnce& operator=(WriteOnce const&) =
        delete("WriteOnce is set at most once. An assignment would empty or overwrite a set slot.");
    WriteOnce& operator=(WriteOnce&&) =
        delete("WriteOnce is set at most once. An assignment would empty or overwrite a set slot.");

    constexpr void set(T v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        CRUCIBLE_PRE(!value_.has_value());
        value_.emplace(std::move(v));
        CRUCIBLE_POST(0, value_.has_value());
    }

    // The post holds on both paths: either this call emplaced, or an
    // earlier one did, because otherwise claimed would be true.
    constexpr bool try_set(T v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        bool const claimed = !value_.has_value();
        if (claimed) {
            value_.emplace(std::move(v));
        }
        CRUCIBLE_POST(claimed, value_.has_value());
        return claimed;
    }

    [[nodiscard]] constexpr const T& get() const noexcept {
        CRUCIBLE_PRE(value_.has_value());
        return *value_;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return value_.has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_.has_value(); }
};

template <typename T>
    requires std::move_constructible<T>
[[nodiscard]] constexpr WriteOnce<T> mint_write_once() noexcept {
    return WriteOnce<T>{};
}

// nullptr is the unset sentinel, which is what collapses storage to
// exactly sizeof(T*) and is why a null argument is never publishable.

template <typename Ptr>
    requires SlotPointer<Ptr>
[[nodiscard]] constexpr WriteOnceNonNull<Ptr> mint_write_once_non_null() noexcept;

template <typename Ptr>
class WriteOnceNonNull {
    static_assert(std::is_pointer_v<Ptr>, "[WriteOnceNonNull_NonPointer_Type] WriteOnceNonNull requires "
                                          "a pointer type argument (e.g. WriteOnceNonNull<MyClass*>). "
                                          "The nullptr-sentinel strategy that makes this primitive "
                                          "zero-overhead is meaningful only for pointer types; for "
                                          "single-set slots over non-pointer values use WriteOnce<T> "
                                          "(sizeof(T) + 1 byte std::optional tag).");
};

template <typename T>
class [[nodiscard]] WriteOnceNonNull<T*> {
    T* ptr_ = nullptr;

    constexpr WriteOnceNonNull() noexcept = default;

    template <typename P>
        requires SlotPointer<P>
    friend constexpr WriteOnceNonNull<P> mint_write_once_non_null() noexcept;

public:
    using value_type = T*;
    using pointee_type = T;
    using row_discipline = ::fixy::row_discipline::write_once_non_null;
    using row_payload = T*;

    constexpr WriteOnceNonNull(WriteOnceNonNull const&) noexcept = default;
    constexpr WriteOnceNonNull(WriteOnceNonNull&&) noexcept = default;
    WriteOnceNonNull& operator=(WriteOnceNonNull const&) =
        delete("WriteOnceNonNull is set at most once. An assignment would empty or overwrite a set slot.");
    WriteOnceNonNull& operator=(WriteOnceNonNull&&) =
        delete("WriteOnceNonNull is set at most once. An assignment would empty or overwrite a set slot.");

    constexpr void set(T* p) noexcept {
        CRUCIBLE_PRE(p != nullptr);
        CRUCIBLE_PRE(ptr_ == nullptr);
        ptr_ = p;
        CRUCIBLE_POST(0, ptr_ == p);
    }

    // The disjunction covers every path. A null argument leaves the
    // slot untouched. A non-null argument leaves it set, either by this
    // call or by an earlier one.
    [[nodiscard]] constexpr bool try_set(T* p) noexcept {
        bool const claimed = (ptr_ == nullptr) && (p != nullptr);
        if (claimed) {
            ptr_ = p;
        }
        CRUCIBLE_POST(claimed, p == nullptr || ptr_ != nullptr);
        return claimed;
    }

    [[nodiscard]] constexpr T* get() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return ptr_;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // The defaulted U=T parameter delays instantiation of the return
    // type until the operator is called, so a void pointee can still
    // use the other members without forming a reference to void at
    // class-instantiation time.
    template <typename U = T>
        requires(!std::is_void_v<U>)
    [[nodiscard]] constexpr U& operator*() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return *ptr_;
    }

    template <typename U = T>
        requires(!std::is_void_v<U>)
    [[nodiscard]] constexpr U* operator->() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return ptr_;
    }
};

template <typename Ptr>
    requires SlotPointer<Ptr>
[[nodiscard]] constexpr WriteOnceNonNull<Ptr> mint_write_once_non_null() noexcept {
    return WriteOnceNonNull<Ptr>{};
}

// Pinned because the atomic is the identity of the counter. Moving it
// would fork the monotonic sequence across two atomics.
//
// alignas(64) hoists cache-line isolation into the type. Every advance
// invalidates the reader's line, so an embedder that shares that line
// with unrelated state pays for it on every step. Placing the
// alignment here makes an embedder that forgets it safe anyway, at a
// cost of one cache line per counter.
//
// The counter is a cell of its value: its one atomic is private, each step of
// it is atomic, and a value that one thread stores, another thread loads.  So
// the class states payload_cell{} (foundation/Transfer.h), and it holds each
// transfer property, the shared write included, exactly when T is Sendable.
//
// THE WRITER OF A COUNTER
//   The second type argument names the writer.  void names a counter that
//   each thread can advance, with a compare-and-swap.  A SoleWriterProof<
//   Owner, Role> names a counter of one writer, the holder of that proof
//   (THE PROOF OF THE ONE WRITER of foundation/SoleWriterProof.h).  The
//   publishes of the one writer take exactly that proof type, and only Owner
//   makes it, so a proof of another owner or another role has no overload.
//   The writer of a counter of one writer reads its own value with relaxed
//   order (peek_relaxed).  A counter of many writers has no such read,
//   because a writer there reads values that a different thread stored.

// The proof of the one writer of a counter (THE PROOF OF THE ONE WRITER of
// foundation/SoleWriterProof.h).
using ::foundation::SoleWriterProof;

// True when the writer of a counter is the proof of one writer (THE WRITER OF
// A COUNTER).
template <typename Writer>
inline constexpr bool names_sole_writer_v = false;
template <class Owner, class Role>
inline constexpr bool names_sole_writer_v<SoleWriterProof<Owner, Role>> = true;

// The gate of the reads and the writes of the one writer of a counter:
// peek_relaxed, advance_sole_writer_from and advance_sole_writer_by_proof.
// Each of the members asks for this one concept, so a stronger gate changes
// one place.
template <typename Writer>
concept SoleWriterCounter = names_sole_writer_v<Writer>;

template <typename T, typename Writer = void, typename Cmp = std::less<T>>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr AtomicMonotonic<T, Writer, Cmp> mint_atomic_monotonic(T initial) noexcept;

template <typename T, typename Writer, typename Cmp>
    requires std::is_trivially_copyable_v<T>
class [[nodiscard]][[= ::foundation::transfer::payload_cell{}]] alignas(64) AtomicMonotonic
    : ::foundation::Pinned<AtomicMonotonic<T, Writer, Cmp>> {
    static_assert(std::is_void_v<Writer> || SoleWriterCounter<Writer>,
                  "AtomicMonotonic<T, Writer, Cmp>: the writer is void, for a counter that each thread can advance, "
                  "or SoleWriterProof<Owner, Role>, for a counter of one writer (THE WRITER OF A COUNTER)");

    // The proof type of the publishes of the one writer: the writer itself.  A
    // counter of many writers names a class that no code can make, so its
    // declarations stay well formed, and their gate removes them.
    struct no_sole_writer_ final {
        no_sole_writer_() = delete("a counter of many writers has no proof of one writer");
    };
    using sole_writer_proof_ = std::conditional_t<SoleWriterCounter<Writer>, Writer, no_sole_writer_>;

    std::atomic<T> value_;

    static constexpr bool kIsLess = AscendingOrder<T, Cmp>;
    static constexpr bool kIsGreater = DescendingOrder<T, Cmp>;
    static constexpr bool kFastPathEligible = std::integral<T> && (kIsLess || kIsGreater);

    constexpr explicit AtomicMonotonic(T initial) noexcept : value_{initial} {}

    template <typename U, typename W, typename C>
        requires std::is_trivially_copyable_v<U>
    friend constexpr AtomicMonotonic<U, W, C> mint_atomic_monotonic(U initial) noexcept;

public:
    using value_type = T;
    using comparator_type = Cmp;
    using row_discipline = ::fixy::row_discipline::atomic_monotonic<Cmp>;
    using row_payload = T;
    using writer_type = Writer;

    // The value, with acquire order: the read of each thread.  It pairs with
    // the release half of each write.  A read that takes an order as an
    // argument does not exist: the order of each read is in its name.
    [[nodiscard]] T get() const noexcept { return value_.load(std::memory_order_acquire); }

    T load(std::memory_order) const noexcept = delete(
        "AtomicMonotonic: the order of a read is in its name.  get() reads with acquire order, peek_relaxed() is the "
        "read of the one writer of its own value, and load_relaxed() seeds a compare-and-swap loop");

    // Relaxed is sound only because the caller is the sole writer of
    // this counter and is reading its own value. A reader on another
    // thread needs get(), whose acquire pairs with the release half of
    // advance and bump.
    [[nodiscard]] T peek_relaxed() const noexcept
        requires SoleWriterCounter<Writer>
    {
        return value_.load(std::memory_order_relaxed);
    }
    T peek_relaxed() const noexcept
        requires(!SoleWriterCounter<Writer>)
    = delete("AtomicMonotonic::peek_relaxed: a counter of many writers has no thread that reads its own last store. "
             "Read it with get(), or seed a compare-exchange with load_relaxed()");

    // The seed of a compare-and-swap loop of a writer that is not the one
    // writer.  No thread owns the value that it reads, and the read gives
    // the caller no data: the compare-and-swap that follows it reads the
    // value again in its own atomic step.  On success that step makes the
    // edge of the order, and on failure it gives the expected value of the
    // next turn.  So the seed needs no order.
    [[nodiscard]] T load_relaxed() const noexcept { return value_.load(std::memory_order_relaxed); }

    [[nodiscard]] bool try_advance(T new_value) noexcept {
        if constexpr (kFastPathEligible && kIsLess) {
            const T prev = value_.fetch_max(new_value, std::memory_order_acq_rel);
            return prev < new_value;
        } else if constexpr (kFastPathEligible && kIsGreater) {
            const T prev = value_.fetch_min(new_value, std::memory_order_acq_rel);
            return prev > new_value;
        } else {
            Cmp cmp;
            T observed = value_.load(std::memory_order_acquire);
            while (cmp(observed, new_value)) {
                if (value_.compare_exchange_weak(observed, new_value, std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
                    return true;
            }
            return false;
        }
    }

    // Safe with many writers.  The value it replaces is read in the same
    // atomic step as the replacement, and the precondition holds on that
    // value, so a writer that another writer overtook fails the check.
    // fetch_max and fetch_min never store a value behind the current one,
    // and the compare-exchange loop checks before it stores.
    void advance(T new_value) noexcept {
        if constexpr (kFastPathEligible && kIsLess) {
            const T replaced = value_.fetch_max(new_value, std::memory_order_acq_rel);
            CRUCIBLE_PRE(replaced < new_value);
        } else if constexpr (kFastPathEligible && kIsGreater) {
            const T replaced = value_.fetch_min(new_value, std::memory_order_acq_rel);
            CRUCIBLE_PRE(replaced > new_value);
        } else {
            Cmp cmp;
            T observed = value_.load(std::memory_order_acquire);
            do {
                CRUCIBLE_PRE(cmp(observed, new_value));
            } while (!value_.compare_exchange_weak(observed, new_value, std::memory_order_acq_rel,
                                                   std::memory_order_acquire));
        }
    }

    // For the one thread that writes this counter, such as the producer
    // of a single-producer ring.  The proof is the writer that the type of
    // the counter names (THE WRITER OF A COUNTER), so a thread that holds no
    // such proof has no overload.  current is the value that the counter
    // holds, from that thread's own load or its own copy of its last store.
    // The precondition compares two values in registers, so it folds to one
    // compare where the step is visibly forward, and a step that wraps past
    // the end of T still fails it.  The check after it makes sure that
    // current is the stored value, in each build.  It reads the counter with
    // a relaxed load, which is correct because the caller is the one writer.
    // The check is CRUCIBLE_FATAL_INVARIANT, because an [[assume]] of the
    // ignore semantic cannot hold the load.  The store is a plain release
    // with no locked instruction.  Two writers use advance instead: between
    // the check and the store here, another writer can pass the new value.
    // A counter of many writers has no such write, so the member asks for
    // the gate of the one writer, as peek_relaxed does.
    void advance_sole_writer_from(sole_writer_proof_ const& /*proof*/, T current, T new_value) noexcept
        requires SoleWriterCounter<Writer>
    {
        CRUCIBLE_PRE(Cmp{}(current, new_value));
        // Equal under the order, which asks of T no operator==.  The relaxed
        // load reads the own last store of the one writer.
        T const stored = value_.load(std::memory_order_relaxed);
        CRUCIBLE_FATAL_INVARIANT(!Cmp{}(current, stored) && !Cmp{}(stored, current));
        value_.store(new_value, std::memory_order_release);
    }
    template <class Owner, class Role>
    void advance_sole_writer_from(SoleWriterProof<Owner, Role> const& /*proof*/, T current, T new_value) noexcept
        requires(!SoleWriterCounter<Writer>)
    = delete("AtomicMonotonic::advance_sole_writer_from: a counter of many writers has no one writer, and a second "
             "writer can pass the new value between the check and the store.  Use advance, or name the proof of "
             "the one writer as the writer of a counter of one writer");

    // The checked publish of the one writer that keeps a copy of the counter
    // on a line of its own, such as the producer of a ring.  own is that
    // copy, and it holds the value of the counter, as for
    // advance_sole_writer_by_proof.  The call checks the counter against
    // current as the form above does, so the check proves that own holds
    // current too.  Then own steps from current, so the step check compares
    // two values in registers, and the call loads own no second time after
    // the store of the counter.
    void advance_sole_writer_from(sole_writer_proof_ const& proof, T current, T new_value,
                                  Monotonic<T, Cmp>& own) noexcept
        requires SoleWriterCounter<Writer>
    {
        advance_sole_writer_from(proof, current, new_value);
        own.advance_from_checked_(current, new_value);
    }
    template <class Owner, class Role>
    void advance_sole_writer_from(SoleWriterProof<Owner, Role> const& /*proof*/, T current, T new_value,
                                  Monotonic<T, Cmp>& own) noexcept
        requires(!SoleWriterCounter<Writer>)
    = delete("AtomicMonotonic::advance_sole_writer_from: a counter of many writers has no one writer for a copy to "
             "follow.  Use advance, or name the proof of the one writer as the writer of a counter of one writer");

    // The publish of the one writer that holds the proof that the type of the
    // counter names (THE WRITER OF A COUNTER).  own is the copy of the
    // counter that the
    // writer keeps on a line of its own, and it holds the value of the
    // counter.  The step check compares own and new_value in registers, in
    // each build and under each contract semantic.  Then own steps from the
    // value that the check read, so the order check of that step compares the
    // same two registers, and the optimizer folds it into the step check.
    // The store is a plain release, and the call
    // loads nothing from the counter, so a reader that polls the line of the
    // counter costs the writer no transfer of that line.
    void advance_sole_writer_by_proof(sole_writer_proof_ const& /*proof*/, Monotonic<T, Cmp>& own, T new_value) noexcept
        requires SoleWriterCounter<Writer>
    {
        T const current = own.get();
        CRUCIBLE_FATAL_INVARIANT(Cmp{}(current, new_value));
        own.advance_from_checked_(current, new_value);
        value_.store(new_value, std::memory_order_release);
    }
    template <class Owner, class Role>
    void advance_sole_writer_by_proof(SoleWriterProof<Owner, Role> const& /*proof*/, Monotonic<T, Cmp>& own,
                                      T new_value) noexcept
        requires(!SoleWriterCounter<Writer>)
    = delete("AtomicMonotonic::advance_sole_writer_by_proof: a counter of many writers has no one writer for a proof "
             "to name.  Use advance, or name the proof of the one writer as the writer of a counter of one writer");

    // delta is a magnitude, not a signed step. Cmp fixes the direction:
    // std::less adds and std::greater subtracts. A negative delta would
    // step the counter against Cmp and break monotonicity, which is
    // what the first precondition rules out. The check is vacuous for
    // unsigned T.  A step past the end of T would wrap the counter to
    // the other end, which is the other way to go backward.  The second
    // precondition holds on the value the step replaces, which the same
    // compare-exchange that stores the step reads, and it is checked
    // before the store.
    [[nodiscard]] T bump_by(T delta) noexcept
        requires std::integral<T> && (kIsLess || kIsGreater)
    {
        CRUCIBLE_PRE(::foundation::decide::non_negative(delta));
        // Each writer of the counter reads a value that a different thread
        // can have stored, so the seed and the failure of the compare-and-
        // swap read with acquire order.
        T observed = value_.load(std::memory_order_acquire);
        for (;;) {
            if constexpr (kIsLess) {
                CRUCIBLE_PRE(::foundation::decide::no_overflow_sum(observed, delta));
            } else {
                CRUCIBLE_PRE(observed >= static_cast<T>(std::numeric_limits<T>::min() + delta));
            }
            const T stepped = kIsLess ? static_cast<T>(observed + delta) : static_cast<T>(observed - delta);
            if (value_.compare_exchange_weak(observed, stepped, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return observed;
            }
        }
    }

    [[nodiscard]] T bump() noexcept
        requires std::integral<T> && (kIsLess || kIsGreater)
    {
        return bump_by(T{1});
    }

    // The only way to move the counter backward, and the reason
    // Pinned does not make reset impossible. Safe only when every
    // thread touching the counter is quiescent. The caller owns that
    // precondition, because the type system cannot prove it.
    void reset_under_quiescence(T value = T{}) noexcept { value_.store(value, std::memory_order_release); }

    // THE COMPARE-AND-SWAP FORMS.  Each advances the counter from EXPECTED
    // to DESIRED, and DESIRED must be ahead of EXPECTED under Cmp.  A
    // failure writes the value that it read into EXPECTED.  The order of
    // each form is in its name, so no caller gives an order, and no form
    // takes a failure order that the standard forbids.

    // Strong.  acq_rel on success, acquire on failure.
    [[nodiscard]] bool compare_exchange_advance_acq_rel(T& expected, T desired) noexcept
        requires(kIsLess || kIsGreater)
    {
        CRUCIBLE_PRE(Cmp{}(expected, desired));
        return value_.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // Weak, for a loop: it can fail with no other writer.  acq_rel on
    // success, acquire on failure.
    [[nodiscard]] bool compare_exchange_advance_weak_acq_rel(T& expected, T desired) noexcept
        requires(kIsLess || kIsGreater)
    {
        CRUCIBLE_PRE(Cmp{}(expected, desired));
        return value_.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // Strong.  seq_cst on success, relaxed on failure: the order of the top
    // of the Chase-Lev deque (Le, Pop, Cohen and Zappa Nardelli, PPoPP
    // 2013).  The owner and a thief race for the last item at top, and the
    // single total order of the seq_cst operations with the fences of the
    // deque decides which one takes it.  A failure only gives up the item,
    // and it reads no data, so it needs no order.
    [[nodiscard]] bool compare_exchange_advance_seq_cst(T& expected, T desired) noexcept
        requires(kIsLess || kIsGreater)
    {
        CRUCIBLE_PRE(Cmp{}(expected, desired));
        return value_.compare_exchange_strong(expected, desired, std::memory_order_seq_cst, std::memory_order_relaxed);
    }

    // Weak, relaxed on success and on failure: the claim of a ticket of the
    // MPSC ring of fixy/concurrent/MpscRing.h.  The claim carries no data:
    // the release store of the turn word of the cell publishes the item, and
    // the acquire load of the turn word by the consumer reads it.  The
    // producer reads the turn word of its cell with acquire order before the
    // claim.  So the claim needs no order.
    [[nodiscard]] bool compare_exchange_advance_weak_relaxed(T& expected, T desired) noexcept
        requires(kIsLess || kIsGreater)
    {
        CRUCIBLE_PRE(Cmp{}(expected, desired));
        return value_.compare_exchange_weak(expected, desired, std::memory_order_relaxed, std::memory_order_relaxed);
    }

    // A pure global ordering operation that touches no atomic, so it
    // is static. It lives on this class because every call site that
    // needs it brackets the fence with loads and stores of a counter
    // of this type, and keeping the two together makes the pairing
    // visible.
    static void fence_seq_cst() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }
};

template <typename T, typename Writer, typename Cmp>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr AtomicMonotonic<T, Writer, Cmp> mint_atomic_monotonic(T initial) noexcept {
    return AtomicMonotonic<T, Writer, Cmp>{initial};
}

template <typename T>
using MaxObserved = AtomicMonotonic<T, void, std::less<T>>;

}  // namespace fixy
