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
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/reflect/EnumName.h>

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
    struct At : PinnedAt<ClockSourceLattice, Source> {
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

namespace detail {

// The carrier of a projected point.  A point stored beside a value goes
// through the order dual.  The check file of this header and
// test/foundation/test_lattices_bands.cpp name it.
template <typename T_>
using ClockGraded = Graded<ModalityKind::Absolute, DualLattice<ClockSourceLattice>, T_>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
