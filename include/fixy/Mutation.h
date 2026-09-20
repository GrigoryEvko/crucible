#pragma once

#include <fixy/GradedFacade.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/Instance.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy {

template <typename T>
class WriteOnce;
template <typename Ptr>
class WriteOnceNonNull;

// Both traits are the one reflection query of foundation/reflect/Instance.h
// asked of a different template.  The struct forms stay, because callers
// read `::value` off them.
template <typename T>
struct is_writeonce : std::bool_constant<::foundation::reflect::is_instance_of_v<T, ^^WriteOnce>> {};
template <typename T>
inline constexpr bool is_writeonce_v = is_writeonce<std::remove_cvref_t<T>>::value;

template <typename T>
struct is_writeoncenonnull : std::bool_constant<::foundation::reflect::is_instance_of_v<T, ^^WriteOnceNonNull>> {};
template <typename T>
inline constexpr bool is_writeoncenonnull_v = is_writeoncenonnull<std::remove_cvref_t<T>>::value;

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

// The bound converts to the carrier, and the carrier orders against it.
template <typename T, auto Max, typename Cmp>
concept BoundedMonotoneCarrier = MonotoneCarrier<T, Cmp> && std::totally_ordered<T> && requires { T(Max); };

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
    // element.  Declaring value_type here hides the base's, and
    // value_type_decoupled in GradedTrait.h is what admits the
    // mismatch.  modality and the two name forwarders still arrive from
    // graded_facade.
    using value_type = T;
    using storage_type = Storage<T>;
    using const_iterator = typename Storage<T>::const_iterator;

    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::SeqPrefixLattice<T>, Storage<T>>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

private:
    graded_type impl_;

    constexpr AppendOnly() noexcept(std::is_nothrow_default_constructible_v<Storage<T>>) : impl_{Storage<T>{}} {}

    template <typename U, template <typename...> class S>
        requires AppendOnlyStorage<U, S>
    friend constexpr AppendOnly<U, S> mint_append_only() noexcept(std::is_nothrow_default_constructible_v<S<U>>);

public:
    // The lattice grade is derived from the container size, so growing
    // the tail updates it with no separate field to maintain.
    template <typename... Args>
    void emplace(Args&&... args) {
        impl_.peek_mut().emplace_back(std::forward<Args>(args)...);
    }

    void append(T item) { impl_.peek_mut().emplace_back(std::move(item)); }

    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return impl_.peek()[i]; }
    [[nodiscard]] const T& front() const noexcept { return impl_.peek().front(); }
    [[nodiscard]] const T& back() const noexcept { return impl_.peek().back(); }
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

static_assert(sizeof(AppendOnly<char*>) == sizeof(std::vector<char*>),
              "AppendOnly<char*> must collapse to sizeof(Storage<char*>). The grade "
              "(Length{size_t}) is computed from c.size() rather than stored "
              "separately, which holds only while SeqPrefixLattice supplies a "
              "grade_of trait method.");
static_assert(sizeof(AppendOnly<std::uint64_t>) == sizeof(std::vector<std::uint64_t>),
              "AppendOnly<uint64_t> must collapse to sizeof(Storage<T>). The grade is "
              "computed from c.size() rather than stored separately.");

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
// Emptiness is also what the layout static_assert below relies on. Both
// members collapse into the AppendOnly through [[no_unique_address]]
// only while they have nothing to store.
//
// Before this constraint the members existed but were dead: append
// constructed a fresh KeyFn{} and Cmp{} per call instead of reading
// them, so a stateful functor was accepted and then ignored. append now
// reads the members, which is equivalent for an empty functor and keeps
// the code honest if the constraint is ever relaxed.

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
    using key_type = std::invoke_result_t<KeyFn, const T&>;
    using key_fn_type = KeyFn;
    using comparator = Cmp;
    using storage_type = Storage<T>;
    using const_iterator = typename AppendOnly<T, Storage>::const_iterator;

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

static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
              "OrderedAppendOnly must collapse empty KeyFn/Cmp to zero layout cost");

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

public:
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

    // Overflow is the only way an integral counter can go backward, so
    // the precondition rules out wraparound: peek() != max means
    // peek() + 1 still fits in T.
    constexpr void bump() noexcept
        requires std::integral<T>
    {
        CRUCIBLE_PRE(impl_.peek() != std::numeric_limits<T>::max());
        T const prior = impl_.peek();
        impl_ = graded_type{static_cast<T>(prior + T{1})};
        CRUCIBLE_POST(0, impl_.peek() == static_cast<T>(prior + T{1}));
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

static_assert(sizeof(Monotonic<uint32_t, std::less<uint32_t>>) == sizeof(uint32_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: value and grade have the "
              "same type and collapse to one storage cell.");
static_assert(sizeof(Monotonic<uint64_t, std::less<uint64_t>>) == sizeof(uint64_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: value and grade have the "
              "same type and collapse to one storage cell.");

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

    static constexpr T kMax = T(Max);

    constexpr explicit BoundedMonotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(!(T(Max) < initial))
        : inner_{mint_monotonic<T, Cmp>(std::move(initial))} {}

    template <typename U, auto M, typename C>
        requires BoundedMonotoneCarrier<U, M, C>
    friend constexpr BoundedMonotonic<U, M, C>
    mint_bounded_monotonic(U initial) noexcept(std::is_nothrow_move_constructible_v<U>);

public:
    using value_type = T;
    using comparator_type = Cmp;
    static constexpr T max() noexcept { return kMax; }

    [[nodiscard]] constexpr const T& get() const noexcept { return inner_.get(); }
    [[nodiscard]] constexpr const T& current() const noexcept { return inner_.current(); }

    constexpr void advance(T new_value) noexcept(std::is_nothrow_move_assignable_v<T>) pre(!(T(Max) < new_value)) {
        inner_.advance(std::move(new_value));
    }

    constexpr bool try_advance(T new_value) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (T(Max) < new_value) return false;
        return inner_.try_advance(std::move(new_value));
    }

    constexpr void bump() noexcept
        requires std::integral<T>
    {
        CRUCIBLE_PRE(inner_.get() < T(Max));
        inner_.advance(static_cast<T>(inner_.get() + T{1}));
    }
};

template <typename T, auto Max, typename Cmp>
    requires BoundedMonotoneCarrier<T, Max, Cmp>
[[nodiscard]] constexpr BoundedMonotonic<T, Max, Cmp>
mint_bounded_monotonic(T initial) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return BoundedMonotonic<T, Max, Cmp>{std::move(initial)};
}

static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t),
              "BoundedMonotonic must collapse to underlying T");

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

    [[nodiscard]] constexpr const T& get_assuming_set() const noexcept {
        [[assume(value_.has_value())]];
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

static_assert(sizeof(WriteOnceNonNull<int*>) == sizeof(int*));
static_assert(sizeof(WriteOnceNonNull<void*>) == sizeof(void*));

// Pinned because the atomic is the identity of the counter. Moving it
// would fork the monotonic sequence across two atomics.
//
// alignas(64) hoists cache-line isolation into the type. Every advance
// invalidates the reader's line, so an embedder that shares that line
// with unrelated state pays for it on every step. Placing the
// alignment here makes an embedder that forgets it safe anyway, at a
// cost of one cache line per counter.

template <typename T, typename Cmp = std::less<T>>
    requires std::is_trivially_copyable_v<T>
class AtomicMonotonic;

template <typename T, typename Cmp = std::less<T>>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr AtomicMonotonic<T, Cmp> mint_atomic_monotonic(T initial) noexcept;

template <typename T, typename Cmp>
    requires std::is_trivially_copyable_v<T>
class [[nodiscard]] alignas(64) AtomicMonotonic : ::foundation::Pinned<AtomicMonotonic<T, Cmp>> {
    std::atomic<T> value_;

    static constexpr bool kIsLess = std::is_same_v<Cmp, std::less<T>> || std::is_same_v<Cmp, std::less<>>;
    static constexpr bool kIsGreater = std::is_same_v<Cmp, std::greater<T>> || std::is_same_v<Cmp, std::greater<>>;
    static constexpr bool kFastPathEligible = std::integral<T> && (kIsLess || kIsGreater);

    constexpr explicit AtomicMonotonic(T initial) noexcept : value_{initial} {}

    template <typename U, typename C>
        requires std::is_trivially_copyable_v<U>
    friend constexpr AtomicMonotonic<U, C> mint_atomic_monotonic(U initial) noexcept;

public:
    using value_type = T;
    using comparator_type = Cmp;

    [[nodiscard]] T get() const noexcept { return value_.load(std::memory_order_acquire); }

    // Relaxed is sound only because the caller is the sole writer of
    // this counter and is reading its own value. A reader on another
    // thread needs get(), whose acquire pairs with the release half of
    // advance and bump.
    [[nodiscard]] T peek_relaxed() const noexcept { return value_.load(std::memory_order_relaxed); }

    // Same load as peek_relaxed, but for a caller that is not the sole
    // writer and is seeding a compare-and-advance retry loop. Relaxed
    // is sound because the following compare-exchange supplies the
    // synchronization: on success it establishes the happens-before
    // edge, and on failure it refreshes the expected value.
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

    void advance(T new_value) noexcept {
        CRUCIBLE_PRE(Cmp{}(value_.load(std::memory_order_acquire), new_value));
        value_.store(new_value, std::memory_order_release);
    }

    // delta is a magnitude, not a signed step. Cmp fixes the direction:
    // std::less adds and std::greater subtracts. A negative delta would
    // step the counter against Cmp and break monotonicity, which is
    // what the precondition rules out. The check is vacuous for
    // unsigned T.
    [[nodiscard]] T bump_by(T delta) noexcept
        requires std::integral<T> && (kIsLess || kIsGreater)
    pre(::foundation::decide::non_negative(delta)) {
        if constexpr (kIsLess) {
            return value_.fetch_add(delta, std::memory_order_acq_rel);
        } else {
            return value_.fetch_sub(delta, std::memory_order_acq_rel);
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

    [[nodiscard]] T load(std::memory_order order = std::memory_order_acquire) const noexcept {
        return value_.load(order);
    }

    void store(T new_value, std::memory_order order = std::memory_order_release) noexcept {
        CRUCIBLE_PRE(Cmp{}(value_.load(std::memory_order_acquire), new_value));
        value_.store(new_value, order);
    }

    // failure_order must not be release or acq_rel. The standard
    // forbids it and nothing here rejects it, so the caller owns that
    // constraint.
    [[nodiscard]] bool compare_exchange_advance(T& expected, T desired,
                                                std::memory_order success_order = std::memory_order_acq_rel,
                                                std::memory_order failure_order = std::memory_order_acquire) noexcept
        requires(kIsLess || kIsGreater)
    pre(Cmp{}(expected, desired)) {
        return value_.compare_exchange_strong(expected, desired, success_order, failure_order);
    }

    [[nodiscard]] bool
    compare_exchange_advance_weak(T& expected, T desired, std::memory_order success_order = std::memory_order_acq_rel,
                                  std::memory_order failure_order = std::memory_order_acquire) noexcept
        requires(kIsLess || kIsGreater)
    pre(Cmp{}(expected, desired)) {
        return value_.compare_exchange_weak(expected, desired, success_order, failure_order);
    }

    // A pure global ordering operation that touches no atomic, so it
    // is static. It lives on this class because every call site that
    // needs it brackets the fence with loads and stores of a counter
    // of this type, and keeping the two together makes the pairing
    // visible.
    static void fence_seq_cst() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }
};

template <typename T, typename Cmp>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr AtomicMonotonic<T, Cmp> mint_atomic_monotonic(T initial) noexcept {
    return AtomicMonotonic<T, Cmp>{initial};
}

static_assert(alignof(AtomicMonotonic<uint64_t>) >= 64, "AtomicMonotonic must be cache-line aligned: repeated "
                                                        "advance/bump/CAS traffic invalidates the consumer's "
                                                        "cached line every iteration, so the counter must not "
                                                        "share a line with unrelated embedder state.");
static_assert(alignof(AtomicMonotonic<uint32_t>) >= 64);
static_assert(sizeof(AtomicMonotonic<uint64_t>) >= 64, "AtomicMonotonic occupies a full cache line by "
                                                       "construction; embedders rely on the counter NOT "
                                                       "sharing a line with any field touched on the "
                                                       "producer/consumer hot path.");
static_assert(sizeof(AtomicMonotonic<uint32_t>) >= 64);

template <typename T>
using MaxObserved = AtomicMonotonic<T, std::less<T>>;

namespace detail::mutation_self_test {

inline void runtime_smoke_test() {
    int seed = 3;

    AppendOnly<int> ao = mint_append_only<int>();
    ao.emplace(seed);
    ao.append(seed + 1);
    ao.append(seed + 2);
    if (ao.size() != 3) std::abort();
    if (ao[0] != 3 || ao[1] != 4 || ao[2] != 5) std::abort();
    if (ao.front() != 3 || ao.back() != 5) std::abort();
    if (ao.empty()) std::abort();
    int sum = 0;
    for (auto it = ao.begin(); it != ao.end(); ++it)
        sum += *it;
    if (sum != 12) std::abort();

    OrderedAppendOnly<int> oao = mint_ordered_append_only<int>();
    oao.append(1);
    oao.append(seed);
    oao.append(seed + 4);
    if (oao.size() != 3) std::abort();
    if (oao.back() != 7) std::abort();

    const auto useed = static_cast<uint32_t>(seed);
    Monotonic<uint32_t> mono = mint_monotonic<uint32_t>(0);
    mono.advance(useed);
    if (mono.get() != 3u) std::abort();
    if (!mono.try_advance(useed + 5u)) std::abort();
    if (mono.get() != 8u) std::abort();
    if (mono.try_advance(2u)) std::abort();
    mono.bump();
    if (mono.get() != 9u) std::abort();

    BoundedMonotonic<uint32_t, 128u> bm = mint_bounded_monotonic<uint32_t, 128u>(0);
    bm.advance(useed);
    if (bm.get() != 3u) std::abort();
    if (!bm.try_advance(64u)) std::abort();
    if (bm.get() != 64u) std::abort();
    if (bm.try_advance(200u)) std::abort();

    WriteOnce<int> wo = mint_write_once<int>();
    if (wo.has_value()) std::abort();
    wo.set(seed * 11);
    if (!wo.has_value()) std::abort();
    if (wo.get() != 33) std::abort();
    if (wo.get_assuming_set() != 33) std::abort();
    if (wo.try_set(99)) std::abort();

    int target = 42;
    WriteOnceNonNull<int*> wonn = mint_write_once_non_null<int*>();
    if (wonn.has_value()) std::abort();
    wonn.set(&target);
    if (!wonn.has_value()) std::abort();
    if (*wonn != 42) std::abort();
    if (wonn.try_set(&target)) std::abort();

    AtomicMonotonic<uint64_t> am = mint_atomic_monotonic<uint64_t>(0ULL);
    am.advance(static_cast<uint64_t>(seed));
    if (am.get() != 3u) std::abort();
    if (!am.try_advance(static_cast<uint64_t>(seed + 7))) std::abort();
    if (am.get() != 10u) std::abort();
    auto prev = am.bump();
    if (prev != 10u || am.get() != 11u) std::abort();
}

}  // namespace detail::mutation_self_test

}  // namespace fixy

// AppendOnly's user-facing value_type is the element T, while its
// substrate grades the container Storage<T>. The specialization tells
// the wrapper concept to skip the equality check between the two.

namespace foundation::algebra {
template <typename T, template <typename...> class Storage>
struct value_type_decoupled<::fixy::AppendOnly<T, Storage>> : std::true_type {};
}  // namespace foundation::algebra
