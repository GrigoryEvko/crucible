#pragma once

// The Ref family: one object and its owner.
//
// Ref<T, Brand> reaches one object of type T and is never null.  The brand
// is the identity of the borrow (foundation/Brand.h).  to(object) is the one
// door from an object: it gives a Ref with a fresh brand, or with the brand
// of an object that has one.  The brand is the last template parameter of
// to(), after a pack that refuses each explicit argument, so a caller cannot
// claim a brand for an object by spelling it.  to() of a temporary is
// deleted, because the Ref dangles at the end of the full expression.
//
// *ref and ref->member reach the object with no test, because a Ref has no
// empty state.  A Ref<T, Brand> converts to a Ref<T const, Brand> and to the
// erased brand DefaultBrand, and nothing converts it back.  same_object(a, b)
// tells if two Refs reach one object.  A Ref has no ==, no default value and
// no conversion from a pointer.  Option<Ref<T, Brand>> has the size of one
// pointer: the null pointer is its empty value, and no Ref holds it.  The
// word of a Ref in an atomic cell is that pointer, so an
// Atomic<Option<Ref<T, Brand>>> holds one pointer.  A Ref owns nothing, so a
// Ref that outlives its object dangles.  address_of(ref) gives the Address
// of the object, which answers is_aligned<N>() and ==, and gives no pointer
// back.
//
// Box<T> owns one object of type T on the heap.  The one door is
// mint_box<T>(alloc, args...): it takes the storage from the allocation
// door of foundation/core/Heap.h, builds the object from the arguments, and
// gives the Box.  The first argument is the allocation capability of
// foundation/effects, so each allocation shows at its call site.  The
// destructor destroys the object and gives the storage back to the door
// with the same size and alignment, so a Box never leaks and never frees
// twice.
//
// A Box is move-only.  A move leaves the source with no object, and the
// use-after-move guard refuses a later use of that source.  The one
// constructor of a Box from arguments takes the key of mint_box, and it
// allocates the storage itself.  So no pointer that a caller holds becomes
// a Box, and the destructor frees only storage that a Box allocated.
//
// operator* and operator-> give a borrow of the object, and view() gives
// the borrow as a Ref with the fresh brand of the call.  The overloads for
// an rvalue are deleted, because a borrow of a temporary Box dangles at the
// end of the full expression.  consume() moves the object out of a Box that
// the caller gives up, and frees the storage.
//
// C++ keeps a moved-from object alive, and no type can refuse its use.  So
// each access tests for the object in each build, and a Box with no object
// ends the process through fatal().  The test is one compare with a branch
// to a cold call.  A loop that reads the object many times takes one Ref
// with view() before the loop, and the Ref has no test.
//
// A Hole<Size, Align> is the credit of the family: the heap storage of one
// object with no object in it.  mint_hole<Size, Align>(alloc) takes the
// storage from the allocation door, and mint_hole(std::move(box)) ends the
// object of a Box and keeps its storage.  mint_box<T>(std::move(hole),
// args...) builds a T in the storage of a Hole of the exact size and
// alignment of T, with no call of the door.  consume() gives the storage
// back.  So N steps from a Box to a Hole and back call the door one time.
//
// A Hole is linear, and consume() is its drop door.  The destructor also
// gives the storage back, so a Hole that a path drops does not leak.  A move
// leaves the source empty, and each spend of an empty Hole ends the process
// through fatal().  No constructor of a Hole takes a pointer: one allocates
// the storage, and one takes the storage of a Box.  The two constructors
// take the key that only HoleDoor builds.
//
// Box and Hole name each other.  Each declaration of the two stands in this
// header with its definition, so a unit that sees one declaration also sees
// the definition, and a second definition is a compile error.  A header that
// declared Box and did not define it would let a unit define a Box of its
// own, which the friend declaration and the mint of a Hole would accept.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The object of a Ref: an object type that is not an array and not
// volatile.  An abstract class is a valid target, because a Ref builds
// nothing.
template <class T>
concept RefTarget = std::is_object_v<T> && !std::is_array_v<T> && !std::is_volatile_v<T>;

template <RefTarget T, ::foundation::brand::IsBrand Brand = ::foundation::brand::DefaultBrand>
class Ref;

namespace detail {

// A Ref<From, FromBrand> converts to a Ref<To, ToBrand> when the conversion
// adds a const, erases the brand, or does the two.  No conversion removes a
// const, and no conversion gives an erased Ref a brand.
template <class From, class FromBrand, class To, class ToBrand>
concept RefWidens = (std::same_as<From, To> || std::same_as<From const, To>)
                 && (std::same_as<FromBrand, ToBrand> || std::same_as<ToBrand, ::foundation::brand::DefaultBrand>)
                 && !(std::same_as<From, To> && std::same_as<FromBrand, ToBrand>);

// The door of an owner of the base that lends one of its objects with the
// fresh brand of its own call, as get() of the Region family does.  The
// definition is after Ref.
struct RefDoor;

// The key of the constructor of a Ref from an object.  RefDoor is its one
// friend, so each Ref from an object comes through the door.  Both
// constructors are user-provided and private, so std::bit_cast and a
// lifetime start over bytes build no key.
class [[nodiscard]] RefKey final {
    friend struct RefDoor;

    constexpr RefKey() noexcept {}
    constexpr RefKey(RefKey const&) noexcept {}

public:
    RefKey& operator=(RefKey const&) = delete("a key of a Ref lives for one construction");
};

}  // namespace detail

// The one door from an object.  The brand is the brand of a branded object,
// and the fresh brand of the call for each other object.  The pack in front
// takes each explicit template argument and refuses it, so no caller names
// the brand.
template <class... Refused, RefTarget T, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] constexpr Ref<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>> to(T& object) noexcept;

template <class... Refused, class T>
    requires(!std::is_lvalue_reference_v<T>)
void to(T&& object) = delete("a Ref of a temporary dangles at the end of the full expression");

template <RefTarget T, ::foundation::brand::IsBrand Brand>
class Ref {
    T* object_ = nullptr;
    // The seal makes the assignment user-provided, so std::bit_cast cannot
    // build a Ref from the bytes of an address.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // The tag of the null value.  Only the two hidden friends below build a
    // Ref from it, because Ref has no other friend.
    struct NullTag {};
    constexpr explicit Ref(NullTag) noexcept {}

    // The null value of an Option of a Ref, and its test.  Each one takes the
    // key that only the storage of an Option builds, and the storage gives
    // the key only to the niche that it calls.  A niche gets no friend, so
    // the niche of a Ref reaches the null value only through these two doors.
    // The rule specialization-home refuses a niche of a Ref outside this header.
    [[nodiscard]] friend constexpr Ref niche_null_(niche_key<Ref> const& /*key*/) noexcept { return Ref{NullTag{}}; }
    [[nodiscard]] friend constexpr bool niche_is_null_(niche_key<Ref> const& /*key*/, Ref const& ref) noexcept {
        return ref.object_ == nullptr;
    }

public:
    // A Ref of an object, with the key that only RefDoor builds.  A reference
    // is never null, so no key makes a null Ref.  The niche of a Ref states
    // that no door builds the null pointer, so an Option of a Ref has no null
    // test.  The constructor has no [[assume]] that the pointer is not null.
    // That condition reads a member through this, so GCC makes it an .ASSUME
    // call, and -fharden-conditional-branches then keeps a branch that the
    // optimizer removes with no assume, such as the null test of the decode
    // of an atomic word.
    constexpr Ref(T& object, detail::RefKey const& /*key*/) noexcept : object_{__builtin_addressof(object)} {}

    // Adds a const to the target, erases the brand, or does the two.
    template <class U, class UBrand>
        requires detail::RefWidens<U, UBrand, T, Brand>
    constexpr Ref(Ref<U, UBrand> other) noexcept : object_{__builtin_addressof(*other)} {}

    [[nodiscard]] constexpr T& operator*() const noexcept { return *object_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return object_; }
};

namespace detail {

// The owner gives the brand: the fresh brand of its own public call, which
// a refused pack keeps out of the hands of the caller.  The door is in
// namespace detail, so code outside the base cannot reach it, and to(),
// Box::view and get() of the Region family call it.
//
// A file outside this header can still write an explicit specialization of
// lend<B, T>.  That specialization is a member of RefDoor, so it builds the
// key and gives a Ref with each brand B, such as the brand of an arena, to an
// object that it can name.  It holds only the constructor from a reference,
// so it cannot make a null Ref.  A rule that refuses such a specialization
// outside its file is planned in specialization-home.
struct RefDoor {
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] static constexpr Ref<T, Brand> lend(T& object) noexcept {
        return Ref<T, Brand>{object, RefKey{}};
    }
};

}  // namespace detail

template <class... Refused, RefTarget T, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] constexpr Ref<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>> to(T& object) noexcept {
    return detail::RefDoor::lend<::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>>(object);
}

// True when the two Refs reach one object.  The two targets are one type,
// with or without a const, and the two brands can differ.
template <class First, class FirstBrand, class Second, class SecondBrand>
    requires std::same_as<std::remove_const_t<First>, std::remove_const_t<Second>>
[[nodiscard]] constexpr bool same_object(Ref<First, FirstBrand> first, Ref<Second, SecondBrand> second) noexcept {
    return __builtin_addressof(*first) == __builtin_addressof(*second);
}

namespace detail {

// The door of address_of, and the key of the constructor of an Address.
// AddressDoor is the one friend of the key, and only address_of calls the
// door.  The door and the key hold no template, so no explicit
// specialization of a template reaches the key.  The door is in namespace
// detail, and a use of that namespace outside the base is a finding.  Such
// a use can still build an Address from each integer.
struct AddressDoor;

class [[nodiscard]] AddressKey final {
    friend struct AddressDoor;

    constexpr AddressKey() noexcept {}
    constexpr AddressKey(AddressKey const&) noexcept {}

public:
    AddressKey& operator=(AddressKey const&) = delete("a key of an Address lives for one construction");
};

struct AddressDoor {
    [[nodiscard]] static constexpr AddressKey key() noexcept { return AddressKey{}; }
};

}  // namespace detail

// The address of one object, which address_of(ref) gives.  It answers an
// alignment query and compares with a different address.  An address
// changes from run to run, so it has no hash and no order, and no pointer
// comes back from it.  The seal makes the assignment user-provided, so
// std::bit_cast cannot build an Address from an integer.
class Address {
    std::uintptr_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The address of one object, with the key that only address_of takes.
    constexpr Address(std::uintptr_t value, detail::AddressKey const& /*key*/) noexcept : value_{value} {}

    // True when the address is a multiple of Alignment, a power of two.
    template <std::size_t Alignment>
        requires(Alignment != 0 && (Alignment & (Alignment - 1)) == 0)
    [[nodiscard]] constexpr bool is_aligned() const noexcept {
        return (value_ & (Alignment - 1)) == 0;
    }

    [[nodiscard]] friend constexpr bool operator==(Address const& left, Address const& right) noexcept {
        return left.value_ == right.value_;
    }
};

// The address of the object of a Ref.  The address of an object is no
// constant of the compile, so the function runs only at run time.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
[[nodiscard]] Address address_of(Ref<T, Brand> ref) noexcept {
    return Address{__builtin_bit_cast(std::uintptr_t, __builtin_addressof(*ref)), detail::AddressDoor::key()};
}

// The empty value of an Option of a Ref: the null pointer, which no Ref
// holds.  Option<Ref<T, Brand>> then has the size of one pointer.  The
// niche gives the key of its call to the hidden friends of Ref.
//
// No door of Ref builds the null pointer.  RefDoor builds a Ref from a
// reference, the widening constructor copies the address of a Ref, the seal
// refuses std::bit_cast and the checked lifetime start, and only the storage
// of an Option builds the key of niche_null_.  So the storage of a full
// Option trusts each Ref and has no test of the null pointer.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
struct niche<Ref<T, Brand>> {
    static constexpr bool is_empty_never_built = true;
    [[nodiscard]] static constexpr Ref<T, Brand> empty(niche_key<Ref<T, Brand>> const& key) noexcept {
        return niche_null_(key);
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<Ref<T, Brand>> const& key,
                                                 Ref<T, Brand> const& ref) noexcept {
        return niche_is_null_(key, ref);
    }
};

// The word of a Ref in an atomic cell (THE WORD OF A VALUE of Atomic.h): the
// pointer of its object.  No Ref holds the null pointer, so the null pointer
// is the empty word, and the cell of an Option of a Ref holds one pointer.
// Only a cell makes the key of from_word, and a cell holds only words that
// to_word gave.  So from_word lends the object of the word again, with the
// brand of the Ref, through the door of this file.  The door takes no key of
// the Atomic family.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
struct atomic_word<Ref<T, Brand>> {
    using word_type = T*;
    static constexpr word_type empty_word = nullptr;

    [[nodiscard]] static constexpr word_type to_word(Ref<T, Brand> const& ref) noexcept {
        return __builtin_addressof(*ref);
    }

    [[nodiscard]] static constexpr Ref<T, Brand> from_word(atomic_word_key<Ref<T, Brand>> const& /*key*/,
                                                           word_type word) noexcept {
        return detail::RefDoor::lend<Brand>(*word);
    }
};

// The object of a Box: a complete object type that is not an array, not
// cv-qualified and not abstract, with a destructor that does not throw.
template <class T>
concept BoxPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                  && !std::is_abstract_v<T> && std::is_nothrow_destructible_v<T>;

// The size and the alignment of a Hole: a size of at least one byte, and an
// alignment that is a power of two.
template <std::size_t Size, std::size_t Align>
concept HoleShape = Size > 0 && Align > 0 && (Align & (Align - 1)) == 0;

// Box takes a Hole in a constructor, and this header defines Hole after Box.
template <std::size_t Size, std::size_t Align>
    requires HoleShape<Size, Align>
class Hole;

template <class T, class Marker, class... Args>
concept CanMintBox = BoxPayload<T> && AllocationCapability<Marker> && std::constructible_from<T, Args&&...>;

namespace detail {

// The door of the two mints of a Hole, and the key of the constructors of a
// Hole.  HoleDoor is the one friend of the key.  The door and the key hold
// no template, so no explicit specialization of a template reaches the key.
// A use of namespace detail outside the base gets only a Hole of new
// storage, or a Hole of the storage of a Box that it gives up.
struct HoleDoor;

class [[nodiscard]] HoleKey final {
    friend struct HoleDoor;

    constexpr HoleKey() noexcept {}
    constexpr HoleKey(HoleKey const&) noexcept {}

public:
    HoleKey& operator=(HoleKey const&) = delete("a key of a Hole lives for one construction");
};

struct HoleDoor {
    [[nodiscard]] static constexpr HoleKey key() noexcept { return HoleKey{}; }
};

// The door of mint_box, and the key of the constructor of a Box.  BoxDoor is
// the one friend of the key, and only mint_box calls the door.  The door and
// the key hold no template, so no explicit specialization of a template
// reaches the key.  The constructor that takes the key allocates the storage
// and builds the object itself.  So a use of namespace detail outside the
// base gets only a Box of new storage, with no allocation capability at its
// call site.
struct BoxDoor;

class [[nodiscard]] BoxKey final {
    friend struct BoxDoor;

    constexpr BoxKey() noexcept {}
    constexpr BoxKey(BoxKey const&) noexcept {}

public:
    BoxKey& operator=(BoxKey const&) = delete("a key of a Box lives for one construction");
};

struct BoxDoor {
    [[nodiscard]] static constexpr BoxKey key() noexcept { return BoxKey{}; }
};

}  // namespace detail

template <BoxPayload T>
class [[nodiscard]] Box {
    T* object_ = nullptr;

    void destroy_() noexcept {
        if (object_ != nullptr) {
            object_->~T();
            detail::heap_release_(static_cast<void*>(object_), sizeof(T), alignof(T));
            object_ = nullptr;
        }
    }

    // A moved-from or consumed Box holds no object.  The use-after-move
    // guard refuses a use of it that it can see, and each build tests it
    // here.
    [[nodiscard]] T* held_() const noexcept {
        if (object_ == nullptr) [[unlikely]] {
            fatal("a borrow of a Box that holds no object: the Box was moved from or consumed");
        }
        return object_;
    }

    // The mint of a Hole from a Box calls vacate_ through the constructor of
    // the Hole.  Hole is a class template, so no unit outside its home can
    // specialize it.
    template <std::size_t Size, std::size_t Align>
        requires HoleShape<Size, Align>
    friend class Hole;

    // Ends the object and gives the storage to a Hole.  The Box then holds
    // no object.
    [[nodiscard]] void* vacate_() noexcept {
        T* const object = held_();
        object->~T();
        object_ = nullptr;
        return static_cast<void*>(object);
    }

public:
    // Takes the storage of the size and the alignment of T from the
    // allocation door, and builds the object from the arguments in it.  Only
    // mint_box holds the key.  An explicit specialization of this constructor
    // is a specialization of Box outside its home, and the rule
    // specialization-home refuses it in each build.
    template <class... Args>
        requires std::constructible_from<T, Args&&...>
    explicit Box(detail::BoxKey const& /*key*/, Args&&... args) noexcept
        : object_{::new(detail::heap_allocate_(sizeof(T), alignof(T))) T(static_cast<Args&&>(args)...)} {}

    // Builds the object from the arguments in the storage of a Hole of the
    // size and the alignment of T, with no call of the allocation door.  The
    // Hole is then empty, and a Hole that holds no storage ends the process
    // through fatal().  Only mint_box holds the key.
    template <class... Args>
        requires std::constructible_from<T, Args&&...>
    explicit Box(detail::BoxKey const& /*key*/, Hole<sizeof(T), alignof(T)>&& hole, Args&&... args) noexcept
        : object_{::new(std::move(hole).detach_()) T(static_cast<Args&&>(args)...)} {}

    Box(Box&& other) noexcept : object_{other.object_} { other.object_ = nullptr; }

    Box& operator=(Box&& other) noexcept {
        if (this != &other) {
            destroy_();
            object_ = other.object_;
            other.object_ = nullptr;
        }
        return *this;
    }

    Box(Box const&) = delete("a Box owns its object alone: a copy would free the object two times");
    Box& operator=(Box const&) = delete("a Box owns its object alone: a copy would free the object two times");

    ~Box() { destroy_(); }

    [[nodiscard]] T& operator*() & noexcept { return *held_(); }
    [[nodiscard]] T const& operator*() const& noexcept { return *held_(); }
    [[nodiscard]] T* operator->() & noexcept { return held_(); }
    [[nodiscard]] T const* operator->() const& noexcept { return held_(); }

    // The borrow of the whole object as a Ref with the fresh brand of the
    // call.  The Ref has no test, so a loop reads the object through it at
    // the cost of a raw pointer.  The pack refuses each explicit argument, so
    // no caller names the brand.  The result type names the brand through
    // undeducible_brand_t, so the address of view with a target type that
    // names a different brand deduces no brand, and it matches no overload.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>> view() & noexcept {
        return detail::RefDoor::lend<Fresh>(*held_());
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>> view() const& noexcept {
        T const& object = *held_();
        return detail::RefDoor::lend<Fresh>(object);
    }

    // Moves the object out, destroys the moved-from object, and frees the
    // storage.  The Box then holds no object.
    [[nodiscard]] T consume() && noexcept
        requires std::is_nothrow_move_constructible_v<T>
    {
        if (object_ == nullptr) [[unlikely]] {
            fatal("consume of a Box that holds no object: the Box was moved from or consumed");
        }
        T taken(static_cast<T&&>(*object_));
        destroy_();
        return taken;
    }

    void view() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    void view() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T& operator*() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const& operator*() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T* operator->() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const* operator->() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
};

// The storage has the size and the alignment of T, and the destructor of
// the Box gives it back with the same two values.  The door never gives a
// null pointer.  An argument list that does not build a T, and a tag that
// is not the allocation capability, fail the gate at the call.
template <class T, class Marker, class... Args>
    requires CanMintBox<T, Marker, Args...>
[[nodiscard]] Box<T> mint_box(Marker /*allocation*/, Args&&... args) noexcept {
    return Box<T>{detail::BoxDoor::key(), static_cast<Args&&>(args)...};
}

// Size bytes of heap storage at the alignment Align, with no object in it.
// The annotation no_start_over_bytes refuses a Hole to the checked lifetime
// start, so no bytes become a Hole that frees storage that it does not own.
template <std::size_t Size, std::size_t Align>
    requires HoleShape<Size, Align>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Hole {
    void* storage_ = nullptr;

    template <BoxPayload T>
    friend class Box;

    // The storage of a Hole that a path spends.  A moved-from or spent Hole
    // holds none, and each build tests it here.
    [[nodiscard]] void* held_() const noexcept {
        if (storage_ == nullptr) [[unlikely]] {
            fatal("a spend of a Hole that holds no storage: the Hole was moved from or spent");
        }
        return storage_;
    }

    // Gives the storage to a Box, which builds its object in it.  The Hole
    // is then empty.
    [[nodiscard]][[= ::foundation::linear::detach_door{}]] void* detach_() && noexcept {
        void* const storage = held_();
        storage_ = nullptr;
        return storage;
    }

public:
    // Takes Size bytes at the alignment Align from the allocation door.
    // Only mint_hole holds the key.
    explicit Hole(detail::HoleKey const& /*key*/) noexcept : storage_{detail::heap_allocate_(Size, Align)} {}

    // Ends the object of a Box of the exact size and alignment, and takes
    // its storage.  The Box is then empty.  Only mint_hole holds the key.
    template <BoxPayload T>
        requires(sizeof(T) == Size && alignof(T) == Align)
    explicit Hole(detail::HoleKey const& /*key*/, Box<T>&& box) noexcept : storage_{box.vacate_()} {}

    Hole(Hole&& other) noexcept : storage_{other.storage_} { other.storage_ = nullptr; }

    Hole& operator=(Hole&& other) noexcept {
        if (this != &other) {
            if (storage_ != nullptr) {
                detail::heap_release_(storage_, Size, Align);
            }
            storage_ = other.storage_;
            other.storage_ = nullptr;
        }
        return *this;
    }

    Hole(Hole const&) = delete("a Hole owns its storage alone: a copy would free the storage two times");
    Hole& operator=(Hole const&) = delete("a Hole owns its storage alone: a copy would free the storage two times");

    ~Hole() {
        if (storage_ != nullptr) {
            detail::heap_release_(storage_, Size, Align);
        }
    }

    // Gives the storage back to the allocation door.  The Hole is then
    // empty.
    [[= ::foundation::linear::drop_door{}]] void consume() && noexcept {
        detail::heap_release_(held_(), Size, Align);
        storage_ = nullptr;
    }
};

// The gate of the mint of a Hole from the allocation capability.
template <std::size_t Size, std::size_t Align, class Marker>
concept CanMintHole = HoleShape<Size, Align> && AllocationCapability<Marker>;

// A Hole of new storage from the allocation door.  The door never gives a
// null pointer.
template <std::size_t Size, std::size_t Align, class Marker>
    requires CanMintHole<Size, Align, Marker>
[[nodiscard]] Hole<Size, Align> mint_hole(Marker /*allocation*/) noexcept {
    return Hole<Size, Align>{detail::HoleDoor::key()};
}

// A Hole of the storage of a Box that the caller gives up.  The object of
// the Box ends, and its storage stays with no call of the door.  A Box that
// holds no object ends the process through fatal().
template <BoxPayload T>
[[nodiscard]] Hole<sizeof(T), alignof(T)> mint_hole(Box<T>&& box) noexcept {
    return Hole<sizeof(T), alignof(T)>{detail::HoleDoor::key(), std::move(box)};
}

// The gate of the mint of a Box in a Hole: the Hole has the exact size and
// alignment of T, so the Box gives back the storage that the Hole holds.
template <class T, std::size_t Size, std::size_t Align, class... Args>
concept CanMintBoxInHole =
    BoxPayload<T> && Size == sizeof(T) && Align == alignof(T) && std::constructible_from<T, Args&&...>;

// A Box of the object that the arguments build in the storage of a Hole
// that the caller gives up.  The mint calls no allocation door, so it takes
// no allocation capability: the mint of the Hole took it.
template <class T, std::size_t Size, std::size_t Align, class... Args>
    requires CanMintBoxInHole<T, Size, Align, Args...>
[[nodiscard]] Box<T> mint_box(Hole<Size, Align>&& hole, Args&&... args) noexcept {
    return Box<T>{detail::BoxDoor::key(), std::move(hole), static_cast<Args&&>(args)...};
}

}  // namespace foundation::core
