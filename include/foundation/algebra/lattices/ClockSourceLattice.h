#pragma once

// On each of the three axes the stronger guarantee sits higher, so a leq that
// holds reads as: a consumer asking for the lower point is served by a provider
// that meets the higher one, on every axis at once.  Two sources that each lead
// on a different axis are incomparable, which is what a product expresses and a
// single chain cannot.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// The ordinals are declaration order and carry no order semantics.  The order
// lives in the point each source projects to, never in the enum itself.  New
// sources append at the next free ordinal so that existing values keep their
// place in the cache keys built from them.
enum class ClockSource : std::uint8_t {
    Realtime = 0,  // CLOCK_REALTIME — settable wall clock
    Monotonic = 1,  // CLOCK_MONOTONIC — NTP-slewed
    MonotonicRaw = 2,  // CLOCK_MONOTONIC_RAW — un-slewed
    Boot = 3,  // CLOCK_BOOTTIME
    ThreadCpu = 4,  // CLOCK_THREAD_CPUTIME_ID
    ProcessCpu = 5,  // CLOCK_PROCESS_CPUTIME_ID
    TscRaw = 6,  // RDTSC
    TscSerialized = 7,  // RDTSCP, or LFENCE then RDTSC
    PmuCounter = 8,  // perf_event cycles
    PtpHwClock = 9,  // the /dev/ptpN hardware clock on a NIC
};

inline constexpr std::size_t clock_source_count = ::foundation::reflect::enum_count<ClockSource>;

// The identifier of s, or "<unknown ClockSource>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view clock_source_name(ClockSource s) noexcept {
    return ::foundation::reflect::enum_name(s);
}

struct ClockSourceLattice : ProductLattice<DetSafeLattice, SuspendBehaviorLattice, PinningRequirementLattice> {
    using det_safe_axis = DetSafeLattice;
    using suspend_axis = SuspendBehaviorLattice;
    using pinning_axis = PinningRequirementLattice;

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ClockSourceLattice"; }

    [[nodiscard]] static constexpr element_type make_point(DetSafeTier det, SuspendBehavior suspend,
                                                           PinningRequirement pin) noexcept {
        element_type point{};
        ClockSourceLattice::get<0>(point) = det;
        ClockSourceLattice::get<1>(point) = suspend;
        ClockSourceLattice::get<2>(point) = pin;
        return point;
    }

    // At pins the source identity and nothing else.  It does not expose the
    // projected point, because the projection is a free function defined below
    // and referring to it here would be a forward reference.  A holder that
    // wants the point calls that function with the pinned source.
    template <ClockSource Source>
    struct AtElement : PinnedElement<Source> {
        using clock_source_value_type = ClockSource;
    };

    template <ClockSource Source>
    struct At : PinnedAt<ClockSourceLattice, Source, AtElement<Source>> {
        static constexpr ClockSource source = Source;
    };
};

// The arms below record kernel and hardware behaviour that the source names
// alone do not give.
[[nodiscard]] constexpr ClockSourceLattice::element_type clock_source_project(ClockSource source) noexcept {
    switch (source) {
        case ClockSource::Realtime:
            // An administrator or a time daemon can step this clock backwards.
            return ClockSourceLattice::make_point(DetSafeTier::WallClockRead, SuspendBehavior::PausesOnSuspend,
                                                  PinningRequirement::NotRequired);
        case ClockSource::Monotonic:
        case ClockSource::MonotonicRaw:
        case ClockSource::ThreadCpu:
        case ClockSource::ProcessCpu:
            // The two monotonic clocks stop for the duration of a suspend.  The
            // two CPU-time clocks only advance while their thread or process
            // runs, and the kernel keeps the count whichever CPU serves the
            // read, so neither needs an affinity proof.
            return ClockSourceLattice::make_point(DetSafeTier::MonotonicClockRead, SuspendBehavior::PausesOnSuspend,
                                                  PinningRequirement::NotRequired);
        case ClockSource::Boot:
        case ClockSource::PtpHwClock:
            // Boot time advances across a suspend.  The hardware clock on a NIC
            // runs off its own oscillator, so a host suspend does not stop it
            // either, and it is read through a file descriptor rather than an
            // instruction, so it needs no CPU pin.
            return ClockSourceLattice::make_point(DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                                                  PinningRequirement::NotRequired);
        case ClockSource::TscRaw:
        case ClockSource::TscSerialized:
        case ClockSource::PmuCounter:
            // An invariant cycle counter keeps running across a suspend, but
            // each core has its own, so a value read on one core means nothing
            // beside a value read on another.
            return ClockSourceLattice::make_point(DetSafeTier::MonotonicClockRead, SuspendBehavior::KeepsTicking,
                                                  PinningRequirement::PerCore);
        default:
            // Bottom is the safe answer for a value outside the enum: it is the
            // weakest point on every axis, so it satisfies no consumer.
            return ClockSourceLattice::bottom();
    }
}

namespace detail::clock_source_lattice_self_test {

static_assert(clock_source_count == 10, "The ClockSource catalog changed size.  A new source needs an arm in "
                                        "clock_source_project() and this count bumped; existing ordinals "
                                        "never renumber.");

static_assert(clock_source_name(static_cast<ClockSource>(200)) == "<unknown ClockSource>");

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
static_assert(clock_source_name(ClockSource::TscRaw) == std::string_view{"TscRaw"});
static_assert(clock_source_name(ClockSource::Boot) == std::string_view{"Boot"});
static_assert(clock_source_name(ClockSource::TscSerialized) == std::string_view{"TscSerialized"});

// The product point occupies three bytes, one per axis, so a carrier over it
// grows by that much plus padding and the exact-size invariant does not apply.
// The bound is asserted by hand instead.
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using ClockGraded = Graded<ModalityKind::Absolute, ClockSourceLattice, T_>;

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
