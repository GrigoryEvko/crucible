// A re-exported wrapper must be the same type as the substrate wrapper,
// and must still cost nothing over the value it wraps.  Every alias is
// materialised here so that both claims are checked in one place.

#include <crucible/fixy/Wrap.h>
#include <crucible/permissions/_Permission.h>  // the pool behind the share mint

#include <cstdint>
#include <type_traits>
#include <utility>

namespace fw = ::crucible::fixy::wrap;
namespace saf = ::crucible::safety;

static_assert(std::is_same_v<fw::Linear<int>, saf::Linear<int>>);
static_assert(std::is_same_v<fw::Refined<saf::positive, int>, saf::Refined<saf::positive, int>>);
static_assert(std::is_same_v<fw::SealedRefined<saf::positive, int>, saf::SealedRefined<saf::positive, int>>);
static_assert(std::is_same_v<fw::Tagged<int, saf::source::FromUser>, saf::Tagged<int, saf::source::FromUser>>);
static_assert(std::is_same_v<fw::Secret<int>, saf::Secret<int>>);
static_assert(std::is_same_v<fw::Monotonic<std::uint32_t>, saf::Monotonic<std::uint32_t>>);
static_assert(std::is_same_v<fw::AppendOnly<int>, saf::AppendOnly<int>>);
static_assert(std::is_same_v<fw::Stale<int>, saf::Stale<int>>);
static_assert(std::is_same_v<fw::TimeOrdered<int, 1>, saf::TimeOrdered<int, 1>>);

struct WrapTestTag {};
static_assert(std::is_same_v<fw::SharedPermission<WrapTestTag>, saf::SharedPermission<WrapTestTag>>);

static_assert(std::is_same_v<fw::WriteOnce<int>, saf::WriteOnce<int>>);
static_assert(std::is_same_v<fw::WriteOnceNonNull<int*>, saf::WriteOnceNonNull<int*>>);
static_assert(std::is_same_v<fw::BoundedMonotonic<std::uint32_t, 1024U>, saf::BoundedMonotonic<std::uint32_t, 1024U>>);
static_assert(std::is_same_v<fw::OrderedAppendOnly<int>, saf::OrderedAppendOnly<int>>);
static_assert(std::is_same_v<fw::AtomicMonotonic<std::uint64_t>, saf::AtomicMonotonic<std::uint64_t>>);

static_assert(sizeof(fw::Linear<int>) == sizeof(int));
static_assert(sizeof(fw::Refined<saf::positive, int>) == sizeof(int));
static_assert(sizeof(fw::SealedRefined<saf::positive, int>) == sizeof(int));
static_assert(sizeof(fw::Tagged<int, saf::source::FromUser>) == sizeof(int));
static_assert(sizeof(fw::Secret<int>) == sizeof(int));
static_assert(sizeof(fw::Monotonic<std::uint32_t>) == sizeof(std::uint32_t));
static_assert(sizeof(fw::BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t));
static_assert(sizeof(fw::WriteOnceNonNull<int*>) == sizeof(int*));

static_assert(std::is_same_v<fw::NonNull<int*>, saf::NonNull<int*>>);
static_assert(std::is_same_v<fw::Positive<int>, saf::Positive<int>>);
static_assert(std::is_same_v<fw::NonNegative<int>, saf::NonNegative<int>>);
static_assert(std::is_same_v<fw::PowerOfTwo<std::size_t>, saf::PowerOfTwo<std::size_t>>);
static_assert(std::is_same_v<fw::LinearRefined<saf::positive, int>, saf::LinearRefined<saf::positive, int>>);
static_assert(std::is_same_v<fw::RefinedLinear<saf::positive, int>, saf::RefinedLinear<saf::positive, int>>);

static_assert(std::is_same_v<fw::Aligned<64>, saf::Aligned<64>>);
static_assert(std::is_same_v<fw::InRange<0, 100>, saf::InRange<0, 100>>);
static_assert(std::is_same_v<fw::BoundedAbove<1024U>, saf::BoundedAbove<1024U>>);
static_assert(std::is_same_v<fw::LengthGe<1>, saf::LengthGe<1>>);

static_assert(fw::implies_v<saf::positive, saf::non_negative>);
static_assert(fw::implies_v<saf::positive, saf::non_zero>);

namespace {

// This tag carries the default, empty effect row, which is what lets the
// share mint below be called without a context.
struct WrapMintTag {};

}  // namespace

int main() {
    auto lin = fw::mint_linear<int>(42);
    fw::drop(std::move(lin));

    auto sec = fw::mint_secret<int>(7);
    (void)sec;

    fw::Monotonic<std::uint32_t> mono{0};
    mono.bump();
    if (mono.get() != 1U) return 1;

    fw::BoundedMonotonic<std::uint32_t, 4U> bm{0};
    bm.advance(3U);
    if (bm.get() != 3U) return 2;

    fw::AppendOnly<int> log{};
    log.emplace(1);
    log.emplace(2);
    if (log.size() != 2U) return 3;

    fw::WriteOnce<int> wo{};
    wo.set(11);
    if (!wo.has_value() || wo.get() != 11) return 4;

    int storage = 99;
    fw::WriteOnceNonNull<int*> won{};
    won.set(&storage);
    if (won.get() != &storage) return 5;

    fw::Refined<saf::positive, int> r{1};
    if (r.value() != 1) return 6;

    fw::SealedRefined<saf::positive, int> sr{2};
    if (sr.value() != 2) return 7;

    fw::Tagged<int, saf::source::FromUser> tag{3};
    if (tag.value() != 3) return 8;

    fw::Stale<int> stale = fw::Stale<int>::fresh(5);
    if (stale.peek() != 5) return 9;

    fw::TimeOrdered<int, 1> tord = fw::TimeOrdered<int, 1>::at_origin(8);
    if (tord.peek() != 8) return 10;

    fw::AtomicMonotonic<std::uint64_t> am{0};
    if (am.get() != 0U) return 11;

    auto root = saf::mint_permission_root<WrapMintTag>();
    auto frac = fw::mint_permission_share<WrapMintTag>(std::move(root));
    (void)frac;

    return 0;
}
