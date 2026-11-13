#pragma once

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Simd.h>
#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>

#include <cstdint>
#include <type_traits>

namespace crucible {
namespace detail {

inline constexpr int8_t kEmpty = static_cast<int8_t>(0x80);

using GroupWidth = ::fixy::PowerOfTwo<std::size_t>;

#if defined(__AVX512BW__)
static constexpr GroupWidth kGroupWidth = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{64});
#elif defined(__AVX2__)
static constexpr GroupWidth kGroupWidth = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{32});
#else
static constexpr GroupWidth kGroupWidth = ::fixy::mint_refined<::fixy::power_of_two>(std::size_t{16});
#endif

[[nodiscard]] consteval std::size_t group_width() noexcept { return kGroupWidth.value(); }

// The ISA the probe was emitted for and the instruction class it issues,
// restated as fixy atoms of the arm the preprocessor selects above.
namespace swiss_hw {

namespace fas = ::fixy::atom::simd;
namespace fah = ::fixy::atom::hw;

#if defined(__AVX512BW__)
using ActiveSimdIsa = fas::avx512bw;
#elif defined(__AVX2__)
using ActiveSimdIsa = fas::avx2;
#elif defined(__SSE2__)
using ActiveSimdIsa = fas::sse2;
#elif defined(__aarch64__)
using ActiveSimdIsa = fas::neon;
#else
using ActiveSimdIsa = fas::scalar;
#endif

// The four vector instruction sets issue vector instructions.  A target with
// none of them runs the group in general-purpose registers, which is the
// scalar class.
using InstructionTier = std::conditional_t<fas::is_trunk_pinned(ActiveSimdIsa::isa), fah::vectorizable, fah::scalar>;

}  // namespace swiss_hw

[[nodiscard]] consteval uint64_t group_mask_ceiling(std::size_t width) noexcept {
    return width == 64 ? ::fixy::max_of<uint64_t> : ((uint64_t{1} << width) - uint64_t{1});
}

static constexpr uint64_t kGroupMaskCeiling = group_mask_ceiling(group_width());

// The shift by 57 leaves the result in [0, 127], so bit 7 of the int8_t is
// always clear. That reserves 0x80 for kEmpty: a tag with bit 7 set would
// read as an empty slot and end the probe loop early.
//
// A post clause here is rejected. The constexpr evaluator folds the body to
// a constant and then reports the contract condition as non-constant. The
// boundary static_asserts in the check file of this header carry the same
// obligation.
[[nodiscard, gnu::const]] constexpr int8_t h2_tag(uint64_t hash) noexcept { return static_cast<int8_t>(hash >> 57); }

// Each set bit is a slot offset in [0, kGroupWidth).
struct BitMask {
    using Mask = ::fixy::Refined<::fixy::bounded_above<kGroupMaskCeiling>, uint64_t>;

    static_assert(sizeof(Mask) == sizeof(uint64_t));
    static_assert(std::is_trivially_copy_constructible_v<Mask> && std::is_trivially_move_constructible_v<Mask>
                  && std::is_trivially_destructible_v<Mask>);
    static_assert(std::is_standard_layout_v<Mask>);

    constexpr BitMask() noexcept = default;
    constexpr explicit BitMask(uint64_t raw_mask) noexcept : mask_(checked_mask_(raw_mask)) {}
    constexpr explicit BitMask(Mask mask) noexcept : mask_(mask) {}

    [[nodiscard]] constexpr uint64_t raw() const noexcept { return mask_.value(); }

    [[nodiscard]] CRUCIBLE_INLINE explicit operator bool() const { return raw() != 0; }

    // countr_zero returns 64 for a zero mask. That is outside every group's
    // slot range, so a probe loop would read it as a real slot index.
    [[nodiscard]] CRUCIBLE_INLINE uint32_t lowest() const {
        CRUCIBLE_PRE(raw() != uint64_t{0});
        return static_cast<uint32_t>(::fixy::countr_zero(raw()));
    }

    CRUCIBLE_INLINE void clear_lowest() {
        const uint64_t m = raw();
        mask_ = checked_mask_(m & (m - uint64_t{1}));
    }

private:
    // Every raw word reaches the mask through the checked mint, so the
    // ceiling is tested at each construction, as a constant too.
    [[nodiscard]] static constexpr Mask checked_mask_(uint64_t raw_mask) noexcept {
        return ::fixy::mint_refined<::fixy::bounded_above<kGroupMaskCeiling>>(raw_mask);
    }

    Mask mask_ = checked_mask_(uint64_t{0});
};

// One group of control bytes in one register of the active instruction set.
// foundation/Simd.h holds the instructions of each target.
struct CtrlGroup {
    using Bytes = ::foundation::simd::vec<int8_t, static_cast<int>(group_width())>;

    Bytes ctrl{};

    // The group_width() control bytes from pos.  The bytes need no alignment.
    // The pointer stays, because the intern probe of ExprPool holds its
    // control bytes as a raw int8_t pointer.
    [[nodiscard]] CRUCIBLE_INLINE static CtrlGroup load(const int8_t* pos) {
        return {::foundation::simd::load<Bytes>(pos)};
    }

    [[nodiscard]] CRUCIBLE_INLINE BitMask match(int8_t h2) const {
        return BitMask{::foundation::simd::collect_equal_bits(ctrl, Bytes{h2})};
    }

    // kEmpty is the only control byte with bit 7 set, so the sign bit of each
    // byte is the same as a compare of each byte with kEmpty.
    [[nodiscard]] CRUCIBLE_INLINE BitMask match_empty() const {
        return BitMask{::foundation::simd::collect_sign_bits(ctrl)};
    }
};

}  // namespace detail
}  // namespace crucible
