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

}  // namespace detail::simd_self_test

}  // namespace foundation::simd
