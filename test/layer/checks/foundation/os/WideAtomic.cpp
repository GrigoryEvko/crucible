// The compile-time checks of foundation/os/WideAtomic.h.

#include <foundation/os/WideAtomic.h>

#include <cstdint>
#include <type_traits>

namespace foundation::os::detail::wide_atomic_checks {

// An address and a count: 16 bytes with no padding.
struct Generation {
    std::uint64_t address = 0;
    std::uint64_t count = 0;
};

// Twelve bytes of members.
struct ThreeWords {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
};

// Twelve bytes of members and four bytes of padding.
struct AddressAndTag {
    std::uint64_t address = 0;
    std::uint32_t tag = 0;
};

// A value of 16 bytes with one bit pattern for each value.  A floating value
// has two zeros, so a float of 16 bytes is refused.
static_assert(WideAtomicValue<Generation> && WideAtomicValue<WideWord>);
static_assert(!WideAtomicValue<ThreeWords> && !WideAtomicValue<AddressAndTag>);
static_assert(!WideAtomicValue<Generation const> && !WideAtomicValue<Generation volatile>);
static_assert(!WideAtomicValue<long double> && !WideAtomicValue<std::uint64_t>);

// The lock form has 32 bytes, the value and the lock word, at each level.
// The instruction form has the 16 bytes of the value.
static_assert(sizeof(WideAtomicByLock<Generation>) == 32 && alignof(WideAtomicByLock<Generation>) == 16);
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
static_assert(sizeof(WideAtomicByInstruction<Generation>) == 16 && alignof(WideAtomicByInstruction<Generation>) == 16);
static_assert(std::is_same_v<WideAtomic<Generation>, WideAtomicByInstruction<Generation>>);
#else
static_assert(std::is_same_v<WideAtomic<Generation>, WideAtomicByLock<Generation>>);
#endif

// A cell is pinned: no copy and no move.
static_assert(!std::is_copy_constructible_v<WideAtomicByLock<Generation>>
              && !std::is_move_constructible_v<WideAtomicByLock<Generation>>);
static_assert(!std::is_copy_constructible_v<WideAtomic<Generation>>
              && !std::is_move_constructible_v<WideAtomic<Generation>>);

// Two threads read and write one cell of each form (foundation/Transfer.h).
static_assert(::foundation::transfer::Synchronized<WideAtomicByLock<Generation>>
              && ::foundation::transfer::Synchronized<WideAtomic<Generation>>);

}  // namespace foundation::os::detail::wide_atomic_checks
