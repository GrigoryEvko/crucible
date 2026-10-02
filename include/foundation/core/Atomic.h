#pragma once

// The Atomic family: one cell that two threads touch, with the memory
// order in the name of each operation.
//
// Atomic<T> holds one integral or enum value whose atomic operations do
// not take a lock on this target.  Each operation is one __atomic builtin
// on the plain value, so it compiles to the instruction of the same
// std::atomic operation.  The order is in the name: load_acquire,
// store_release, fetch_add_acq_rel, cas_acq_rel.  The type has no
// operator and no conversion, so no operation takes the sequentially
// consistent order without a name that says so, and no operation takes the
// relaxed order at all.
//
// store_release_sole_writer is for the one thread that writes the cell,
// such as the producer of a single-producer ring.  That thread keeps its
// own copy of the stored value, and it gives the copy and the next value.
// A Debug build reads the cell and refuses a copy that does not agree,
// which is how a second writer shows.
//
// CacheLine<T> aligns a cell to 64 bytes and pads it to a multiple of 64
// bytes, so the type states that no other data shares its cache line.
//
// Tally is a statistics counter.  add() is a fetch-add with acq_rel order,
// which is the same locked instruction on x86-64, and read() is an acquire
// load.  A value of a Tally orders no other data.

#include <foundation/Pinned.h>
#include <foundation/Platform.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The value of an Atomic: an integral or enum type, not cv-qualified, whose
// atomic operations are lock-free for each object of its size.
template <class T>
concept AtomicValue = (std::integral<T> || std::is_enum_v<T>)
                   && !std::is_const_v<T> && !std::is_volatile_v<T> && __atomic_always_lock_free(sizeof(T), 0);

// An integral value that a fetch-add can step.  A bool or an enum has no
// meaning for an addition.
template <class T>
concept AtomicCount = AtomicValue<T> && std::integral<T> && !std::same_as<T, bool>;

// The result of a compare-and-swap: the value that the cell held before
// the operation, and whether the operation stored the desired value.
template <class T>
struct CasOutcome {
    T observed;
    bool is_swapped;
};

template <AtomicValue T>
class Atomic : ::foundation::Pinned<Atomic<T>> {
    alignas(sizeof(T)) T value_{};

public:
    using value_type = T;

    constexpr Atomic() noexcept = default;
    explicit constexpr Atomic(T initial) noexcept : value_{initial} {}

    [[nodiscard]] T load_acquire() const noexcept { return __atomic_load_n(&value_, __ATOMIC_ACQUIRE); }

    void store_release(T value) noexcept { __atomic_store_n(&value_, value, __ATOMIC_RELEASE); }

    // The sole writer stores next.  current is the value that the cell
    // holds, from the own copy of the writer.  The relaxed load of the
    // Debug check is sound only because the caller wrote that value.
    void store_release_sole_writer(T current, T next) noexcept {
        CRUCIBLE_DEBUG_ASSERT(__atomic_load_n(&value_, __ATOMIC_RELAXED) == current);
        static_cast<void>(current);
        __atomic_store_n(&value_, next, __ATOMIC_RELEASE);
    }

    // Unsigned arithmetic wraps, and signed arithmetic of an atomic
    // operation wraps too.
    [[nodiscard]] T fetch_add_acq_rel(T delta) noexcept
        requires AtomicCount<T>
    {
        return __atomic_fetch_add(&value_, delta, __ATOMIC_ACQ_REL);
    }

    // A strong compare-and-swap: it fails only when the cell holds a value
    // other than expected.  A failure reads the cell with acquire order.
    [[nodiscard]] CasOutcome<T> cas_acq_rel(T expected, T desired) noexcept {
        bool const is_swapped =
            __atomic_compare_exchange_n(&value_, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        return CasOutcome<T>{expected, is_swapped};
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
