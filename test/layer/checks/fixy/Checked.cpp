// The compile-time checks of fixy/Checked.h.

#include <fixy/Checked.h>

namespace fixy {

// Only the accepting cases can be witnessed here.  A rejection is a
// failed static_assert, which would break this translation unit, so
// those cases live in the negative-compile harness instead.

static_assert(safe_add<std::uint32_t, 10u, 20u> == 30u);
static_assert(safe_sub<std::uint32_t, 30u, 20u> == 10u);
static_assert(safe_mul<std::uint32_t, 6u, 7u> == 42u);
static_assert(safe_capacity<8u, 16u> == 128u);
static_assert(safe_capacity<std::size_t{1} << 16, std::size_t{1} << 16> == (std::size_t{1} << 32));

static_assert(safe_mul<std::size_t, std::size_t{0}, std::size_t{1} << 60> == 0u);
static_assert(safe_add<std::size_t, std::size_t{0}, std::size_t{0}> == 0u);

static_assert(safe_add_all<std::size_t> == 0u);
static_assert(safe_add_all<std::size_t, 42u> == 42u);
static_assert(safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u> == 15u);
static_assert(safe_add_all<std::uint32_t, 10u, 20u, 30u> == 60u);

static_assert(safe_array_bytes<std::uint64_t, 8u> == 64u);
static_assert(safe_array_bytes<std::byte, 4096u> == 4096u);
static_assert(safe_array_bytes<std::uint32_t, 0u> == 0u);

static_assert(safe_struct_bytes<> == 0u);
static_assert(safe_struct_bytes<std::uint64_t> == 8u);
static_assert(safe_struct_bytes<std::uint64_t, std::uint32_t> == 12u);
static_assert(safe_struct_bytes<std::uint64_t, std::uint64_t, std::uint32_t> == 20u);

static_assert(bytes_fit_v<64u, 20u>);
static_assert(bytes_fit_v<64u, 64u>);
static_assert(!bytes_fit_v<64u, 65u>);

[[maybe_unused]] constexpr auto _check_fits = []() {
    ensure_bytes_fit<64, safe_struct_bytes<std::uint64_t, std::uint64_t>>();
    return 0;
}();

}  // namespace fixy
