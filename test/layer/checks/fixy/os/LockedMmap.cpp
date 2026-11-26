// The compile-time checks of fixy/os/LockedMmap.h.

#include <fixy/os/LockedMmap.h>

#include <foundation/Linear.h>

#include <type_traits>

namespace fixy::mmap::detail::locked_mmap_invariants {

namespace eff = ::foundation::effects;

struct ProbeRegion final {
    using permission_row = eff::Row<>;
};
struct ProbeBrand final {};

using Locked = LockedMmap<ProbeRegion, prot::WriteCopy, share::Anonymous, ProbeBrand>;

using AllRowsCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using NoPoolCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using NoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Nondet>>;

// The advice asks for the rows of the system call and for the row of the
// pool, and it is an advice that discards pages.
static_assert(CtxFitsReleaseAwareAdvise<AllRowsCtx, advice::DontNeed, ProbeRegion>);
static_assert(CtxFitsReleaseAwareAdvise<AllRowsCtx, advice::Free, ProbeRegion>);
static_assert(!CtxFitsReleaseAwareAdvise<AllRowsCtx, advice::HugePage, ProbeRegion>,
              "an advice that discards no page goes through advise, in a body of the lock.");
static_assert(!CtxFitsReleaseAwareAdvise<NoPoolCtx, advice::DontNeed, ProbeRegion>,
              "the upgrade of the pool depends on the other threads, so the context owns Nondet.");
static_assert(!CtxFitsReleaseAwareAdvise<NoBlockCtx, advice::DontNeed, ProbeRegion>,
              "the system call can park the caller, so the context owns Block.");

// A memory policy asks for the rows of mbind(2) and for an anonymous mapping,
// and it asks for no row of the pool, because a body already holds the lease.
static_assert(CtxFitsRegionPolicy<AllRowsCtx, share::Anonymous> && CtxFitsRegionPolicy<NoPoolCtx, share::Anonymous>);
static_assert(!CtxFitsRegionPolicy<NoBlockCtx, share::Anonymous>);
static_assert(!CtxFitsRegionPolicy<AllRowsCtx, share::Shared> && !CtxFitsRegionPolicy<AllRowsCtx, share::Private>,
              "the kernel keeps a policy for an anonymous private range only.");

// The object is pinned.  No code outside the mints builds one, because no
// code outside their door builds the key.
static_assert(!std::is_move_constructible_v<Locked> && !std::is_copy_constructible_v<Locked>);
static_assert(!std::is_move_assignable_v<Locked> && !std::is_copy_assignable_v<Locked>);
static_assert(!std::is_default_constructible_v<Locked>);
static_assert(!std::is_default_constructible_v<LockedMmapKey> && !std::is_copy_constructible_v<LockedMmapKey>);

// The lock is the private base, so no code reads the pool or the region
// through it.
using Region = LockedRegion<ProbeRegion, prot::WriteCopy, share::Anonymous, ProbeBrand>;
using Lock = SharedLocked<Region, ProbeRegion, ProbeBrand>;
static_assert(!std::is_convertible_v<Locked*, Lock*>);

// No body moves the region out of the lock, and no body assigns to it, also
// from a mapping of its own type.
using Mapping = OwnedMmap<ProbeRegion, prot::WriteCopy, share::Anonymous, ProbeBrand>;
static_assert(!std::is_move_constructible_v<Region> && !std::is_move_assignable_v<Region>);
static_assert(!std::is_copy_constructible_v<Region> && !std::is_copy_assignable_v<Region>);
static_assert(!std::is_assignable_v<Region&, Mapping&&> && !std::is_constructible_v<Region, Mapping&&>);
static_assert(!std::is_convertible_v<Region&, Mapping&> && !std::is_convertible_v<Region const&, Mapping const&>);

// A reader gets a const region, which gives no View of elements that it can
// write.  A region that the kernel mapped read-only gives none to a writer.
template <typename Held, typename T>
concept GivesView = requires(Held& region) { region.template view<T>(); };
template <typename Held, typename T>
concept GivesRange = requires(Held& region, ::foundation::core::ByteCount offset,
                              ::foundation::core::ElementCount count) { region.template view_at<T>(offset, count); };
template <typename Held, typename T>
concept GivesFixedRange =
    requires(Held& region, ::foundation::core::ByteCount offset) { region.template view_at<T, 2>(offset); };
template <typename Held, typename T>
constexpr int kViewDoors = int{GivesView<Held, T>} + int{GivesRange<Held, T>} + int{GivesFixedRange<Held, T>};
using ReadOnlyRegion = LockedRegion<ProbeRegion, prot::ReadOnly, share::Private, ProbeBrand>;
static_assert(kViewDoors<Region, int> == 3 && kViewDoors<Region, int const> == 3);
static_assert(kViewDoors<Region const, int> == 0 && kViewDoors<Region const, int const> == 3);
static_assert(kViewDoors<ReadOnlyRegion, int> == 0 && kViewDoors<ReadOnlyRegion, int const> == 3);
static_assert(kViewDoors<ReadOnlyRegion const, int> == 0 && kViewDoors<ReadOnlyRegion const, int const> == 3);

// The end of a lock gives the mapping and the exclusive, so its result is
// linear.  It asks for the row of the pool alone, because it makes no system
// call, and a const object has no end.
template <typename Held, typename Ctx>
concept Ends = requires(Held& held, Ctx const& ctx) { held.end_lock(ctx); };
static_assert(Ends<Locked, AllRowsCtx> && Ends<Locked, NoBlockCtx>);
static_assert(!Ends<Locked, NoPoolCtx> && !Ends<Locked const, AllRowsCtx>);
static_assert(::foundation::linear::is_linear_type(^^Locked::ended_type));

// The int of a C library is no descriptor of a map until the caller names it
// one, and a descriptor gives no int by conversion.
static_assert(!std::is_convertible_v<int, ForeignDescriptor> && !std::is_convertible_v<ForeignDescriptor, int>);
static_assert(!ForeignDescriptor{-1}.is_open() && ForeignDescriptor{0}.is_open());

// The region gives its mapping up only under the key of the end of its own
// lock.  No code outside SharedLocked builds the key, and the key of another T
// is no key of the region.
static_assert(LeavesLock<Region> && std::is_same_v<left_lock_t<Region>, Linear<Mapping>>);
static_assert(!LeavesLock<int> && !LeavesLock<Mapping>);

// The value that leave_lock gives holds no borrow: a mapping owns its pages,
// so the region passes above, and a T that gives a View of what it keeps has
// no end of its lock.
struct LeavesAView final {
    ::foundation::core::View<int const> kept{};
    [[nodiscard]] ::foundation::core::View<int const> leave_lock(SharedLockEndKey<LeavesAView> const&) noexcept {
        return kept;
    }
};
static_assert(!LeavesLock<LeavesAView>);
using EndKey = SharedLockEndKey<Region>;
static_assert(!std::is_default_constructible_v<EndKey> && !std::is_copy_constructible_v<EndKey>);
static_assert(!std::is_convertible_v<SharedLockEndKey<ReadOnlyRegion> const&, EndKey const&>);

// The refusal of a map holds the exclusive, so it is linear: a caller that
// drops it gets an error.
static_assert(::foundation::linear::is_linear_type(^^MapRefusal<ProbeRegion, ProbeBrand>));
static_assert(::foundation::core::ErrorValue<MapRefusal<ProbeRegion, ProbeBrand>>);
using ForeignRefusal = ForeignMapRefusal<ProbeRegion, prot::ReadWrite, share::Shared, ProbeBrand>;
static_assert(::foundation::linear::is_linear_type(^^ForeignRefusal));
static_assert(::foundation::core::ErrorValue<ForeignRefusal>);
static_assert(::foundation::core::ErrorValue<ReleaseRefusal>);

}  // namespace fixy::mmap::detail::locked_mmap_invariants
