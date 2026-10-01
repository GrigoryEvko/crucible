#pragma once

// Pins which clock produced a time reading, so a consumer that needs a
// particular kind of clock can turn away the others at compile time.
//
// There is no widening or relabelling operation.  A source is a physical
// fact about where the reading came from, and two clocks that tick
// differently across a suspend cannot stand in for one another.  What
// the wrapper does expose is the tier each source projects to on the
// determinism, suspend and pinning axes, so a consumer can gate on the
// one property it actually depends on.

#include <fixy/GradedFacade.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy::time::detail {
// The door that stamps a value with the clock it came from.  It is
// defined in fixy/os/Time.h beside the readers, and only they reach it.
struct clock_stamp_access;
}  // namespace fixy::time::detail

namespace fixy {

using ::foundation::algebra::lattices::ClockSourceLattice;
using ClockSource_v = ::foundation::algebra::lattices::ClockSource;
using DetSafeTier_v = ::foundation::algebra::lattices::DetSafeTier;
using SuspendBehavior_v = ::foundation::algebra::lattices::SuspendBehavior;
using PinningRequirement_v = ::foundation::algebra::lattices::PinningRequirement;

template <ClockSource_v Source, typename T>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] ClockSource
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute, ClockSourceLattice::At<Source>, T> {
public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the names this class
    // body uses unqualified are re-declared here rather than found by
    // lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute, ClockSourceLattice::At<Source>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

    static constexpr ClockSource_v source = Source;

    static constexpr DetSafeTier_v det_safe_tier =
        ClockSourceLattice::get<0>(::foundation::algebra::lattices::clock_source_project(Source));
    static constexpr SuspendBehavior_v suspend_behavior =
        ClockSourceLattice::get<1>(::foundation::algebra::lattices::clock_source_project(Source));
    static constexpr PinningRequirement_v pinning_requirement =
        ClockSourceLattice::get<2>(::foundation::algebra::lattices::clock_source_project(Source));

private:
    graded_type impl_;

    // The one constructor that stamps a value with a source, and it is
    // private.  Its sole friend is fixy::time::detail::clock_stamp_access,
    // whose stamp is private too and reached only by the readers in
    // fixy/os/Time.h, right after the read of this same clock.  So a
    // ClockSource<Boot, T> holds a value that CLOCK_BOOTTIME returned.
    constexpr explicit ClockSource(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{::foundation::algebra::grade_key<ClockSource>{}, std::move(value),
                typename lattice_type::element_type{}} {}

    friend struct ::fixy::time::detail::clock_stamp_access;

public:
    // There is no default constructor, no in_place constructor and no
    // free mint.  Each of them builds a reading that no clock returned.
    ClockSource() = delete("a default-constructed ClockSource would claim a reading that no clock returned.  Read "
                           "the clock through a reader from fixy::time::mint_clock_reader, mint_tsc_reader or "
                           "mint_ptp_clock_reader.");

    // A copy of a reading is still a reading of the same clock, so copies
    // stay.  The constructors are trivial, so a reading passes in a
    // register.  The assignments are user-provided, so the class is not
    // trivially copyable: std::bit_cast refuses to stamp a raw integer
    // with a source, and -Wclass-memaccess refuses a memcpy into one.
    // The annotation on the class refuses the checked lifetime start over
    // bytes, and utils/scripts/check-start-lifetime.py refuses the raw one.
    constexpr ClockSource(const ClockSource&) = default;
    constexpr ClockSource(ClockSource&&) = default;
    constexpr ClockSource& operator=(const ClockSource& other) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        impl_ = other.impl_;
        return *this;
    }
    constexpr ClockSource& operator=(ClockSource&& other) noexcept(std::is_nothrow_move_assignable_v<T>) {
        impl_ = std::move(other.impl_);
        return *this;
    }
    ~ClockSource() = default;

    [[nodiscard]] friend constexpr bool operator==(ClockSource const& a,
                                                   ClockSource const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    // There is no peek_mut().  A mutable reference would let a holder
    // write any value under the source of a real reading.

    constexpr void swap(ClockSource& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(ClockSource& a, ClockSource& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // True when this source covers the required one on all three axes at
    // once, not on any single axis alone.
    template <ClockSource_v Required>
    static constexpr bool satisfies =
        ClockSourceLattice::leq(::foundation::algebra::lattices::clock_source_project(Required),
                                ::foundation::algebra::lattices::clock_source_project(Source));
};

template <typename T>
using RealtimeClockBytes = ClockSource<ClockSource_v::Realtime, T>;
template <typename T>
using MonotonicClockBytes = ClockSource<ClockSource_v::Monotonic, T>;
template <typename T>
using MonotonicRawClockBytes = ClockSource<ClockSource_v::MonotonicRaw, T>;
template <typename T>
using BootClockBytes = ClockSource<ClockSource_v::Boot, T>;
template <typename T>
using ThreadCpuBytes = ClockSource<ClockSource_v::ThreadCpu, T>;
template <typename T>
using ProcessCpuBytes = ClockSource<ClockSource_v::ProcessCpu, T>;
template <typename T>
using TscBytes = ClockSource<ClockSource_v::TscRaw, T>;
template <typename T>
using TscSerializedBytes = ClockSource<ClockSource_v::TscSerialized, T>;
template <typename T>
using PmuBytes = ClockSource<ClockSource_v::PmuCounter, T>;
// A clock on the network adapter itself.  Its oscillator runs
// independently of the host, so it keeps ticking through a host suspend,
// and reading it needs no CPU pin.
template <typename T>
using PtpHwClockBytes = ClockSource<ClockSource_v::PtpHwClock, T>;

}  // namespace fixy
