// A re-exported name must resolve to the same substrate entity, and a
// re-exported primitive must still compute what the substrate one does.
// Every alias is materialised here so both claims are checked in one
// place.

#include <crucible/fixy/Struct.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace fstr = ::crucible::fixy::struct_;
namespace saf = ::crucible::safety;
namespace csat = ::crucible::sat;
namespace csmd = ::crucible::simd;

namespace {
struct CarrierPinned {};
struct CarrierNonMovable {};
}  // namespace

static_assert(std::is_same_v<fstr::Pinned<CarrierPinned>, saf::Pinned<CarrierPinned>>,
              "fixy::struct_::Pinned must alias safety::Pinned.");

static_assert(std::is_same_v<fstr::NonMovable<CarrierNonMovable>, saf::NonMovable<CarrierNonMovable>>,
              "fixy::struct_::NonMovable must alias safety::NonMovable.");

static_assert(!std::is_copy_constructible_v<fstr::Pinned<CarrierPinned>>,
              "Pinned alias preserves the substrate's deleted copy ctor.");

static_assert(!std::is_move_constructible_v<fstr::Pinned<CarrierPinned>>,
              "Pinned alias preserves the substrate's deleted move ctor.");

namespace {
struct FinalLeaf final {};
struct NonFinalLeaf {};
}  // namespace

static_assert(fstr::NotInherited<FinalLeaf>, "fixy::struct_::NotInherited accepts final classes.");

static_assert(!fstr::NotInherited<NonFinalLeaf>, "fixy::struct_::NotInherited rejects non-final classes.");

// The base must be inherited virtually, which is what stops any further
// class deriving from this one.
namespace {
class StructFinalByDerived : public virtual fstr::FinalBy<StructFinalByDerived> {
public:
    StructFinalByDerived() = default;
};
}  // namespace

static_assert(std::is_constructible_v<StructFinalByDerived>, "FinalBy<Derived> permits Derived to construct.");

static_assert(fstr::checked_add<int>(2, 3).value() == 5, "fixy::struct_::checked_add must delegate.");
static_assert(fstr::wrapping_sub<std::uint8_t>(0, 1) == 0xFFu, "fixy::struct_::wrapping_sub must wrap.");
static_assert(fstr::saturating_add<std::int8_t>(120, 100) == 127, "fixy::struct_::saturating_add must clamp.");

static_assert(fstr::safe_capacity<8u, 16u> == 128u, "fixy::struct_::safe_capacity must compute.");
static_assert(fstr::safe_mul<std::size_t, 6u, 7u> == 42u, "fixy::struct_::safe_mul must compute.");

static_assert(fstr::add_sat<std::uint8_t>(250, 250) == 255u, "fixy::struct_::add_sat must saturate.");

static_assert(fstr::bytes_fit_v<64, 32>, "fixy::struct_::bytes_fit_v must evaluate.");

static_assert(fstr::ct::select<std::uint32_t>(1u, 0xAAu, 0xBBu) == 0xAAu,
              "fixy::struct_::ct::select must select on bit 1.");
static_assert(fstr::ct::select<std::uint32_t>(0u, 0xAAu, 0xBBu) == 0xBBu,
              "fixy::struct_::ct::select must select on bit 0.");
static_assert(fstr::ct::mask_from_bit<std::uint32_t>(1u) == 0xFFFF'FFFFu,
              "fixy::struct_::ct::mask_from_bit must broadcast bit 1.");
static_assert(fstr::ct::is_zero<std::uint32_t>(0u) == 1u, "fixy::struct_::ct::is_zero must return 1 on zero.");
static_assert(fstr::ct::less<std::uint32_t>(3u, 5u) == 1u, "fixy::struct_::ct::less must return 1 on a < b.");

static_assert(std::is_same_v<fstr::simd::i64x8, csmd::i64x8>,
              "fixy::struct_::simd::i64x8 must alias crucible::simd::i64x8.");
static_assert(std::is_same_v<fstr::simd::u64x8, csmd::u64x8>,
              "fixy::struct_::simd::u64x8 must alias crucible::simd::u64x8.");

static_assert(fstr::simd::DetSafeSimd<csmd::u64x8>, "DetSafeSimd accepts integer-lane vec types.");

static_assert(fstr::simd::kSse42Available == csmd::kSse42Available, "fixy::struct_::simd::kSse42Available must alias.");

namespace {
struct OwnedRegionTag {};
}  // namespace
static_assert(std::is_same_v<fstr::OwnedRegion<int, OwnedRegionTag>, saf::OwnedRegion<int, OwnedRegionTag>>,
              "fixy::struct_::OwnedRegion must alias safety::OwnedRegion.");

static_assert(std::is_same_v<fstr::Slice<OwnedRegionTag, 0>, saf::Slice<OwnedRegionTag, 0>>,
              "fixy::struct_::Slice must alias safety::Slice.");

static_assert(std::is_same_v<fstr::WorkBudget, saf::WorkBudget>,
              "fixy::struct_::WorkBudget must alias safety::WorkBudget.");

// The remaining names are functions with no separate definition in the
// re-exporting namespace, so calling one through the alias is the whole
// of the identity claim.

int main() {
    const std::byte ba[3] = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::byte bb[3] = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::byte bc[3] = {std::byte{1}, std::byte{2}, std::byte{4}};
    // This comparison takes spans and nothing else, which removes any
    // way to pass a null pointer alongside a non-zero length.
    (void)fstr::ct::eq(std::span<const std::byte>{ba}, std::span<const std::byte>{bb});
    (void)fstr::ct::eq(std::span<const std::byte>{ba}, std::span<const std::byte>{bc});

    const auto idx = fstr::simd::iota_v<csmd::u64x8>();
    (void)idx;

    const auto budget = fstr::WorkBudget{
        .read_bytes = 128,
        .write_bytes = 128,
        .item_count = 16,
    };
    (void)fstr::should_parallelize(budget);
    return 0;
}
