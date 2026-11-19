#pragma once

// The cell of 128 bits: one value of 16 bytes that two threads touch, with
// the memory order in the name of each operation.
//
// THE VALUE
//   A value of 16 bytes with an alignment of at most 16, trivially copyable,
//   with one bit pattern for each value, such as a pointer with a count of
//   its generation.  The cell holds the bits of the value as one unsigned
//   integer of 128 bits, so a compare-and-swap compares the bits of the value.
//
// THE OPERATIONS
//   load_acquire()             the value, with acquire order
//   store_release(v)           stores v, with release order
//   cas_acq_rel(e, d)          stores d if the cell holds e, strong, and gives
//                              a Result with the value it read
//   The names and the Result are those of the cell of foundation/core/Atomic.h.
//
// THE TWO FORMS
//   WideAtomicByInstruction<T> uses the instructions of the core, and it has
//   16 bytes.  Its compare-and-swap is a full barrier:
//     x86-64 v2 and above      lock cmpxchg16b, in line (THE SWAP ON X86-64)
//     aarch64 with LSE         caspal, in line, through the builtin
//                              __sync_val_compare_and_swap
//     aarch64 with no LSE      the helper __aarch64_cas16_sync of libgcc,
//                              through the same builtin, which picks caspal
//                              or a loop of ldaxp and stlxp at run time
//   The type exists only when the target has a compare-and-swap of 16 bytes
//   (__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16).  x86-64 v1 does not have one.
//
//   WideAtomicByLock<T> holds the value and a lock word, and it has 32
//   bytes.  Each operation takes the lock with an exchange, spins with
//   CRUCIBLE_SPIN_PAUSE while a different thread holds it, and gives the same
//   results as the instruction form.  It compiles on each target, so a test
//   compares the two forms in each build.
//
//   WideAtomic<T> is the instruction form when the target has it, else the
//   lock form.  So a WideAtomic has 16 bytes at x86-64 v2 and above and on
//   aarch64, and 32 bytes at x86-64 v1.  The engine builds one library for
//   each level, and a cell is Pinned and never goes into a file, so no cell
//   crosses two levels.
//
// THE LOAD AND THE STORE ON X86-64
//   GCC calls libatomic for each __atomic operation of 16 bytes, so the cell
//   does not use them.  Intel and AMD state that an aligned load or store of
//   16 bytes through MOVDQA or VMOVDQA is one access on a core with AVX.
//   libatomic of GCC 16 reads that fact at run time
//   (libatomic/config/x86/init.c): the bits AVX and CMPXCHG16B of CPUID leaf
//   1, and a vendor of Intel, AMD, Zhaoxin or Centaur above family 6.  The
//   start-up hook of src/foundation/os/WideAtomic.cpp reads the same facts one
//   time.  With the facts, a load is one MOVDQA, and a store is one MOVDQA
//   with no fence, because each store of x86-64 has release order.  A unit
//   with AVX takes the VEX form VMOVDQA, as each other vector move of the
//   unit does.  With no facts, and before the hook, a load is a
//   compare-and-swap of zero with zero, and a store is a loop of
//   compare-and-swap operations.  A load through a compare-and-swap writes
//   the cache line, so two cores that read one cell take turns to own the
//   line.
//
//   On aarch64 the load and the store are the same compare-and-swap forms.
//   LSE2 makes an aligned LDP one access, but no aarch64 host here can measure
//   that form, so the cell does not use it.
//
// THE SWAP ON X86-64
//   The builtin __sync_val_compare_and_swap gives the old word, and a
//   compare of that word with the expected word then tells the result.  On
//   the dev host (AMD EPYC 9754, a guest of KVM), that compare made a swap
//   that stores 9% slower than the swap of libatomic.  So the cell writes lock
//   cmpxchg16b in line and reads the result from its zero flag.  The old word
//   comes back in RDX:RAX for a refusal.  In bench_wide_atomic on that host,
//   that form stores in 729 and refuses in 700 thousandths of the time of
//   libatomic.
//
// THE SANITIZER
//   ThreadSanitizer sees no order through inline assembly.  Under
//   -fsanitize=thread the load, the store and the compare-and-swap take the
//   builtin forms, which ThreadSanitizer reads as operations of
//   __tsan_atomic128.
//
// The cell has no wait.  A thread waits for a change of a cell through the
// event count of fixy/concurrent/EventCount.h (THE WAIT of
// foundation/core/Atomic.h).

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>

#include <cstdint>
#include <type_traits>

namespace foundation::os {

namespace detail {

__extension__ using WideWord = unsigned __int128;

#if defined(__x86_64__)
// True when an aligned MOVDQA of 16 bytes is one access on the cores of this
// host (THE LOAD AND THE STORE ON X86-64).  The start-up hook of
// src/foundation/os/WideAtomic.cpp writes it one time, at the load of its
// library.  Before the hook it is false, which takes the compare-and-swap
// forms.
extern bool has_atomic_vector_moves_;
#endif

}  // namespace detail

#if defined(__x86_64__)
// True when an aligned vector move of 16 bytes is one access on the cores of
// this host, so the load and the store of a WideAtomicByInstruction outside
// ThreadSanitizer take one vector move.  False before the start-up hook.
[[nodiscard]] inline bool has_atomic_vector_moves() noexcept { return detail::has_atomic_vector_moves_; }
#endif

// The value of a cell of 128 bits (THE VALUE).
template <class T>
concept WideAtomicValue =
    std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && sizeof(T) == 16
    && alignof(T) <= 16 && std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>;

#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)

// The cell of 128 bits through the instructions of the core (THE TWO FORMS).
// Each step of a cell is atomic, so two threads read and write one cell at the
// same time.  A value that one thread stores, another loads, so the cell is a
// cell of its payload (foundation/Transfer.h).
template <WideAtomicValue T>
class[[= ::foundation::transfer::payload_cell{}]] WideAtomicByInstruction
    : ::foundation::Pinned<WideAtomicByInstruction<T>> {
    // A load through a compare-and-swap writes the value that the cell holds
    // back to the cell, so a const cell writes its word.  The word is mutable
    // for that write, and a const cell then never lives in memory that is
    // read only.
    alignas(16) mutable detail::WideWord word_ = 0;

    [[nodiscard]] static constexpr detail::WideWord to_word_(T const& value) noexcept {
        return __builtin_bit_cast(detail::WideWord, value);
    }

    [[nodiscard]] static constexpr T from_word_(detail::WideWord word) noexcept { return __builtin_bit_cast(T, word); }

    // A compare-and-swap of 16 bytes with a full barrier, in the form of
    // __atomic_compare_exchange_n.  It stores DESIRED when the cell holds
    // HELD, and it returns true.  Else it puts the word of the cell into HELD
    // and returns false.  On x86-64 outside ThreadSanitizer the swap is the
    // instruction, and the result comes from its zero flag (THE SWAP ON
    // X86-64).
    [[nodiscard]] bool swap_word_(detail::WideWord& held, detail::WideWord desired) const noexcept {
#if defined(__x86_64__) && !defined(__SANITIZE_THREAD__)
        auto low = static_cast<std::uint64_t>(held);
        auto high = static_cast<std::uint64_t>(held >> 64);
        bool is_stored = false;
        asm volatile("lock cmpxchg16b %1"
                     : "=@ccz"(is_stored), "+m"(word_), "+a"(low), "+d"(high)
                     : "b"(static_cast<std::uint64_t>(desired)), "c"(static_cast<std::uint64_t>(desired >> 64))
                     : "memory");
        held = (detail::WideWord{high} << 64) | low;
        return is_stored;
#else
        detail::WideWord const expected = held;
        held = __sync_val_compare_and_swap(&word_, expected, desired);
        return held == expected;
#endif
    }

public:
    using value_type = T;

    constexpr WideAtomicByInstruction() noexcept
        requires std::is_nothrow_default_constructible_v<T>
        : word_{to_word_(T{})} {}

    explicit constexpr WideAtomicByInstruction(T initial) noexcept : word_{to_word_(initial)} {}

    // The vector move is only for a unit that is not under ThreadSanitizer
    // (THE SANITIZER).
    [[nodiscard]] T load_acquire() const noexcept {
#if defined(__x86_64__) && !defined(__SANITIZE_THREAD__)
        if (detail::has_atomic_vector_moves_) [[likely]] {
            detail::WideWord word = 0;
#if defined(__AVX__)
            asm volatile("vmovdqa\t{%1, %0|%0, %1}" : "=x"(word) : "m"(word_) : "memory");
#else
            asm volatile("movdqa\t{%1, %0|%0, %1}" : "=x"(word) : "m"(word_) : "memory");
#endif
            return from_word_(word);
        }
#endif
        detail::WideWord held = 0;
        static_cast<void>(swap_word_(held, 0));
        return from_word_(held);
    }

    void store_release(T value) noexcept {
        detail::WideWord const desired = to_word_(value);
#if defined(__x86_64__) && !defined(__SANITIZE_THREAD__)
        if (detail::has_atomic_vector_moves_) [[likely]] {
#if defined(__AVX__)
            asm volatile("vmovdqa\t{%1, %0|%0, %1}" : "=m"(word_) : "x"(desired) : "memory");
#else
            asm volatile("movdqa\t{%1, %0|%0, %1}" : "=m"(word_) : "x"(desired) : "memory");
#endif
            return;
        }
#endif
        detail::WideWord held = 0;
        while (!swap_word_(held, desired)) {}
    }

    // A strong compare-and-swap.  A failure gives the value that the cell
    // held, which is not EXPECTED.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::CasRefusal<T>>
    cas_acq_rel(T expected, T desired) noexcept {
        detail::WideWord held = to_word_(expected);
        if (swap_word_(held, to_word_(desired))) return ::foundation::core::Unit{};
        return ::foundation::core::err(::foundation::core::CasRefusal<T>{from_word_(held)});
    }
};

#endif

// The cell of 128 bits through a lock (THE TWO FORMS).  Each step takes the
// lock, so two threads read and write one cell at the same time.  It is a
// cell of its payload (foundation/Transfer.h).
template <WideAtomicValue T>
class[[= ::foundation::transfer::payload_cell{}]] WideAtomicByLock : ::foundation::Pinned<WideAtomicByLock<T>> {
    alignas(16) detail::WideWord word_ = 0;
    // 0 when no thread holds the lock, and 1 when a thread holds it.  A load
    // takes the lock, so a const cell writes the lock word.
    mutable ::foundation::core::Atomic<std::uint32_t> lock_word_{0};
    ::foundation::core::FixedArray<unsigned char, 12> padding_{};

    [[nodiscard]] static constexpr detail::WideWord to_word_(T const& value) noexcept {
        return __builtin_bit_cast(detail::WideWord, value);
    }

    [[nodiscard]] static constexpr T from_word_(detail::WideWord word) noexcept { return __builtin_bit_cast(T, word); }

    // The exchange takes the lock.  A thread that finds the lock held reads
    // the word until it is free, so the wait writes no line.
    void take_lock_() const noexcept {
        while (lock_word_.exchange_acq_rel(1) != 0) {
            while (lock_word_.load_acquire() != 0) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    }

    void give_lock_() const noexcept { lock_word_.store_release(0); }

public:
    using value_type = T;

    constexpr WideAtomicByLock() noexcept
        requires std::is_nothrow_default_constructible_v<T>
        : word_{to_word_(T{})} {}

    explicit constexpr WideAtomicByLock(T initial) noexcept : word_{to_word_(initial)} {}

    [[nodiscard]] T load_acquire() const noexcept {
        take_lock_();
        detail::WideWord const word = word_;
        give_lock_();
        return from_word_(word);
    }

    void store_release(T value) noexcept {
        detail::WideWord const word = to_word_(value);
        take_lock_();
        word_ = word;
        give_lock_();
    }

    // A strong compare-and-swap.  A failure gives the value that the cell
    // held, which is not EXPECTED.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::CasRefusal<T>>
    cas_acq_rel(T expected, T desired) noexcept {
        detail::WideWord const wanted = to_word_(expected);
        take_lock_();
        detail::WideWord const held = word_;
        bool const is_stored = held == wanted;
        if (is_stored) word_ = to_word_(desired);
        give_lock_();
        if (is_stored) return ::foundation::core::Unit{};
        return ::foundation::core::err(::foundation::core::CasRefusal<T>{from_word_(held)});
    }
};

// The cell of 128 bits of the target (THE TWO FORMS).
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
template <WideAtomicValue T>
using WideAtomic = WideAtomicByInstruction<T>;
#else
template <WideAtomicValue T>
using WideAtomic = WideAtomicByLock<T>;
#endif

}  // namespace foundation::os
