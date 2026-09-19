#pragma once

#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Hw.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::fixy::simd {

// Bits1024 and Bits2048 are enumerated ahead of use so a future SVE kernel
// pins them without an enum edit. Neither carries a convenience alias yet.
enum class WidthBits : std::uint16_t {
    Scalar = 0,
    Bits128 = 128,
    Bits256 = 256,
    Bits512 = 512,
    Bits1024 = 1024,
    Bits2048 = 2048,
};

// A scoped enum value can still fall outside the enumerators: an explicit
// static_cast produces one. This gate is what rejects it.
[[nodiscard]] constexpr bool is_known_width(WidthBits width_value) noexcept {
    switch (width_value) {
        case WidthBits::Scalar:
        case WidthBits::Bits128:
        case WidthBits::Bits256:
        case WidthBits::Bits512:
        case WidthBits::Bits1024:
        case WidthBits::Bits2048:
            return true;
        default:
            return false;
    }
}

template <WidthBits W>
inline constexpr bool is_known_width_v = is_known_width(W);

}  // namespace crucible::fixy::simd

namespace crucible::fixy::grant::simd {

template <::crucible::fixy::simd::WidthBits W>
    requires ::crucible::fixy::simd::is_known_width_v<W>
struct width final : grant_base {};

}  // namespace crucible::fixy::grant::simd

namespace crucible::fixy::grant {

template <::crucible::fixy::simd::WidthBits W>
    requires ::crucible::fixy::simd::is_known_width_v<W>
struct which_dim<simd::width<W>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SimdIsa> {};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::simd {

namespace gs = ::crucible::fixy::grant::simd;

using width_scalar = gs::width<WidthBits::Scalar>;
using width_128 = gs::width<WidthBits::Bits128>;
using width_256 = gs::width<WidthBits::Bits256>;
using width_512 = gs::width<WidthBits::Bits512>;

}  // namespace crucible::fixy::simd

namespace crucible::fixy::simd::detail::v259_self_test {

namespace gs = ::crucible::fixy::grant::simd;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(is_known_width(WidthBits::Scalar));
static_assert(is_known_width(WidthBits::Bits128));
static_assert(is_known_width(WidthBits::Bits256));
static_assert(is_known_width(WidthBits::Bits512));
static_assert(is_known_width(WidthBits::Bits1024));
static_assert(is_known_width(WidthBits::Bits2048));
static_assert(!is_known_width(static_cast<WidthBits>(64)));
static_assert(!is_known_width(static_cast<WidthBits>(777)));

static_assert(IsGrantTag<gs::width<WidthBits::Bits256>>);
static_assert(IsGrantTag<gs::width<WidthBits::Bits1024>>);
static_assert(which_dim_v<gs::width<WidthBits::Scalar>> == D::SimdIsa);
static_assert(which_dim_v<gs::width<WidthBits::Bits512>> == D::SimdIsa);

static_assert(sizeof(gs::width<WidthBits::Scalar>) == 1);
static_assert(sizeof(gs::width<WidthBits::Bits512>) == 1);
static_assert(sizeof(gs::width<WidthBits::Bits2048>) == 1);

static_assert(!std::is_same_v<gs::width<WidthBits::Bits128>, gs::width<WidthBits::Bits256>>);
static_assert(!std::is_same_v<gs::width<WidthBits::Bits512>, gs::width<WidthBits::Bits1024>>);
static_assert(std::is_same_v<gs::width<WidthBits::Bits256>, gs::width<WidthBits::Bits256>>);

static_assert(!::crucible::fixy::grant::IsGrantTag_v<const gs::width<WidthBits::Bits256>>);
static_assert(!::crucible::fixy::grant::IsGrantTag_v<gs::width<WidthBits::Bits512>&>);

static_assert(std::is_same_v<width_scalar, gs::width<WidthBits::Scalar>>);
static_assert(std::is_same_v<width_128, gs::width<WidthBits::Bits128>>);
static_assert(std::is_same_v<width_256, gs::width<WidthBits::Bits256>>);
static_assert(std::is_same_v<width_512, gs::width<WidthBits::Bits512>>);
static_assert(IsGrantTag<width_scalar>);
static_assert(IsGrantTag<width_512>);

// The locals below are non-constant so the calls are not folded at compile
// time.
inline void runtime_smoke_test() {
    WidthBits w = WidthBits::Bits256;
    [[maybe_unused]] bool known = is_known_width(w);
    [[maybe_unused]] bool unknown = is_known_width(static_cast<WidthBits>(48));

    [[maybe_unused]] width_256 a{};
    [[maybe_unused]] width_512 b{};
    [[maybe_unused]] gs::width<WidthBits::Bits1024> sve{};
}

}  // namespace crucible::fixy::simd::detail::v259_self_test

namespace crucible::fixy::simd::detail::v039_simd_isa_duplicate_witness {

namespace gh = ::crucible::fixy::grant::hw;
namespace gs = ::crucible::fixy::grant::simd;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(which_dim_v<gh::simd_width<256>> == D::SimdIsa, "hw::simd_width<W> must route to SimdIsa.");
static_assert(which_dim_v<gs::width<WidthBits::Bits256>> == D::SimdIsa, "simd::width<W> must route to SimdIsa.");

static_assert(which_dim_v<gh::simd_width<256>> == which_dim_v<gs::width<WidthBits::Bits256>>,
              "hw::simd_width and simd::width must share an axis so a Grants "
              "pack containing both fails UniqueEngagementPerAxis "
              "(FixyDuplicate_SimdIsa).");

static_assert(!std::is_same_v<gh::simd_width<256>, gs::width<WidthBits::Bits256>>,
              "The two grants are structurally distinct types with different "
              "NTTP shapes. They coexist at type level but not within a single "
              "Grants pack.");

static_assert(static_cast<std::uint16_t>(WidthBits::Bits256) == 256u,
              "WidthBits::Bits256 has underlying value 256, so both grant "
              "families name the same physical width.");

}  // namespace crucible::fixy::simd::detail::v039_simd_isa_duplicate_witness
