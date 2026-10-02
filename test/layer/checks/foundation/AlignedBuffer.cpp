// The compile-time checks of foundation/AlignedBuffer.h.

#include <foundation/AlignedBuffer.h>

namespace foundation {

namespace detail::aligned_buffer_self_test {

// A class that holds a proof, whose lifetime cannot start over bytes.
using ::foundation::lifetime::detail::HoldsProof;

// A count that refuses a start over bytes and builds from its own default
// constructor, the shape of a provenance tag.
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] MarkedCount {
    unsigned long long count = 0;
};

template <typename T, std::size_t Alignment = alignof(T)>
concept can_buffer = requires { typename AlignedBuffer<T, Alignment>; };

template <typename T>
concept can_allocate = requires { AlignedBuffer<T>::allocate(std::size_t{1}); };

template <typename T>
concept can_value_initialize = requires { AlignedBuffer<T>::allocate_value_initialized(std::size_t{1}); };

static_assert(sizeof(AlignedBuffer<int>) == sizeof(void*) + sizeof(std::size_t));
static_assert(!std::is_copy_constructible_v<AlignedBuffer<int>> && !std::is_copy_assignable_v<AlignedBuffer<int>>);
static_assert(std::is_nothrow_move_constructible_v<AlignedBuffer<int>>);
static_assert(std::is_nothrow_move_assignable_v<AlignedBuffer<int>>);
static_assert(can_buffer<int> && can_buffer<unsigned char, 4096> && can_buffer<double, huge_page_bytes>);
static_assert(can_allocate<int> && can_value_initialize<int>);
static_assert(!can_allocate<HoldsProof> && !can_value_initialize<HoldsProof>,
              "a proof element can start its lifetime by neither route");
static_assert(!can_allocate<MarkedCount> && can_value_initialize<MarkedCount>,
              "a class that refuses a start over bytes builds from its own default constructor");
static_assert(!can_buffer<int, 3> && !can_buffer<double, 4>, "the alignment is a power of two, at least alignof(T)");
static_assert(!can_buffer<int&>, "an element is an object type");
static_assert(AlignedBuffer<int>::allocation_bytes(3) == 12);
static_assert(AlignedBuffer<int, 64>::allocation_bytes(3) == 64);
static_assert(AlignedBuffer<unsigned char, huge_page_bytes>::allocation_bytes(1) == huge_page_bytes);
static_assert(aligned_allocation_bytes<64>(16, 16, 8) == 192, "16 prefix bytes and 16 slots of 8 bytes round to 192");
static_assert(aligned_allocation_bytes<16>(0, 0, 8) == 0);

// A constant evaluation builds an empty buffer, resets it and destroys it.  A
// class with an empty buffer member is then a literal type too.
struct HoldsEmptyBuffer {
    AlignedBuffer<unsigned long long> buffer;
    int marker = 7;
};

consteval int empty_buffer_lives_in_a_constant_evaluation() {
    AlignedBuffer<int> buffer{};
    buffer.reset();
    HoldsEmptyBuffer holder{};
    holder.buffer.reset();
    return holder.marker;
}
static_assert(empty_buffer_lives_in_a_constant_evaluation() == 7);

// A size that wraps is not a constant expression: the abort is not constexpr.
template <std::size_t Prefix, std::size_t Count, std::size_t ElementBytes>
concept has_constant_size = requires {
    typename std::integral_constant<std::size_t, aligned_allocation_bytes<64>(Prefix, Count, ElementBytes)>;
};
inline constexpr std::size_t max_size = ~std::size_t{0};
static_assert(has_constant_size<0, 1, 8>);
static_assert(!has_constant_size<0, (std::size_t{1} << 62), 8>, "count * element bytes wraps");
static_assert(!has_constant_size<17, (std::size_t{1} << 60) - 1, 16>, "prefix + element bytes wraps");
static_assert(!has_constant_size<0, max_size, 1>, "the round up to the alignment wraps");

}  // namespace detail::aligned_buffer_self_test

}  // namespace foundation
