#pragma once

// On each of the three axes the stronger guarantee sits higher, so a leq that
// holds reads as: a consumer asking for the lower point is served by a provider
// that meets the higher one, on every axis at once.  Two sources that each lead
// on a different axis are incomparable, which is what a product expresses and a
// single chain cannot.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_DetSafeLattice.h>
#include <crucible/algebra/lattices/_PinningRequirementLattice.h>
#include <crucible/algebra/lattices/_ProductLattice.h>
#include <crucible/algebra/lattices/_SuspendBehaviorLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

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

inline constexpr std::size_t clock_source_count = std::meta::enumerators_of(^^ClockSource).size();

[[nodiscard]] consteval std::string_view clock_source_name(ClockSource s) noexcept {
    switch (s) {
        case ClockSource::Realtime:
            return "Realtime";
        case ClockSource::Monotonic:
            return "Monotonic";
        case ClockSource::MonotonicRaw:
            return "MonotonicRaw";
        case ClockSource::Boot:
            return "Boot";
        case ClockSource::ThreadCpu:
            return "ThreadCpu";
        case ClockSource::ProcessCpu:
            return "ProcessCpu";
        case ClockSource::TscRaw:
            return "TscRaw";
        case ClockSource::TscSerialized:
            return "TscSerialized";
        case ClockSource::PmuCounter:
            return "PmuCounter";
        case ClockSource::PtpHwClock:
            return "PtpHwClock";
        default:
            return std::string_view{"<unknown ClockSource>"};
    }
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
    struct At {
        struct element_type {
            using clock_source_value_type = ClockSource;
            [[nodiscard]] constexpr operator clock_source_value_type() const noexcept { return Source; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr ClockSource source = Source;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (Source) {
                case ClockSource::Realtime:
                    return "ClockSourceLattice::At<Realtime>";
                case ClockSource::Monotonic:
                    return "ClockSourceLattice::At<Monotonic>";
                case ClockSource::MonotonicRaw:
                    return "ClockSourceLattice::At<MonotonicRaw>";
                case ClockSource::Boot:
                    return "ClockSourceLattice::At<Boot>";
                case ClockSource::ThreadCpu:
                    return "ClockSourceLattice::At<ThreadCpu>";
                case ClockSource::ProcessCpu:
                    return "ClockSourceLattice::At<ProcessCpu>";
                case ClockSource::TscRaw:
                    return "ClockSourceLattice::At<TscRaw>";
                case ClockSource::TscSerialized:
                    return "ClockSourceLattice::At<TscSerialized>";
                case ClockSource::PmuCounter:
                    return "ClockSourceLattice::At<PmuCounter>";
                case ClockSource::PtpHwClock:
                    return "ClockSourceLattice::At<PtpHwClock>";
                default:
                    return "ClockSourceLattice::At<?>";
            }
        }
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
                                        "clock_source_name(), an arm in clock_source_project(), and this "
                                        "count bumped; existing ordinals never renumber.");

[[nodiscard]] consteval bool every_clock_source_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ClockSource));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (clock_source_name([:en:]) == std::string_view{"<unknown ClockSource>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_clock_source_has_name(), "clock_source_name() switch missing an arm for at least one source — "
                                             "add the arm or the new source leaks the '<unknown ClockSource>' "
                                             "sentinel into observer debug output.");

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
// carrier graded on it costs exactly the payload.
static_assert(crucible::algebra::Lattice<ClockSourceLattice::At<ClockSource::Boot>>);
static_assert(crucible::algebra::BoundedLattice<ClockSourceLattice::At<ClockSource::TscRaw>>);
static_assert(std::is_empty_v<ClockSourceLattice::At<ClockSource::Boot>::element_type>,
              "At<Source>::element_type must be empty so a carrier graded on it "
              "collapses to the size of its payload.");
static_assert(ClockSourceLattice::At<ClockSource::Boot>::source == ClockSource::Boot);
static_assert(ClockSourceLattice::At<ClockSource::Realtime>::source == ClockSource::Realtime);
static_assert(ClockSourceLattice::At<ClockSource::TscRaw>::name()
              == std::string_view{"ClockSourceLattice::At<TscRaw>"});

[[nodiscard]] consteval bool every_at_clock_source_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ClockSource));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ClockSourceLattice::At<([:en:])>::name() == std::string_view{"ClockSourceLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_clock_source_has_name(), "ClockSourceLattice::At<Source>::name() switch missing an arm for at "
                                                "least one source — add the arm or the new source leaks the "
                                                "'ClockSourceLattice::At<?>' sentinel.");

static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                               ClockSourceLattice::At<ClockSource::Boot>, EightByteValue>)
                  == sizeof(EightByteValue),
              "A carrier graded on At<Boot> must add no bytes to an eight-byte "
              "payload.");

// Calling each operation on runtime operands catches the defects the
// compile-time assertions above cannot see, such as an inline body that only
// ever instantiates in a consteval context.
inline void runtime_smoke_test() {
    ClockSource source = ClockSource::TscRaw;
    auto tsc_point = clock_source_project(source);
    [[maybe_unused]] DetSafeTier det = ClockSourceLattice::get<0>(tsc_point);
    [[maybe_unused]] SuspendBehavior suspend = ClockSourceLattice::get<1>(tsc_point);
    [[maybe_unused]] PinningRequirement pin = ClockSourceLattice::get<2>(tsc_point);

    auto boot_point = clock_source_project(ClockSource::Boot);
    [[maybe_unused]] bool le = ClockSourceLattice::leq(boot_point, tsc_point);
    [[maybe_unused]] auto jn = ClockSourceLattice::join(boot_point, tsc_point);
    [[maybe_unused]] auto mt = ClockSourceLattice::meet(boot_point, tsc_point);
    [[maybe_unused]] auto bt = ClockSourceLattice::bottom();
    [[maybe_unused]] auto tp = ClockSourceLattice::top();

    [[maybe_unused]] auto built = ClockSourceLattice::make_point(det, suspend, pin);

    EightByteValue payload{42};
    ClockGraded<EightByteValue> initial{payload, boot_point};
    auto widened = initial.weaken(tsc_point);
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(ClockSourceLattice::top());

    [[maybe_unused]] auto grade = rv_widen.grade();
    [[maybe_unused]] auto value = composed.peek().v;
    [[maybe_unused]] auto moved = std::move(composed).consume().v;
}

}  // namespace detail::clock_source_lattice_self_test

}  // namespace crucible::algebra::lattices
