// The compile-time checks of foundation/Simd.h.

#include <foundation/Simd.h>

namespace foundation::simd {

namespace detail::simd_self_test {

// Each mask lane is the signed integer of the lane width of its value.
static_assert(std::is_same_v<i64x8::mask_type, mask<std::int64_t, 8>>);
static_assert(std::is_same_v<u64x4::mask_type, mask<std::int64_t, 4>>);
static_assert(std::is_same_v<u32x8::mask_type, mask<std::int32_t, 8>>);
static_assert(std::is_same_v<u8x32::mask_type, mask<std::int8_t, 32>>);
static_assert(std::is_same_v<vec<float, 8>::mask_type, mask<std::int32_t, 8>>);
static_assert(std::is_same_v<vec<double, 4>::mask_type, mask<std::int64_t, 4>>);

// A register holds exactly its lanes.
static_assert(sizeof(i64x8) == 64 && sizeof(u32x8) == 32 && sizeof(u8x16) == 16);
static_assert(sizeof(i64x8_mask) == sizeof(i64x8));

// The shapes that GCC refuses as a vector are refused by the constraint.
static_assert(VectorShape<std::int16_t, 8> && VectorShape<double, 2>);
static_assert(!VectorShape<bool, 8>, "a vector of bool is not a GCC vector type");
static_assert(!VectorShape<const int, 8>, "a lane is not cv-qualified");
static_assert(!VectorShape<int, 3>, "the lane count is a power of two");
static_assert(!VectorShape<int, 1> && !VectorShape<int, 0> && !VectorShape<int, -4>, "a vector has two lanes or more");
static_assert(!VectorShape<int*, 4>, "a pointer is not a vector element");

// A mask lane is the signed integer that a comparison writes, and a shape
// off the constraint is refused at the head of each template.
template <typename MaskElement, int Lanes>
concept can_mask = requires { typename mask<MaskElement, Lanes>; };
template <typename ElementType, int Lanes>
concept can_vec = requires { typename vec<ElementType, Lanes>; };
static_assert(can_mask<std::int64_t, 8> && can_mask<std::int8_t, 32>);
static_assert(!can_mask<float, 8> && !can_mask<std::uint64_t, 8>, "a mask lane is a signed integer");
static_assert(!can_mask<std::int32_t, 3>, "a mask has the shape of a vector");
static_assert(can_vec<char, 16> && can_vec<double, 2>);
static_assert(!can_vec<bool, 8> && !can_vec<int, 6> && !can_vec<volatile int, 8>);

// A load from bytes reads exactly the bytes of one register.
static_assert(RegisterBytes<u64x4, std::byte const, 32> && RegisterBytes<u8x16, std::uint8_t, 16>);
static_assert(!RegisterBytes<u64x4, std::byte const, 16>, "a load reads exactly the bytes of one register");
static_assert(!RegisterBytes<u64x4, std::uint64_t const, 4>, "the View holds bytes, not lanes");

// A load from lanes reads exactly the lanes of one register, of the lane
// type of the vector.
static_assert(RegisterLanes<u64x4, std::uint64_t const, 4> && RegisterLanes<i64x8, std::int64_t, 8>);
static_assert(!RegisterLanes<i64x8, std::int64_t const, 4>, "a load reads each lane of one register");
static_assert(!RegisterLanes<i64x8, std::uint64_t const, 8>, "the lanes have the lane type of the vector");
static_assert(!RegisterLanes<u8x16, std::uint8_t const, 16>, "a View of bytes takes the load of RegisterBytes");

// A sign gather reads a register of one-byte lanes, a vec or a mask.
static_assert(ByteLaneRegister<vec<std::int8_t, 16>> && ByteLaneRegister<vec<char, 32>>
              && ByteLaneRegister<u8x32::mask_type> && ByteLaneRegister<vec<std::uint8_t, 64>>);
static_assert(!ByteLaneRegister<vec<std::int8_t, 8>>, "a register of 8 bytes is no register of a target");
static_assert(!ByteLaneRegister<u64x4>, "a lane of 8 bytes gives no one bit");

// A lookup in a table of 16 bytes reads one register of 16, 32 or 64 byte
// lanes, and gives a register of the same lanes.
static_assert(NibbleLookupLanes<16> && NibbleLookupLanes<32> && NibbleLookupLanes<64>);
static_assert(!NibbleLookupLanes<8> && !NibbleLookupLanes<128>, "a lookup reads one register of a target");
static_assert(std::is_same_v<decltype(lookup_nibbles(u8x16{}, u8x32{})), u8x32>);

// Bit 7 of each byte reaches its own bit.  The top byte needs bit 0 of the
// multiplier: a multiplier with no bit 0 drops it.
static_assert(collect_word_sign_bits(0x0000000000000080u) == 0x01u);
static_assert(collect_word_sign_bits(0x8000000000000000u) == 0x80u);
static_assert(collect_word_sign_bits(0x8080808080808080u) == 0xFFu);
static_assert(collect_word_sign_bits(0x7F7F7F7F7F7F7F7Fu) == 0x00u);
static_assert(collect_word_sign_bits(0xFF00FF00FF00FF00u) == 0xAAu);

// The even lanes and the odd lanes of two vectors, and of one vector.  Lane i
// of the result takes lane 2i, or 2i + 1, of the concatenation, so a sum of
// the two takes the adjacent pairs in order.
[[nodiscard]] consteval bool alternate_lanes_keep_their_order() noexcept {
    using Quad = vec<std::int32_t, 4>;
    Quad const low = iota_v<Quad>();
    Quad const high = low + Quad{std::int32_t{4}};
    Quad const even = pick_even_lanes(low, high);
    Quad const odd = pick_odd_lanes(low, high);
    bool is_ordered = true;
    for (int lane = 0; lane < 4; ++lane) {
        is_ordered = is_ordered && even[lane] == 2 * lane && odd[lane] == 2 * lane + 1;
    }
    vec<std::int32_t, 2> const half_even = pick_even_lanes(low);
    vec<std::int32_t, 2> const half_odd = pick_odd_lanes(low);
    return is_ordered && half_even[0] == 0 && half_even[1] == 2 && half_odd[0] == 1 && half_odd[1] == 3;
}
static_assert(alternate_lanes_keep_their_order());

// canonicalize_nans gives the canonical NaN in each lane of a NaN, a quiet,
// a signaling and a negative one, and keeps the bits of each other lane.
[[nodiscard]] consteval bool canonicalizes_each_nan_lane() noexcept {
    using Floats = vec<float, 4>;
    using Words = vec<std::uint32_t, 4>;
    Words const input = Words{[](auto lane) noexcept {
        constexpr int index = decltype(lane)::value;
        return index == 0 ? 0x7FC12345u : index == 1 ? 0x7F800001u : index == 2 ? 0xFF800000u : 0xFFFFFFFFu;
    }};
    Words const output = bit_cast<Words>(canonicalize_nans(bit_cast<Floats>(input)));
    return output[0] == 0x7FC00000u && output[1] == 0x7FC00000u && output[2] == 0xFF800000u && output[3] == 0x7FC00000u;
}
static_assert(canonicalizes_each_nan_lane());
static_assert(std::is_same_v<decltype(pick_even_lanes(vec<float, 16>{})), vec<float, 8>>);
static_assert(native_lanes<float> * sizeof(float) == native_vector_bytes);
static_assert(native_lanes<double> * sizeof(double) == native_vector_bytes);

}  // namespace detail::simd_self_test

}  // namespace foundation::simd
