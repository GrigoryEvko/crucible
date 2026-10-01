// The compile-time checks of foundation/algebra/lattices/ClockSourceLattice.h.

#include <foundation/algebra/lattices/ClockSourceLattice.h>

namespace foundation::algebra::lattices {

namespace detail::clock_source_lattice_self_test {

static_assert(::foundation::reflect::enum_count<ClockSource> == 10,
              "The ClockSource catalog changed size.  A new source needs an arm in clock_source_project() and "
              "this count bumped; existing ordinals never renumber.");

static_assert(Lattice<ClockSourceLattice>, "ClockSourceLattice must satisfy the Lattice concept "
                                           "(element_type + leq + join + meet) — inherited from the 3-ary "
                                           "ProductLattice primary.");
static_assert(BoundedLattice<ClockSourceLattice>, "every component (DetSafe/Suspend/Pinning) is a bounded "
                                                  "chain, so the product has bottom() and top().");
static_assert(BoundedBelowLattice<ClockSourceLattice>);
static_assert(BoundedAboveLattice<ClockSourceLattice>);
static_assert(!UnboundedLattice<ClockSourceLattice>);
static_assert(!Semiring<ClockSourceLattice>, "ClockSourceLattice carries only order-theoretic ops, "
                                             "not the equality+add+mul of a semiring.");

static_assert(ClockSourceLattice::arity == 3);
static_assert(std::is_same_v<ClockSourceLattice::nth_lattice<0>, DetSafeLattice>);
static_assert(std::is_same_v<ClockSourceLattice::nth_lattice<1>, SuspendBehaviorLattice>);
static_assert(std::is_same_v<ClockSourceLattice::nth_lattice<2>, PinningRequirementLattice>);
static_assert(std::is_same_v<ClockSourceLattice::det_safe_axis, DetSafeLattice>);
static_assert(std::is_same_v<ClockSourceLattice::suspend_axis, SuspendBehaviorLattice>);
static_assert(std::is_same_v<ClockSourceLattice::pinning_axis, PinningRequirementLattice>);

static_assert(ClockSourceLattice::get<0>(ClockSourceLattice::bottom()) == DetSafeTier::NonDeterministicSyscall);
static_assert(ClockSourceLattice::get<1>(ClockSourceLattice::bottom()) == SuspendBehavior::Unknown);
static_assert(ClockSourceLattice::get<2>(ClockSourceLattice::bottom()) == PinningRequirement::NotRequired);
static_assert(ClockSourceLattice::get<0>(ClockSourceLattice::top()) == DetSafeTier::Pure);
static_assert(ClockSourceLattice::get<1>(ClockSourceLattice::top()) == SuspendBehavior::KeepsTicking);
static_assert(ClockSourceLattice::get<2>(ClockSourceLattice::top()) == PinningRequirement::CrossSocketSafe);

// A wrong cell in the projection silently mistypes a clock read, which is the
// failure this lattice exists to prevent, so every source is pinned.
[[nodiscard]] consteval bool projects_to(ClockSource source, DetSafeTier det, SuspendBehavior suspend,
                                         PinningRequirement pin) noexcept {
    auto point = clock_source_project(source);
    return ClockSourceLattice::get<0>(point) == det && ClockSourceLattice::get<1>(point) == suspend
        && ClockSourceLattice::get<2>(point) == pin;
}

static_assert(projects_to(ClockSource::Realtime, DetSafeTier::WallClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired),
              "Realtime must project to (WallClockRead, PausesOnSuspend, NotRequired).");
static_assert(projects_to(ClockSource::Monotonic, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::MonotonicRaw, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::Boot, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::NotRequired),
              "Boot must project to (MonotonicClockRead, KeepsTicking, NotRequired).");
static_assert(projects_to(ClockSource::ThreadCpu, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::ProcessCpu, DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                          PinningRequirement::NotRequired));
static_assert(projects_to(ClockSource::TscRaw, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::PerCore),
              "TscRaw must project to (MonotonicClockRead, KeepsTicking, PerCore).");
static_assert(projects_to(ClockSource::TscSerialized, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::PerCore));
static_assert(projects_to(ClockSource::PmuCounter, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::PerCore));
static_assert(projects_to(ClockSource::PtpHwClock, DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                          PinningRequirement::NotRequired),
              "PtpHwClock must project to (MonotonicClockRead, KeepsTicking, "
              "NotRequired) — the same point as Boot, under a distinct source identity.");

static_assert(ClockSourceLattice::leq(clock_source_project(ClockSource::Boot),
                                      clock_source_project(ClockSource::TscRaw)),
              "Boot must sit below TscRaw — the pinning axis is the only one that "
              "differs, and PerCore subsumes NotRequired.");
static_assert(!ClockSourceLattice::leq(clock_source_project(ClockSource::TscRaw),
                                       clock_source_project(ClockSource::Boot)),
              "TscRaw must not sit below Boot — the descending direction is false.");
static_assert(ClockSourceLattice::leq(clock_source_project(ClockSource::Realtime),
                                      clock_source_project(ClockSource::Boot)));
static_assert(!ClockSourceLattice::leq(clock_source_project(ClockSource::Boot),
                                       clock_source_project(ClockSource::Realtime)));
static_assert(ClockSourceLattice::leq(clock_source_project(ClockSource::Monotonic),
                                      clock_source_project(ClockSource::Boot)));
static_assert(!ClockSourceLattice::leq(clock_source_project(ClockSource::Boot),
                                       clock_source_project(ClockSource::Monotonic)));

// These two points lead on opposite axes, so neither sits below the other.  A
// chain admits no such pair, which is what distinguishes this from one.
static_assert(!ClockSourceLattice::leq(ClockSourceLattice::make_point(DetSafeTier::Pure, SuspendBehavior::Unknown,
                                                                      PinningRequirement::CrossSocketSafe),
                                       ClockSourceLattice::make_point(DetSafeTier::NonDeterministicSyscall,
                                                                      SuspendBehavior::KeepsTicking,
                                                                      PinningRequirement::NotRequired)),
              "The product must not behave as a chain — the left point leads on the "
              "determinism and pinning axes, the right on the suspend axis, so the "
              "two are incomparable.");
static_assert(!ClockSourceLattice::leq(
    ClockSourceLattice::make_point(DetSafeTier::NonDeterministicSyscall, SuspendBehavior::KeepsTicking,
                                   PinningRequirement::NotRequired),
    ClockSourceLattice::make_point(DetSafeTier::Pure, SuspendBehavior::Unknown, PinningRequirement::CrossSocketSafe)));

static_assert(ClockSourceLattice::get<0>(ClockSourceLattice::join(clock_source_project(ClockSource::Boot),
                                                                  clock_source_project(ClockSource::Realtime)))
              == DetSafeTier::MonotonicClockRead);
static_assert(ClockSourceLattice::get<1>(ClockSourceLattice::join(clock_source_project(ClockSource::Boot),
                                                                  clock_source_project(ClockSource::Realtime)))
              == SuspendBehavior::KeepsTicking);
static_assert(ClockSourceLattice::get<2>(ClockSourceLattice::meet(clock_source_project(ClockSource::TscRaw),
                                                                  clock_source_project(ClockSource::Boot)))
              == PinningRequirement::NotRequired);

// Each component is a chain, which is distributive, and a product of
// distributive lattices is distributive.
static_assert(verify_bounded_lattice_axioms_at<ClockSourceLattice>(clock_source_project(ClockSource::Realtime),
                                                                   clock_source_project(ClockSource::Boot),
                                                                   clock_source_project(ClockSource::TscRaw)));
static_assert(verify_bounded_lattice_axioms_at<ClockSourceLattice>(ClockSourceLattice::bottom(),
                                                                   clock_source_project(ClockSource::Monotonic),
                                                                   ClockSourceLattice::top()));
static_assert(verify_distributive_lattice<ClockSourceLattice>(clock_source_project(ClockSource::Realtime),
                                                              clock_source_project(ClockSource::Boot),
                                                              clock_source_project(ClockSource::TscRaw)));

static_assert(subsumes<ClockSourceLattice>(clock_source_project(ClockSource::Boot),
                                           clock_source_project(ClockSource::TscRaw)));
static_assert(strictly_less<ClockSourceLattice>(clock_source_project(ClockSource::Boot),
                                                clock_source_project(ClockSource::TscRaw)));
static_assert(equivalent<ClockSourceLattice>(clock_source_project(ClockSource::Monotonic),
                                             clock_source_project(ClockSource::MonotonicRaw)),
              "Monotonic and MonotonicRaw must project to the same point.  They "
              "differ only in whether a time daemon slews them, which no axis here "
              "models; their identities stay distinct one layer up.");

static_assert(ClockSourceLattice::name() == std::string_view{"ClockSourceLattice"},
              "name() must return this composite's own name, not the generic one "
              "inherited from the product.");

// Each axis puts the stronger guarantee higher, and the product puts the
// stronger claim higher too.  A point stored beside a value goes through
// the order dual.
static_assert(claim_orientation_v<ClockSourceLattice> == ClaimOrientation::stronger_is_higher);
static_assert(!GradableLattice<ClockSourceLattice> && GradableLattice<DualLattice<ClockSourceLattice>>);

// The product point occupies three bytes, one per axis, so a carrier over it
// grows by that much plus padding and the exact-size invariant does not apply.
// The bound is asserted by hand instead.
struct EightByteValue {
    unsigned long long v{0};
};

static_assert(sizeof(ClockGraded<int>) <= sizeof(int) + 4,
              "ClockGraded<int> exceeded sizeof(int) + 4 — the three-byte grade "
              "plus at most one byte of alignment padding must fit in four trailing "
              "bytes.");
static_assert(sizeof(ClockGraded<EightByteValue>) <= sizeof(EightByteValue) + 8,
              "ClockGraded<EightByteValue> exceeded sizeof + 8 — the three-byte "
              "grade plus at most five bytes of alignment padding must fit in eight "
              "trailing bytes.");

// At<Source> carries the source as a template argument and holds nothing, so a
// carrier graded on it costs exactly the payload.  The walk pins the shape of
// every At<Source>: a bounded lattice with an empty element that converts
// back to its source and carries a reflected name.
static_assert(verify_pinned_at<ClockSourceLattice, ClockSource>(),
              "ClockSourceLattice::At<Source>: a pinned grade lost its emptiness, "
              "its conversion back to Source, or its reflected name.");
static_assert(ClockSourceLattice::At<ClockSource::Boot>::source == ClockSource::Boot);
static_assert(ClockSourceLattice::At<ClockSource::Realtime>::source == ClockSource::Realtime);
static_assert(ClockSourceLattice::At<ClockSource::TscRaw>::name()
              == std::string_view{"ClockSourceLattice::At<TscRaw>"});
static_assert(ClockSourceLattice::At<static_cast<ClockSource>(200)>::name() == "ClockSourceLattice::At<?>");

static_assert(sizeof(foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute,
                                                 ClockSourceLattice::At<ClockSource::Boot>, EightByteValue>)
                  == sizeof(EightByteValue),
              "A carrier graded on At<Boot> must add no bytes to an eight-byte "
              "payload.");

}  // namespace detail::clock_source_lattice_self_test

}  // namespace foundation::algebra::lattices
