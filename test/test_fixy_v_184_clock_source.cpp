// A composite lattice: the product of the determinism, suspend-behaviour and
// pinning axes, together with the value-level clock sources and the
// many-to-one projection of a source onto a point of that product.
//
// Two properties carry the file. Every source projects to the tuple it is
// documented to project to. And the order really is a product, taken
// pointwise, rather than a chain: there are incomparable points, and one pair
// is ordered by the pinning axis alone.
//
// A composite carries no federation salt of its own, so nothing here asserts
// anything about row hashing. That discrimination happens through the per-axis
// wrappers instead.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ClockSourceLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;

namespace {

using cal::ClockSource;
using cal::DetSafeTier;
using cal::PinningRequirement;
using cal::SuspendBehavior;
using L = cal::ClockSourceLattice;

static_assert(crucible::algebra::Lattice<L>, "ClockSourceLattice must satisfy the Lattice concept.");
static_assert(crucible::algebra::BoundedLattice<L>, "the product of three bounded chains is itself a bounded lattice.");
static_assert(crucible::algebra::BoundedBelowLattice<L>);
static_assert(crucible::algebra::BoundedAboveLattice<L>);
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>);

static_assert(cal::clock_source_count == 10, "ClockSource must have exactly 10 enumerators. The enumeration is "
                                             "append-only and existing positions never renumber.");
static_assert(std::is_same_v<std::underlying_type_t<ClockSource>, std::uint8_t>,
              "ClockSource uses a uint8_t underlying type. Its ordinal is "
              "declaration order and carries no order semantics.");

static_assert(L::arity == 3);
static_assert(std::is_same_v<L::nth_lattice<0>, cal::DetSafeLattice>);
static_assert(std::is_same_v<L::nth_lattice<1>, cal::SuspendBehaviorLattice>);
static_assert(std::is_same_v<L::nth_lattice<2>, cal::PinningRequirementLattice>);
static_assert(std::is_same_v<L::det_safe_axis, cal::DetSafeLattice>);
static_assert(std::is_same_v<L::suspend_axis, cal::SuspendBehaviorLattice>);
static_assert(std::is_same_v<L::pinning_axis, cal::PinningRequirementLattice>);

// The bounds are the component endpoints lifted pointwise.
static_assert(L::get<0>(L::bottom()) == DetSafeTier::NonDeterministicSyscall);
static_assert(L::get<1>(L::bottom()) == SuspendBehavior::Unknown);
static_assert(L::get<2>(L::bottom()) == PinningRequirement::NotRequired);
static_assert(L::get<0>(L::top()) == DetSafeTier::Pure);
static_assert(L::get<1>(L::top()) == SuspendBehavior::KeepsTicking);
static_assert(L::get<2>(L::top()) == PinningRequirement::CrossSocketSafe);

[[nodiscard]] consteval bool projects_to(ClockSource source, DetSafeTier det, SuspendBehavior suspend,
                                         PinningRequirement pin) noexcept {
    auto point = cal::clock_source_project(source);
    return L::get<0>(point) == det && L::get<1>(point) == suspend && L::get<2>(point) == pin;
}

static_assert(projects_to(ClockSource::Realtime, DetSafeTier::WallClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired),
              "Realtime");
static_assert(projects_to(ClockSource::Monotonic, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::MonotonicRaw, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::Boot, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::NotRequired),
              "Boot");
static_assert(projects_to(ClockSource::ThreadCpu, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::ProcessCpu, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::TscRaw, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::PerCore),
              "TscRaw");
static_assert(projects_to(ClockSource::TscSerialized, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::PerCore));
static_assert(projects_to(ClockSource::PmuCounter, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::PerCore));
// A per-NIC silicon clock: unaffected by suspend and needing no CPU pin, so
// it projects onto the same tuple as the boot clock. The two sources stay
// distinct one layer up, where the cache key is formed.
static_assert(projects_to(ClockSource::PtpHwClock, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::NotRequired),
              "PtpHwClock must project to (MonotonicClockRead, KeepsTicking, "
              "NotRequired), the same tuple as Boot.");
static_assert(crucible::algebra::equivalent<L>(cal::clock_source_project(ClockSource::Boot),
                                               cal::clock_source_project(ClockSource::PtpHwClock)),
              "Boot and PtpHwClock collapse to the same projected tuple. Both are "
              "monotonic, both keep ticking across suspend, and both are read through "
              "a descriptor. Their identities stay distinct at the cache key.");

// Boot is strictly below TscRaw, and the pinning axis alone is what orders
// them.
static_assert(L::leq(cal::clock_source_project(ClockSource::Boot), cal::clock_source_project(ClockSource::TscRaw)));
static_assert(!L::leq(cal::clock_source_project(ClockSource::TscRaw), cal::clock_source_project(ClockSource::Boot)),
              "TscRaw is not below Boot, because PerCore is not below NotRequired.");
static_assert(L::leq(cal::clock_source_project(ClockSource::Realtime), cal::clock_source_project(ClockSource::Boot)));
static_assert(L::leq(cal::clock_source_project(ClockSource::Monotonic), cal::clock_source_project(ClockSource::Boot)));

// Neither point is below the other. A chain admits no such pair, so this is
// what makes the order a genuine product.
static_assert(!L::leq(L::make_point(DetSafeTier::Pure, SuspendBehavior::Unknown, PinningRequirement::CrossSocketSafe),
                      L::make_point(DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
                                    PinningRequirement::NotRequired)),
              "these two points are incomparable, which a chain cannot produce.");
static_assert(!L::leq(L::make_point(DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
                                    PinningRequirement::NotRequired),
                      L::make_point(DetSafeTier::Pure, SuspendBehavior::Unknown, PinningRequirement::CrossSocketSafe)));

static_assert(L::get<1>(L::join(cal::clock_source_project(ClockSource::Boot),
                                cal::clock_source_project(ClockSource::Realtime)))
              == SuspendBehavior::KeepsTicking);
static_assert(L::get<2>(L::meet(cal::clock_source_project(ClockSource::TscRaw),
                                cal::clock_source_project(ClockSource::Boot)))
              == PinningRequirement::NotRequired);

static_assert(crucible::algebra::equivalent<L>(cal::clock_source_project(ClockSource::Monotonic),
                                               cal::clock_source_project(ClockSource::MonotonicRaw)),
              "Monotonic and MonotonicRaw differ only in whether time is slewed, "
              "which this lattice does not model, so they project to one point. "
              "Their identities stay distinct at the cache key.");

static_assert(L::name() == std::string_view{"ClockSourceLattice"});
static_assert(cal::clock_source_name(ClockSource::TscRaw) == std::string_view{"TscRaw"});
static_assert(cal::clock_source_name(ClockSource::Boot) == std::string_view{"Boot"});

// Unlike a singleton grade, this one is three bytes wide, so a graded value
// over this lattice is genuinely larger than its payload.
struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, L, EightByteValue>)
                  <= sizeof(EightByteValue) + 8,
              "the three-byte grade plus alignment padding must fit in 8 trailing "
              "bytes over an 8-byte payload.");

}  // namespace

int main() {
    cal::detail::clock_source_lattice_self_test::runtime_smoke_test();
    return 0;
}
