// Sentinel TU for foundation/SwissTableBuffer.h.  One allocation holds the
// control bytes and the slots, the slots start at the capacity offset with
// their lifetime started, a move hands the allocation on, and a capacity
// off the shape or a byte size that wraps aborts.  The build compiles this
// file twice: once under the contract semantic of the preset and once
// under the ignore semantic, where no contract clause checks anything.

#include <foundation/SwissTableBuffer.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using PtrBuffer = ::foundation::SwissTableBuffer<const int*>;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the layout test reads the address of the first element of each array of a buffer")
// The address of the first element of a view, as an integer.
template <class T>
[[nodiscard]] std::uintptr_t address_of_first(::fixy::View<T> view) noexcept {
    return std::bit_cast<std::uintptr_t>(::foundation::core::foreign_first(view));
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// A slot of 2^34 - 1 bytes.  At the largest capacity, 2^30, the slots take
// 2^64 - 2^30 bytes, and the control bytes in front of them make the sum
// wrap to zero.  The type is never built, only named.
struct HugeSlot {
    ::fixy::FixedArray<unsigned char, (std::size_t{1} << 34) - 1> bytes;
};
using HugeBuffer = ::foundation::SwissTableBuffer<HugeSlot>;

// The allocation is one block of 64-byte alignment: the control bytes
// first, the slots after them, the total rounded to 64.
[[nodiscard]] int layout_is_one_block() {
    PtrBuffer buffer = PtrBuffer::allocate(16);
    if (!buffer || buffer.empty() || buffer.capacity() != 16) return 1;
    if (buffer.ctrl().size() != 16 || buffer.slots().size() != 16) return 5;
    const std::uintptr_t ctrl_address = address_of_first(buffer.ctrl());
    const std::uintptr_t slots_address = address_of_first(buffer.slots());
    if ((ctrl_address & 63U) != 0) return 2;
    if (slots_address - ctrl_address != 16) return 3;
    // 16 control bytes and 16 pointers of 8 bytes are 144 bytes, rounded to 192.
    if (buffer.alloc_bytes() != 192) return 4;
    return 0;
}

// Every slot and every control byte is writable and reads back.
[[nodiscard]] int slots_hold_values() {
    static const ::fixy::FixedArray<int, 16> anchors{};
    PtrBuffer buffer = PtrBuffer::allocate(16);
    if (buffer.capacity() != 16) return 3;
    const auto ctrl = buffer.ctrl().window<16>(0).expect("a buffer of capacity 16 holds 16 control bytes");
    const auto slots = buffer.slots().window<16>(0).expect("a buffer of capacity 16 holds 16 slots");
    for (const ::fixy::Index<16> i : ::fixy::IndexRange<16>{}) {
        ctrl.at(i) = static_cast<std::int8_t>(-128);
        slots.at(i) = &anchors.at(i);
    }
    for (const ::fixy::Index<16> i : ::fixy::IndexRange<16>{}) {
        if (ctrl.at(i) != static_cast<std::int8_t>(-128)) return 1;
        if (slots.at(i) != &anchors.at(i)) return 2;
    }
    return 0;
}

// A move leaves the source empty, and a move assignment frees the old
// allocation of its target before it takes the new one.
[[nodiscard]] int move_hands_the_block_on() {
    PtrBuffer first = PtrBuffer::allocate(32);
    const std::uintptr_t ctrl_address = address_of_first(first.ctrl());
    PtrBuffer second{std::move(first)};
    if (first || !first.empty() || first.ctrl().size() != 0 || first.slots().size() != 0) return 1;
    if (first.alloc_bytes() != 0 || address_of_first(second.ctrl()) != ctrl_address || second.capacity() != 32)
        return 2;
    PtrBuffer third = PtrBuffer::allocate(64);
    third = std::move(second);
    if (second || address_of_first(third.ctrl()) != ctrl_address || third.capacity() != 32) return 3;
    third.reset();
    if (third || !third.empty() || third.alloc_bytes() != 0) return 4;
    return 0;
}

// Capacity zero is the empty table and allocates nothing.
[[nodiscard]] int zero_capacity_is_empty() {
    PtrBuffer buffer = PtrBuffer::allocate(0);
    if (buffer || !buffer.empty() || buffer.ctrl().size() != 0 || buffer.alloc_bytes() != 0) return 1;
    return 0;
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};

    if (const int failed = layout_is_one_block(); failed != 0) return 10 + failed;
    if (const int failed = slots_hold_values(); failed != 0) return 20 + failed;
    if (const int failed = move_hands_the_block_on(); failed != 0) return 30 + failed;
    if (const int failed = zero_capacity_is_empty(); failed != 0) return 40 + failed;

    // A capacity below the group width, not a power of two or past the
    // bound aborts before anything is allocated, under every semantic.  Each
    // refused allocation runs in a child through expect_dies of the Check
    // family, and the capacity of 1024 is the control in the test process.
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)PtrBuffer::allocate(8); },
        ::foundation::core::TextView{"foundation: fatal invariant failed: is_swiss_table_capacity(capacity)"});
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)PtrBuffer::allocate(48); },
        ::foundation::core::TextView{"foundation: fatal invariant failed: is_swiss_table_capacity(capacity)"});
    if (PtrBuffer::allocate(1024).capacity() != 1024) return 52;
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)PtrBuffer::allocate(::foundation::swiss_table_max_capacity * 2); },
        ::foundation::core::TextView{"foundation: fatal invariant failed: is_swiss_table_capacity(capacity)"});

    // The slot bytes fit, and the control bytes added to them wrap the size
    // to zero.  The size check aborts before the heap sees the request.
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)HugeBuffer::allocate(::foundation::swiss_table_max_capacity); },
        ::foundation::core::TextView{
            "foundation: fatal: 1073741824 prefix bytes and 1073741824 elements of 17179869183 bytes, rounded "
            "to 64, do not fit in size_t"});

    return 0;
}
