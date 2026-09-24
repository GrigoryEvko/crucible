// Sentinel TU for foundation/AlignedBuffer.h.  The buffer honours its
// alignment, a huge-page buffer is one at huge_page_bytes, value
// initialization gives each element its declared defaults, a move hands the
// storage on, and an index past the end or a byte count that wraps aborts.

#include <foundation/AlignedBuffer.h>

#include "abort_probe.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

namespace fnd = ::foundation;

struct WithDefaults {
    std::uint32_t id = 0xFFFF'FFFFU;
    std::uint16_t count;
    std::uint8_t flags;
};

// A value that refuses a start over bytes, the shape of a provenance tag:
// only its own default constructor builds it.
struct [[=::foundation::lifetime::no_start_over_bytes{}]] Stamped {
    std::uint64_t origin = 7;
};

template <typename T, std::size_t Alignment>
[[nodiscard]] bool is_aligned_to(const fnd::AlignedBuffer<T, Alignment>& buffer) noexcept {
    return (std::bit_cast<std::uintptr_t>(buffer.data()) & (Alignment - 1)) == 0;
}

// The storage meets the alignment the type names, and bytes() is the
// rounded allocation.
[[nodiscard]] int alignment_is_honoured() {
    auto plain = fnd::AlignedBuffer<int>::allocate(3);
    if (!plain || plain.size() != 3 || plain.bytes() != 12 || !is_aligned_to(plain)) return 1;
    auto line = fnd::AlignedBuffer<int, 64>::allocate(3);
    if (line.size() != 3 || line.bytes() != 64 || !is_aligned_to(line)) return 2;
    return 0;
}

// A huge-page buffer is the buffer at huge_page_bytes: one element takes a
// whole aligned huge page.
[[nodiscard]] int huge_page_buffer_is_one_page() {
    auto huge = fnd::AlignedBuffer<int, fnd::huge_page_bytes>::allocate(1);
    if (!huge || huge.size() != 1 || huge.bytes() != fnd::huge_page_bytes) return 1;
    if (!is_aligned_to(huge)) return 2;
    huge[0] = 7;
    if (huge.span()[0] != 7) return 3;
    return 0;
}

// Value initialization applies the member initializer and zeroes the rest.
// The allocator hands a freed block of the same size out again, so the bytes
// written here make a buffer that is not initialized fail the test.
[[nodiscard]] int value_initialized_elements_read_their_defaults() {
    {
        auto poison = fnd::AlignedBuffer<unsigned char, 64>::allocate(64);
        for (unsigned char& byte : poison.span())
            byte = 0xAB;
    }
    auto buffer = fnd::AlignedBuffer<WithDefaults, 64>::allocate_value_initialized(5);
    if (buffer.size() != 5) return 1;
    for (const WithDefaults& element : buffer.span()) {
        if (element.id != 0xFFFF'FFFFU || element.count != 0 || element.flags != 0) return 2;
    }

    // A class that refuses a start over bytes still builds by value
    // initialization, and each element reads the value its constructor set.
    auto stamped = fnd::AlignedBuffer<Stamped, fnd::huge_page_bytes>::allocate_value_initialized(3);
    for (const Stamped& element : stamped.span()) {
        if (element.origin != 7) return 3;
    }
    return 0;
}

// The elements are writable through the index and read back through the span.
[[nodiscard]] int elements_hold_values() {
    auto buffer = fnd::AlignedBuffer<std::uint64_t>::allocate(8);
    for (std::size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = i * i;
    std::uint64_t total = 0;
    for (const std::uint64_t value : buffer.span())
        total += value;
    return total == 140 ? 0 : 1;
}

// A move leaves the source empty, and a move assignment frees the target's
// old storage before it takes the new one.
[[nodiscard]] int move_hands_the_storage_on() {
    auto first = fnd::AlignedBuffer<int>::allocate(4);
    const int* const storage = first.data();
    fnd::AlignedBuffer<int> second{std::move(first)};
    if (first || !first.empty() || first.data() != nullptr || first.bytes() != 0) return 1;
    if (second.data() != storage || second.size() != 4) return 2;
    auto third = fnd::AlignedBuffer<int>::allocate(9);
    third = std::move(second);
    if (second || third.data() != storage || third.size() != 4) return 3;
    third.reset();
    if (third || !third.empty() || third.bytes() != 0) return 4;
    return 0;
}

// A count of zero is the empty buffer and allocates nothing.
[[nodiscard]] int zero_count_is_empty() {
    auto buffer = fnd::AlignedBuffer<int>::allocate(0);
    if (buffer || !buffer.empty() || buffer.data() != nullptr || !buffer.span().empty()) return 1;
    return 0;
}

}  // namespace

int main() {
    using foundation::test::aborts;

    if (const int failed = alignment_is_honoured(); failed != 0) return 10 + failed;
    if (const int failed = huge_page_buffer_is_one_page(); failed != 0) return 20 + failed;
    if (const int failed = value_initialized_elements_read_their_defaults(); failed != 0) return 30 + failed;
    if (const int failed = elements_hold_values(); failed != 0) return 40 + failed;
    if (const int failed = move_hands_the_storage_on(); failed != 0) return 50 + failed;
    if (const int failed = zero_count_is_empty(); failed != 0) return 60 + failed;

    // An index at the size is past the end, and the precondition aborts.
    auto guarded = fnd::AlignedBuffer<int>::allocate(2);
    if (!aborts([&guarded] { (void)guarded[2]; })) return 70;
    if (aborts([&guarded] { (void)guarded[1]; })) return 71;

    // A byte count that wraps would hand back a buffer smaller than asked
    // for, so each of the two wraps aborts.
    constexpr std::size_t max_count = std::numeric_limits<std::size_t>::max();
    if (!aborts([] { (void)fnd::AlignedBuffer<std::uint32_t>::allocation_bytes(max_count / 2); })) return 72;
    if (!aborts([] { (void)fnd::AlignedBuffer<unsigned char, 4096>::allocation_bytes(max_count - 1); })) return 73;
    guarded.reset();

    return 0;
}
