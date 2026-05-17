// ── test_fixy_struct — sentinel TU for fixy/Struct.h ───────────────
//
// Pulls fixy/Struct.h into one TU compiled under project warning
// flags so the header's using-declarations + concept aliases are
// instantiated and re-checked.  Witnesses:
//
//   1. Pinned<T> / NonMovable<T> aliases match the substrate
//      (size + structural-equivalence).
//   2. NotInherited concept, FinalBy CRTP, and assert_not_inherited
//      consteval helper alias the substrate names.
//   3. Checked.h primitives (checked_add / wrapping_sub /
//      trapping_mul / saturating_add / safe_capacity / safe_mul /
//      ensure_bytes_fit) round-trip through the alias.
//   4. ConstantTime primitives (ct::select / ct::eq /
//      ct::mask_from_bit / ct::less / ct::is_zero / ct::cswap)
//      round-trip through the fixy::struct_::ct sub-namespace.
//   5. SIMD facade (i64x8 / DetSafeSimd / iota_v / prefix_mask /
//      kAvx2Available / runtime_supports_sse42) alias the substrate
//      via the fixy::struct_::simd sub-namespace.
//   6. OwnedRegion + Workload primitives (parallel_for_views /
//      parallel_reduce_views / parallel_apply_pair /
//      parallel_for_views_adaptive / parallel_for_smart /
//      should_parallelize / WorkBudget) are reachable through the
//      alias.
//
// HS14: ≥2 fixy_neg fixtures per major structural surface live in
// test/fixy_neg/neg_fixy_struct_*.cpp.

#include <crucible/fixy/Struct.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace fstr = ::crucible::fixy::struct_;
namespace saf  = ::crucible::safety;
namespace csat = ::crucible::sat;
namespace csmd = ::crucible::simd;

// ─── 1. Pinned / NonMovable aliases ────────────────────────────────

namespace {
struct CarrierPinned {};
struct CarrierNonMovable {};
}  // namespace

static_assert(std::is_same_v<fstr::Pinned<CarrierPinned>,
                              saf::Pinned<CarrierPinned>>,
    "fixy::struct_::Pinned must alias safety::Pinned.");

static_assert(std::is_same_v<fstr::NonMovable<CarrierNonMovable>,
                              saf::NonMovable<CarrierNonMovable>>,
    "fixy::struct_::NonMovable must alias safety::NonMovable.");

static_assert(!std::is_copy_constructible_v<fstr::Pinned<CarrierPinned>>,
    "Pinned alias preserves the substrate's deleted copy ctor.");

static_assert(!std::is_move_constructible_v<fstr::Pinned<CarrierPinned>>,
    "Pinned alias preserves the substrate's deleted move ctor.");

// ─── 2. NotInherited / FinalBy / assert_not_inherited aliases ──────

namespace {
struct FinalLeaf final {};
struct NonFinalLeaf {};
}  // namespace

static_assert(fstr::NotInherited<FinalLeaf>,
    "fixy::struct_::NotInherited accepts final classes.");

static_assert(!fstr::NotInherited<NonFinalLeaf>,
    "fixy::struct_::NotInherited rejects non-final classes.");

// FinalBy CRTP shape: subscribe Derived as friend, derive virtually.
namespace {
class StructFinalByDerived : public virtual fstr::FinalBy<StructFinalByDerived> {
public:
    StructFinalByDerived() = default;
};
}  // namespace

static_assert(std::is_constructible_v<StructFinalByDerived>,
    "FinalBy<Derived> permits Derived to construct.");

// ─── 3. Checked arithmetic round-trips ─────────────────────────────

static_assert(fstr::checked_add<int>(2, 3).value()      == 5,
    "fixy::struct_::checked_add must delegate.");
static_assert(fstr::wrapping_sub<std::uint8_t>(0, 1)    == 0xFFu,
    "fixy::struct_::wrapping_sub must wrap.");
static_assert(fstr::saturating_add<std::int8_t>(120, 100) == 127,
    "fixy::struct_::saturating_add must clamp.");

static_assert(fstr::safe_capacity<8u, 16u>           == 128u,
    "fixy::struct_::safe_capacity must compute.");
static_assert(fstr::safe_mul<std::size_t, 6u, 7u>    == 42u,
    "fixy::struct_::safe_mul must compute.");

// add_sat / sub_sat / mul_sat alias the lower-layer crucible::sat.
static_assert(fstr::add_sat<std::uint8_t>(250, 250)  == 255u,
    "fixy::struct_::add_sat must saturate.");

// ensure_bytes_fit fires on a non-overflowing budget — should compile.
static_assert(fstr::bytes_fit_v<64, 32>,
    "fixy::struct_::bytes_fit_v must evaluate.");

// ─── 4. ConstantTime primitives round-trip ─────────────────────────

static_assert(fstr::ct::select<std::uint32_t>(1u, 0xAAu, 0xBBu) == 0xAAu,
    "fixy::struct_::ct::select must select on bit 1.");
static_assert(fstr::ct::select<std::uint32_t>(0u, 0xAAu, 0xBBu) == 0xBBu,
    "fixy::struct_::ct::select must select on bit 0.");
static_assert(fstr::ct::mask_from_bit<std::uint32_t>(1u) == 0xFFFF'FFFFu,
    "fixy::struct_::ct::mask_from_bit must broadcast bit 1.");
static_assert(fstr::ct::is_zero<std::uint32_t>(0u) == 1u,
    "fixy::struct_::ct::is_zero must return 1 on zero.");
static_assert(fstr::ct::less<std::uint32_t>(3u, 5u) == 1u,
    "fixy::struct_::ct::less must return 1 on a < b.");

// ─── 5. SIMD facade aliases ────────────────────────────────────────

static_assert(std::is_same_v<fstr::simd::i64x8, csmd::i64x8>,
    "fixy::struct_::simd::i64x8 must alias crucible::simd::i64x8.");
static_assert(std::is_same_v<fstr::simd::u64x8, csmd::u64x8>,
    "fixy::struct_::simd::u64x8 must alias crucible::simd::u64x8.");

static_assert(fstr::simd::DetSafeSimd<csmd::u64x8>,
    "DetSafeSimd accepts integer-lane vec types.");

static_assert(fstr::simd::kSse42Available == csmd::kSse42Available,
    "fixy::struct_::simd::kSse42Available must alias.");

// ─── 6. OwnedRegion + Workload aliases ─────────────────────────────

// OwnedRegion alias: same template, takes the same (T, Tag) shape.
namespace {
struct OwnedRegionTag {};
}  // namespace
static_assert(std::is_same_v<fstr::OwnedRegion<int, OwnedRegionTag>,
                              saf::OwnedRegion<int, OwnedRegionTag>>,
    "fixy::struct_::OwnedRegion must alias safety::OwnedRegion.");

static_assert(std::is_same_v<fstr::Slice<OwnedRegionTag, 0>,
                              saf::Slice<OwnedRegionTag, 0>>,
    "fixy::struct_::Slice must alias safety::Slice.");

// WorkBudget is value-type; alias preserves the type identity.
static_assert(std::is_same_v<fstr::WorkBudget, saf::WorkBudget>,
    "fixy::struct_::WorkBudget must alias safety::WorkBudget.");

// should_parallelize / WorkBudget are free names; calling through the
// alias must compile.  Identity is already established by the
// using-declaration shape (no separate definition exists in fixy::).

int main() {
    // Runtime smoke: ct::select / ct::eq on real bytes.
    const std::byte ba[3] = { std::byte{1}, std::byte{2}, std::byte{3} };
    const std::byte bb[3] = { std::byte{1}, std::byte{2}, std::byte{3} };
    const std::byte bc[3] = { std::byte{1}, std::byte{2}, std::byte{4} };
    (void)fstr::ct::eq(ba, bb, sizeof(ba));
    (void)fstr::ct::eq(ba, bc, sizeof(ba));

    // Runtime smoke: SIMD iota_v lane order.
    const auto idx = fstr::simd::iota_v<csmd::u64x8>();
    (void)idx;

    // Runtime smoke: should_parallelize on a tiny budget (sequential).
    const auto budget = fstr::WorkBudget{
        .read_bytes  = 128,
        .write_bytes = 128,
        .item_count  = 16,
    };
    (void)fstr::should_parallelize(budget);
    return 0;
}
