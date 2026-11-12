#pragma once

// The Atomic family: one cell that two threads touch, with the memory
// order in the name of each operation.
//
// Atomic<T> holds one value whose atomic operations do not take a lock on
// this target.  Each operation uses the __atomic builtins on the plain
// value, so it compiles to the instructions of the same std::atomic
// operation.  The order is in the name:
//
//   load_acquire()                 the value, with acquire order
//   store_release(v)               stores v, with release order
//   exchange_acq_rel(v)            stores v, and gives the value before
//   cas_acq_rel(e, d)              stores d if the cell holds e, strong, and
//                                  gives a Result with the value it read
//   fetch_add_acq_rel(d)           adds d, for an integer
//   fetch_sub_acq_rel(d)           subtracts d, for an integer
//   fetch_or_acq_rel(b)            sets the bits b, for an unsigned integer
//   fetch_and_acq_rel(b)           keeps only the bits b, for an unsigned integer
//   fetch_max_acq_rel(v)           stores v if v is larger, for an integer
//   fetch_min_acq_rel(v)           stores v if v is smaller, for an integer
//
// The type has no operator and no conversion, so no operation takes the
// sequentially consistent order without a name that says so.  Only the
// writer of a cell of one writer takes the relaxed order (below).
//
// A value of a class type is admitted when it has one bit pattern for each
// value.  The cell holds it as the unsigned integer of its size, and the
// compare of a compare-and-swap then compares the bits of the value.  The
// bits of a value with no padding are the value, so the compare agrees with
// a compare of the members.
//
// THE CELL OF ONE WRITER
//   Atomic<T, SoleWriter<Role, Brand>> names its writer in its type, and
//   each write takes the proof of that writer:
//
//     load_acquire()             the value, with acquire order, on each thread
//     load_owned(writer)         the value, with relaxed order, for the writer
//     store_release(writer, v)   stores v, with release order, for the writer
//
//   The holder of the proof is the only thread that writes the cell, so a
//   read by the writer reads a value that the same thread stored.  That read
//   needs no order.  A proof moves to a different thread only through a
//   handoff that orders it, such as a thread start, a join or a channel of
//   the base.  The last store of the old holder then happens before the
//   first read of the new holder.  The cell has no read-modify-write
//   operation: the writer reads with load_owned and stores with
//   store_release, with no locked instruction.
//
//   SoleWriter<Role, Brand> is the proof, a linear token with no bytes.
//   mint_sole_writer<Role>() is its one door, and each call site of the
//   door gives a fresh brand.  The identity is the call site: a mint in a
//   loop gives one type in each turn, and a token that leaves its turn can
//   write the cell of a different turn.  The run-time id of a brand of the
//   linear design (misc/12_10_2026_linear.md, phase B) closes that case.
//
// CacheLine<T> aligns a cell to 64 bytes and pads it to a multiple of 64
// bytes, so the type states that no other data shares its cache line.
// *line and line->member borrow the cell.
//
// Tally is a statistics counter.  add() is a fetch-add with acq_rel order,
// which is the same locked instruction on x86-64, and read() is an acquire
// load.  A value of a Tally orders no other data.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/core/Choice.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The value of an Atomic: a trivially copyable object type, not an array and
// not cv-qualified, with one bit pattern for each value, whose atomic
// operations are lock-free for each object of its size.  The one bit
// pattern makes a compare-and-swap compare values: a type with padding, or a
// floating type with two zeros, is refused.
template <class T>
concept AtomicValue = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                   && std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>
                   && __atomic_always_lock_free(sizeof(T), 0);

// An integral value that an addition, a subtraction, a maximum or a minimum
// can change.  A bool or an enum has no meaning for these operations.
template <class T>
concept AtomicCount = AtomicValue<T> && std::integral<T> && !std::same_as<T, bool>;

// An unsigned integral value that a bitwise operation can change.
template <class T>
concept AtomicBits = AtomicCount<T> && std::unsigned_integral<T>;

// The error of a compare-and-swap that stored nothing: the value that the
// cell held, which was not the expected value.
template <class T>
struct CasRefusal final {
    T observed;
};

namespace detail {

// The word that holds a value of an Atomic.  An integer, an enum and a
// pointer go to the builtins as they are.  Each other value goes as the
// unsigned integer of its size, because the builtins take only those.
template <class T>
using atomic_storage_t = std::conditional_t<
    std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>, T,
    std::conditional_t<sizeof(T) == 1, std::uint8_t,
                       std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                          std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>>;

// The word of a value, and the value of a word.  A value of one bit pattern
// for each value round-trips through its word.
template <class T>
[[nodiscard]] constexpr atomic_storage_t<T> to_atomic_storage(T value) noexcept {
    if constexpr (std::same_as<atomic_storage_t<T>, T>) {
        return value;
    } else {
        return __builtin_bit_cast(atomic_storage_t<T>, value);
    }
}

template <class T>
[[nodiscard]] constexpr T from_atomic_storage(atomic_storage_t<T> bits) noexcept {
    if constexpr (std::same_as<atomic_storage_t<T>, T>) {
        return bits;
    } else {
        return __builtin_bit_cast(T, bits);
    }
}

// The role of a sole writer: an empty class that is not cv-qualified.  A role
// names a kind of cell, such as the head of a ring.
template <class Role>
concept SoleWriterRole =
    std::is_class_v<Role> && std::is_empty_v<Role> && !std::is_const_v<Role> && !std::is_volatile_v<Role>;

// The template arguments of mint_sole_writer: a role and no other argument.
// A second argument would name the brand of a writer that the caller holds.
template <class Role, class... Arguments>
concept SoleWriterMint = SoleWriterRole<Role> && sizeof...(Arguments) == 0;

}  // namespace detail

template <class Role, class Brand>
class SoleWriter;

// The one door to a sole writer.  The fresh brand is the last template
// parameter, after a pack, so a caller can name the role and nothing else.
// An explicit second argument goes into the pack, and the constraint refuses
// it.
template <class Role, class... Arguments, class Brand = CRUCIBLE_FRESH_BRAND>
    requires detail::SoleWriterMint<Role, Arguments...>
[[nodiscard]] constexpr SoleWriter<Role, Brand> mint_sole_writer() noexcept;

// The key of the constructor of a sole writer.  Only mint_sole_writer makes
// one.  The two constructors are user-provided, so std::bit_cast and a
// lifetime start over bytes build no key.  The key stays in this namespace,
// because a friend declaration in a nested namespace would declare a
// different mint there.
class sole_writer_key final {
    constexpr sole_writer_key() noexcept {}

public:
    constexpr sole_writer_key(sole_writer_key const&) noexcept {}

private:
    template <class Role, class... Arguments, class Brand>
        requires detail::SoleWriterMint<Role, Arguments...>
    friend constexpr SoleWriter<Role, Brand> mint_sole_writer() noexcept;
};

// The proof of the one thread that writes a cell (THE CELL OF ONE WRITER).
// The token has no bytes.  Its move constructor and its destructor are
// trivial, so the ABI passes it in a register.  Its move assignment is
// user-provided, so it is not trivially copyable, and std::bit_cast builds
// no token.  The annotation no_start_over_bytes refuses the checked lifetime
// start of foundation/Lifetime.h.
template <class Role, class Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]] SoleWriter final {
    static_assert(detail::SoleWriterRole<Role>, "SoleWriter<Role, Brand>: the role names a kind of cell, such as the "
                                                "head of a ring.  It must be an empty class that is not cv-qualified.");
    static_assert(::foundation::brand::IsFreshBrand<Brand>,
                  "SoleWriter<Role, Brand>: the brand is the identity of one writer.  DefaultBrand would give each "
                  "writer of the role one type, so the writer of one cell could write a different cell.  Mint the "
                  "writer with mint_sole_writer<Role>().");

public:
    using brand_type = Brand;

    explicit constexpr SoleWriter(sole_writer_key) noexcept {}

    SoleWriter(SoleWriter const&) = delete("SoleWriter: a copy gives the cell a second writer, and load_owned then "
                                           "reads a value that a different thread stores.  Move the writer.");
    SoleWriter& operator=(SoleWriter const&) = delete("SoleWriter: a copy gives the cell a second writer.");

    constexpr SoleWriter(SoleWriter&&) noexcept = default;
    constexpr SoleWriter& operator=(SoleWriter&&) noexcept { return *this; }
    ~SoleWriter() = default;

private:
    // A writer lives in a scope, a handle or a parameter.  A new-expression
    // gives it storage that no scope ends, and each copy of the pointer
    // reaches the one token.  The class operator new hides each global form,
    // and it is private, so it is no part of the public surface.
    static void* operator new(std::size_t) = delete("a SoleWriter lives in a scope or a handle, not on the heap");
    static void* operator new[](std::size_t) = delete("a SoleWriter lives in a scope or a handle, not on the heap");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
};

template <class Role, class... Arguments, class Brand>
    requires detail::SoleWriterMint<Role, Arguments...>
[[nodiscard]] constexpr SoleWriter<Role, Brand> mint_sole_writer() noexcept {
    return SoleWriter<Role, Brand>{sole_writer_key{}};
}

// The writer of a cell: void for a cell that each thread can write, or a
// SoleWriter for a cell of one writer.
template <AtomicValue T, class Writer = void>
class Atomic : ::foundation::Pinned<Atomic<T, Writer>> {
    static_assert(std::is_void_v<Writer>, "Atomic<T, Writer>: the writer is void, for a cell that each thread can "
                                          "write, or SoleWriter<Role, Brand>, for a cell of one writer.");

    using storage_type = detail::atomic_storage_t<T>;
    static_assert(sizeof(storage_type) == sizeof(T), "a lock-free value has the size of an unsigned integer");

    alignas(sizeof(T)) storage_type value_{};

    [[nodiscard]] static constexpr storage_type to_storage_(T value) noexcept {
        return detail::to_atomic_storage<T>(value);
    }

    [[nodiscard]] static constexpr T from_storage_(storage_type bits) noexcept {
        return detail::from_atomic_storage<T>(bits);
    }

public:
    using value_type = T;

    constexpr Atomic() noexcept
        requires std::is_nothrow_default_constructible_v<T>
        : value_{to_storage_(T{})} {}

    explicit constexpr Atomic(T initial) noexcept : value_{to_storage_(initial)} {}

    [[nodiscard]] T load_acquire() const noexcept { return from_storage_(__atomic_load_n(&value_, __ATOMIC_ACQUIRE)); }

    void store_release(T value) noexcept { __atomic_store_n(&value_, to_storage_(value), __ATOMIC_RELEASE); }

    [[nodiscard]] T exchange_acq_rel(T value) noexcept {
        return from_storage_(__atomic_exchange_n(&value_, to_storage_(value), __ATOMIC_ACQ_REL));
    }

    // A strong compare-and-swap: it fails only when the cell holds a value
    // other than expected.  A failure reads the cell with acquire order, and
    // its error holds the value that it read.
    [[nodiscard]] Result<Unit, CasRefusal<T>> cas_acq_rel(T expected, T desired) noexcept {
        storage_type observed = to_storage_(expected);
        if (__atomic_compare_exchange_n(&value_, &observed, to_storage_(desired), false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return Unit{};
        }
        return err(CasRefusal<T>{from_storage_(observed)});
    }

    // Unsigned arithmetic wraps, and signed arithmetic of an atomic
    // operation wraps too.
    [[nodiscard]] T fetch_add_acq_rel(T delta) noexcept
        requires AtomicCount<T>
    {
        return __atomic_fetch_add(&value_, delta, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_sub_acq_rel(T delta) noexcept
        requires AtomicCount<T>
    {
        return __atomic_fetch_sub(&value_, delta, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_or_acq_rel(T bits) noexcept
        requires AtomicBits<T>
    {
        return __atomic_fetch_or(&value_, bits, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_and_acq_rel(T bits) noexcept
        requires AtomicBits<T>
    {
        return __atomic_fetch_and(&value_, bits, __ATOMIC_ACQ_REL);
    }

    // GCC has no builtin for a maximum or a minimum, so each one is a loop
    // of compare-and-swap operations, as the fallback of std::atomic is.
    // A value that does not change the cell writes nothing, and the
    // operation is then an acquire load.
    [[nodiscard]] T fetch_max_acq_rel(T value) noexcept
        requires AtomicCount<T>
    {
        T observed = __atomic_load_n(&value_, __ATOMIC_ACQUIRE);
        while (observed < value
               && !__atomic_compare_exchange_n(&value_, &observed, value, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
        return observed;
    }

    [[nodiscard]] T fetch_min_acq_rel(T value) noexcept
        requires AtomicCount<T>
    {
        T observed = __atomic_load_n(&value_, __ATOMIC_ACQUIRE);
        while (value < observed
               && !__atomic_compare_exchange_n(&value_, &observed, value, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
        return observed;
    }
};

// The cell of one writer (THE CELL OF ONE WRITER).  The parameter of the
// brand has the name Identity, so the rule erased-brand reads no brand of the
// primary template here.
template <AtomicValue T, class Role, class Identity>
class Atomic<T, SoleWriter<Role, Identity>> : ::foundation::Pinned<Atomic<T, SoleWriter<Role, Identity>>> {
    static_assert(detail::SoleWriterRole<Role>, "Atomic<T, SoleWriter<Role, Brand>>: the role names a kind of cell.  "
                                                "It must be an empty class that is not cv-qualified.");
    static_assert(::foundation::brand::IsFreshBrand<Identity>,
                  "Atomic<T, SoleWriter<Role, Brand>>: the brand is the identity of one writer.  DefaultBrand would "
                  "give each writer of the role one type, so the writer of one cell could write a different cell.");

    using storage_type = detail::atomic_storage_t<T>;
    static_assert(sizeof(storage_type) == sizeof(T), "a lock-free value has the size of an unsigned integer");

    alignas(sizeof(T)) storage_type value_{};

public:
    using value_type = T;

    constexpr Atomic() noexcept
        requires std::is_nothrow_default_constructible_v<T>
        : value_{detail::to_atomic_storage<T>(T{})} {}

    explicit constexpr Atomic(T initial) noexcept : value_{detail::to_atomic_storage<T>(initial)} {}

    // A read on each thread, with acquire order.  It pairs with each
    // store_release of the writer.
    [[nodiscard]] T load_acquire() const noexcept {
        return detail::from_atomic_storage<T>(__atomic_load_n(&value_, __ATOMIC_ACQUIRE));
    }

    // The read of the writer.  Each value of the cell is a value that the
    // holder of the proof stored, so the read needs no order.
    [[nodiscard]] T load_owned(SoleWriter<Role, Identity> const&) const noexcept {
        return detail::from_atomic_storage<T>(__atomic_load_n(&value_, __ATOMIC_RELAXED));
    }

    void store_release(SoleWriter<Role, Identity> const&, T value) noexcept {
        __atomic_store_n(&value_, detail::to_atomic_storage<T>(value), __ATOMIC_RELEASE);
    }
};

// A cell alone on its cache lines.  The platform floor of
// foundation/Platform.h holds the line at 64 bytes.
template <class T>
concept CacheLineCell = std::is_object_v<T> && !std::is_array_v<T>;

// A CacheLine always holds its cell, so *line and line->member are total.
// A borrow of a temporary line dangles at the end of the full expression,
// so the rvalue forms are deleted.
template <CacheLineCell T>
class alignas(64) CacheLine {
    T cell_;

public:
    template <class... Args>
        requires std::constructible_from<T, Args&&...>
    explicit constexpr CacheLine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>)
        : cell_(static_cast<Args&&>(args)...) {}

    [[nodiscard]] constexpr T& operator*() & noexcept { return cell_; }
    [[nodiscard]] constexpr T const& operator*() const& noexcept { return cell_; }
    [[nodiscard]] constexpr T* operator->() & noexcept { return __builtin_addressof(cell_); }
    [[nodiscard]] constexpr T const* operator->() const& noexcept { return __builtin_addressof(cell_); }

    T& operator*() && = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T const&
    operator*() const&& = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T* operator->() && = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T const*
    operator->() const&& = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
};

class Tally : ::foundation::Pinned<Tally> {
    alignas(8) std::uint64_t count_ = 0;

public:
    constexpr Tally() noexcept = default;

    void add(std::uint64_t amount) noexcept {
        static_cast<void>(__atomic_fetch_add(&count_, amount, __ATOMIC_ACQ_REL));
    }

    [[nodiscard]] std::uint64_t read() const noexcept { return __atomic_load_n(&count_, __ATOMIC_ACQUIRE); }
};

}  // namespace foundation::core
