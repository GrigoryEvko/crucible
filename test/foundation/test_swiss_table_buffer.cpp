// Sentinel TU for foundation/SwissTableBuffer.h.  One allocation holds the
// control bytes and the slots, the slots start at the capacity offset with
// their lifetime started, a move hands the allocation on, and a capacity
// off the shape or a byte size that wraps aborts.  The build compiles this
// file twice: once under the contract semantic of the preset and once
// under the ignore semantic, where no contract clause checks anything.

#include <foundation/SwissTableBuffer.h>

#include "abort_probe.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using PtrBuffer = ::foundation::SwissTableBuffer<const int*>;

// A slot of 2^34 - 1 bytes.  At the largest capacity, 2^30, the slots take
// 2^64 - 2^30 bytes, and the control bytes in front of them make the sum
// wrap to zero.  The type is never built, only named.
struct HugeSlot {
    unsigned char bytes[(std::size_t{1} << 34) - 1];
};
using HugeBuffer = ::foundation::SwissTableBuffer<HugeSlot>;

// The allocation is one block of 64-byte alignment: the control bytes
// first, the slots after them, the total rounded to 64.
[[nodiscard]] int layout_is_one_block() {
    PtrBuffer buffer = PtrBuffer::allocate(16);
    if (!buffer || buffer.empty() || buffer.capacity() != 16) return 1;
    if (buffer.ctrl().size() != 16 || buffer.slots().size() != 16) return 5;
    const auto ctrl_address = std::bit_cast<std::uintptr_t>(buffer.ctrl().data());
    const auto slots_address = std::bit_cast<std::uintptr_t>(buffer.slots().data());
    if ((ctrl_address & 63U) != 0) return 2;
    if (slots_address - ctrl_address != 16) return 3;
    // 16 control bytes and 16 pointers of 8 bytes are 144 bytes, rounded to 192.
    if (buffer.alloc_bytes() != 192) return 4;
    return 0;
}

// Every slot and every control byte is writable and reads back.
[[nodiscard]] int slots_hold_values() {
    static const std::array<int, 16> anchors{};
    PtrBuffer buffer = PtrBuffer::allocate(16);
    for (std::size_t i = 0; i < buffer.capacity(); ++i) {
        buffer.ctrl()[i] = static_cast<std::int8_t>(-128);
        buffer.slots()[i] = &anchors[i];
    }
    for (std::size_t i = 0; i < buffer.capacity(); ++i) {
        if (buffer.ctrl()[i] != static_cast<std::int8_t>(-128)) return 1;
        if (buffer.slots()[i] != &anchors[i]) return 2;
    }
    return 0;
}

// A move leaves the source empty, and a move assignment frees the old
// allocation of its target before it takes the new one.
[[nodiscard]] int move_hands_the_block_on() {
    PtrBuffer first = PtrBuffer::allocate(32);
    const auto* const ctrl = first.ctrl().data();
    PtrBuffer second{std::move(first)};
    if (first || !first.empty() || !first.ctrl().empty() || !first.slots().empty()) return 1;
    if (first.alloc_bytes() != 0 || second.ctrl().data() != ctrl || second.capacity() != 32) return 2;
    PtrBuffer third = PtrBuffer::allocate(64);
    third = std::move(second);
    if (second || third.ctrl().data() != ctrl || third.capacity() != 32) return 3;
    third.reset();
    if (third || !third.empty() || third.alloc_bytes() != 0) return 4;
    return 0;
}

// Capacity zero is the empty table and allocates nothing.
[[nodiscard]] int zero_capacity_is_empty() {
    PtrBuffer buffer = PtrBuffer::allocate(0);
    if (buffer || !buffer.empty() || !buffer.ctrl().empty() || buffer.alloc_bytes() != 0) return 1;
    return 0;
}

}  // namespace

int main() {
    using foundation::test::aborts;

    if (const int failed = layout_is_one_block(); failed != 0) return 10 + failed;
    if (const int failed = slots_hold_values(); failed != 0) return 20 + failed;
    if (const int failed = move_hands_the_block_on(); failed != 0) return 30 + failed;
    if (const int failed = zero_capacity_is_empty(); failed != 0) return 40 + failed;

    // A capacity below the group width, not a power of two or past the
    // bound aborts before anything is allocated, under every semantic.
    if (!aborts([] { (void)PtrBuffer::allocate(8); })) return 50;
    if (!aborts([] { (void)PtrBuffer::allocate(48); })) return 51;
    if (aborts([] { (void)PtrBuffer::allocate(1024); })) return 52;
    if (!aborts([] { (void)PtrBuffer::allocate(::foundation::swiss_table_max_capacity * 2); })) return 53;

    // The slot bytes fit, and the control bytes added to them wrap the size
    // to zero.  The size check aborts before the heap sees the request.
    if (!aborts([] { (void)HugeBuffer::allocate(::foundation::swiss_table_max_capacity); })) return 54;

    return 0;
}
