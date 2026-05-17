// ── test_fixy_wrap — sentinel TU for fixy/Wrap.h ───────────────────
//
// Pulls fixy/Wrap.h into one TU compiled under project warning flags
// so every alias / using-declaration is materialised and every
// embedded static_assert in the substrate headers executes here.
//
// Witnesses:
//
//   1. Every Graded-backed wrapper aliased via fixy::wrap::* is the
//      same type as the safety:: substrate.
//   2. Every Mutation.h derivative wrapper aliased via fixy::wrap::*
//      is the same type as the safety:: substrate.
//   3. Zero-cost: sizeof(fixy::wrap::X<...>) == sizeof(safety::X<...>)
//      for every wrapper (Graded + Mutation derivatives).
//   4. Mints re-exported (mint_linear / mint_secret /
//      mint_permission_share) round-trip via the fixy::wrap alias.
//   5. Named predicates / refinement aliases (NonNull, Positive,
//      InRange, BoundedAbove, Aligned, LengthGe, positive,
//      power_of_two, non_zero, non_empty) reach through the alias.
//
// HS14: companion neg-compile fixtures live in test/fixy_neg/
// neg_fixy_wrap_*.cpp.

#include <crucible/fixy/Wrap.h>
#include <crucible/permissions/Permission.h>  // SharedPermissionPool for mint_permission_share

#include <cstdint>
#include <type_traits>
#include <utility>

namespace fw  = ::crucible::fixy::wrap;
namespace saf = ::crucible::safety;

// ─── 1. Graded-backed wrapper alias identity ─────────────────────

static_assert(std::is_same_v<fw::Linear<int>, saf::Linear<int>>);
static_assert(std::is_same_v<fw::Refined<saf::positive, int>,
                             saf::Refined<saf::positive, int>>);
static_assert(std::is_same_v<fw::SealedRefined<saf::positive, int>,
                             saf::SealedRefined<saf::positive, int>>);
static_assert(std::is_same_v<fw::Tagged<int, saf::source::FromUser>,
                             saf::Tagged<int, saf::source::FromUser>>);
static_assert(std::is_same_v<fw::Secret<int>, saf::Secret<int>>);
static_assert(std::is_same_v<fw::Monotonic<std::uint32_t>,
                             saf::Monotonic<std::uint32_t>>);
static_assert(std::is_same_v<fw::AppendOnly<int>, saf::AppendOnly<int>>);
static_assert(std::is_same_v<fw::Stale<int>, saf::Stale<int>>);
static_assert(std::is_same_v<fw::TimeOrdered<int, 1>,
                             saf::TimeOrdered<int, 1>>);

// SharedPermission alias identity — same tag, same wrapper.
struct WrapTestTag {};
static_assert(std::is_same_v<fw::SharedPermission<WrapTestTag>,
                             saf::SharedPermission<WrapTestTag>>);

// ─── 2. Mutation.h derivative wrapper alias identity ─────────────

static_assert(std::is_same_v<fw::WriteOnce<int>, saf::WriteOnce<int>>);
static_assert(std::is_same_v<fw::WriteOnceNonNull<int*>,
                             saf::WriteOnceNonNull<int*>>);
static_assert(std::is_same_v<fw::BoundedMonotonic<std::uint32_t, 1024U>,
                             saf::BoundedMonotonic<std::uint32_t, 1024U>>);
static_assert(std::is_same_v<fw::OrderedAppendOnly<int>,
                             saf::OrderedAppendOnly<int>>);
static_assert(std::is_same_v<fw::AtomicMonotonic<std::uint64_t>,
                             saf::AtomicMonotonic<std::uint64_t>>);

// ─── 3. Zero-cost guarantee ──────────────────────────────────────

static_assert(sizeof(fw::Linear<int>)              == sizeof(int));
static_assert(sizeof(fw::Refined<saf::positive, int>)        == sizeof(int));
static_assert(sizeof(fw::SealedRefined<saf::positive, int>)  == sizeof(int));
static_assert(sizeof(fw::Tagged<int, saf::source::FromUser>) == sizeof(int));
static_assert(sizeof(fw::Secret<int>)              == sizeof(int));
static_assert(sizeof(fw::Monotonic<std::uint32_t>) == sizeof(std::uint32_t));
static_assert(sizeof(fw::BoundedMonotonic<std::uint32_t, 1024U>)
                  == sizeof(std::uint32_t));
static_assert(sizeof(fw::WriteOnceNonNull<int*>)  == sizeof(int*));

// ─── 4. Named predicate / refinement aliases reachable ───────────

static_assert(std::is_same_v<fw::NonNull<int*>, saf::NonNull<int*>>);
static_assert(std::is_same_v<fw::Positive<int>, saf::Positive<int>>);
static_assert(std::is_same_v<fw::NonNegative<int>, saf::NonNegative<int>>);
static_assert(std::is_same_v<fw::PowerOfTwo<std::size_t>,
                             saf::PowerOfTwo<std::size_t>>);
static_assert(std::is_same_v<fw::LinearRefined<saf::positive, int>,
                             saf::LinearRefined<saf::positive, int>>);
static_assert(std::is_same_v<fw::RefinedLinear<saf::positive, int>,
                             saf::RefinedLinear<saf::positive, int>>);

// Parameterised predicate templates resolve through the alias.
static_assert(std::is_same_v<fw::Aligned<64>, saf::Aligned<64>>);
static_assert(std::is_same_v<fw::InRange<0, 100>, saf::InRange<0, 100>>);
static_assert(std::is_same_v<fw::BoundedAbove<1024U>,
                             saf::BoundedAbove<1024U>>);
static_assert(std::is_same_v<fw::LengthGe<1>, saf::LengthGe<1>>);

// Implication trait is reachable.
static_assert(fw::implies_v<saf::positive, saf::non_negative>);
static_assert(fw::implies_v<saf::positive, saf::non_zero>);

// ─── 5. Mints round-trip via fixy::wrap ──────────────────────────

namespace {

// Permission tag for mint_permission_share round-trip.  Default
// permission_row<Tag> specialization yields Row<>, which the bare
// ctx-less mint_permission_share overload accepts.
struct WrapMintTag {};

}  // namespace

int main() {
    // Linear mint via fixy::wrap.
    auto lin = fw::mint_linear<int>(42);
    fw::drop(std::move(lin));

    // Secret mint via fixy::wrap.
    auto sec = fw::mint_secret<int>(7);
    (void)sec;

    // Monotonic + bump via fixy::wrap.
    fw::Monotonic<std::uint32_t> mono{0};
    mono.bump();
    if (mono.get() != 1U) return 1;

    // BoundedMonotonic guards the bound via fixy::wrap.
    fw::BoundedMonotonic<std::uint32_t, 4U> bm{0};
    bm.advance(3U);
    if (bm.get() != 3U) return 2;

    // AppendOnly grows via fixy::wrap.
    fw::AppendOnly<int> log{};
    log.emplace(1);
    log.emplace(2);
    if (log.size() != 2U) return 3;

    // WriteOnce + WriteOnceNonNull through fixy::wrap.
    fw::WriteOnce<int> wo{};
    wo.set(11);
    if (!wo.has_value() || wo.get() != 11) return 4;

    int storage = 99;
    fw::WriteOnceNonNull<int*> won{};
    won.set(&storage);
    if (won.get() != &storage) return 5;

    // Refined construction (predicate fires at boundary).
    fw::Refined<saf::positive, int> r{1};
    if (r.value() != 1) return 6;

    // SealedRefined construction.
    fw::SealedRefined<saf::positive, int> sr{2};
    if (sr.value() != 2) return 7;

    // Tagged round-trip.
    fw::Tagged<int, saf::source::FromUser> tag{3};
    if (tag.value() != 3) return 8;

    // Stale at_origin / Stale::fresh.
    fw::Stale<int> stale = fw::Stale<int>::fresh(5);
    if (stale.peek() != 5) return 9;

    // TimeOrdered at_origin.
    fw::TimeOrdered<int, 1> tord = fw::TimeOrdered<int, 1>::at_origin(8);
    if (tord.peek() != 8) return 10;

    // AtomicMonotonic load / advance via fixy::wrap.
    fw::AtomicMonotonic<std::uint64_t> am{0};
    if (am.get() != 0U) return 11;

    // mint_permission_share through fixy::wrap: convert an exclusive
    // root token into a fractional carrier.  Authority chain: root
    // mint → share → drop via end-of-scope.  Row<> tag, so the
    // bare ctx-less overload applies.
    auto root  = saf::mint_permission_root<WrapMintTag>();
    auto frac  = fw::mint_permission_share<WrapMintTag>(std::move(root));
    (void)frac;

    return 0;
}
