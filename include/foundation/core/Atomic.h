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
// sequentially consistent order without a name that says so, and no
// operation takes the relaxed order at all.
//
// A value of a class type is admitted when it has one bit pattern for each
// value.  The cell holds it as the unsigned integer of its size, and the
// compare of a compare-and-swap then compares the bits of the value.  The
// bits of a value with no padding are the value, so the compare agrees with
// a compare of the members.
//
// The one thread that writes a cell, such as the producer of a
// single-producer ring, keeps its own copy of the value and publishes each
// change with store_release.  A relaxed read of its own cell needs a proof
// that the thread is the sole writer, so the family gives no relaxed read.
//
// CacheLine<T> aligns a cell to 64 bytes and pads it to a multiple of 64
// bytes, so the type states that no other data shares its cache line.
//
// Tally is a statistics counter.  add() is a fetch-add with acq_rel order,
// which is the same locked instruction on x86-64, and read() is an acquire
// load.  A value of a Tally orders no other data.

#include <foundation/Pinned.h>
#include <foundation/core/Choice.h>

#include <concepts>
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

}  // namespace detail

template <AtomicValue T>
class Atomic : ::foundation::Pinned<Atomic<T>> {
    using storage_type = detail::atomic_storage_t<T>;
    static_assert(sizeof(storage_type) == sizeof(T), "a lock-free value has the size of an unsigned integer");

    alignas(sizeof(T)) storage_type value_{};

    [[nodiscard]] static constexpr storage_type to_storage_(T value) noexcept {
        if constexpr (std::same_as<storage_type, T>) {
            return value;
        } else {
            return __builtin_bit_cast(storage_type, value);
        }
    }

    [[nodiscard]] static constexpr T from_storage_(storage_type bits) noexcept {
        if constexpr (std::same_as<storage_type, T>) {
            return bits;
        } else {
            return __builtin_bit_cast(T, bits);
        }
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

// A cell alone on its cache lines.  The platform floor of
// foundation/Platform.h holds the line at 64 bytes.
template <class T>
concept CacheLineCell = std::is_object_v<T> && !std::is_array_v<T>;

template <CacheLineCell T>
class alignas(64) CacheLine {
    T cell_;

public:
    template <class... Args>
        requires std::constructible_from<T, Args&&...>
    explicit constexpr CacheLine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>)
        : cell_(static_cast<Args&&>(args)...) {}

    [[nodiscard]] constexpr T& get() & noexcept { return cell_; }
    [[nodiscard]] constexpr T const& get() const& noexcept { return cell_; }

    T& get() && = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T const& get() const&& = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
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
