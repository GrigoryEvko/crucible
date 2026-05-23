// FIXY-V-190 sentinel TU: fixy/Time.h — the time-reading + bounded-sleep
// surface.  Grant tags (clock_read / tsc_read / sleep) routed to
// SyscallSurface / HwInstruction, plus three §XXI mints returning runtime
// reader/sleeper objects that consume the V-185 ClockSource axis and the
// V-187 CpuPinned proof.
//
// LOAD-BEARING PROPERTIES this TU defends:
//   (1) a ClockReader<Boot> yields BootClockBytes, a ClockReader<Monotonic>
//       yields MonotonicClockBytes — distinct provenance types (V-194);
//   (2) mint_tsc_reader admits ONLY a single-core CpuPinned proof (V-196);
//   (3) mint_bounded_sleep admits ONLY a Block-capable ctx, and the cap is
//       a compile-time NTTP.
//
// HS14 negative coverage lives in six distinct-mismatch-class fixtures
// (test/fixy_neg/neg_time_*): two per mint.

#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace ft  = ::crucible::fixy::time;
namespace gt  = ::crucible::fixy::grant::time;
namespace sf  = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml  = ::crucible::algebra::lattices;
using CS = ft::ClockSource_v;
using TM = ft::TscMode;

// ── mint return types are the concrete reader/sleeper objects ───────
static_assert(std::is_same_v<
    decltype(ft::mint_clock_reader<CS::Boot>(std::declval<eff::ColdInitCtx const&>())),
    ft::ClockReader<CS::Boot>>);
static_assert(std::is_same_v<
    decltype(ft::mint_bounded_sleep<4096>(std::declval<eff::BgDrainCtx const&>())),
    ft::BoundedSleeper<4096>>);

// ── the clock-source provenance distinction (V-194) ─────────────────
static_assert(std::is_same_v<ft::ClockReader<CS::Boot>::result_type,
                             sf::BootClockBytes<std::uint64_t>>);
static_assert(std::is_same_v<ft::ClockReader<CS::Monotonic>::result_type,
                             sf::MonotonicClockBytes<std::uint64_t>>);
// A Boot read satisfies the KeepsTicking watchdog floor; a Monotonic read does not.
static_assert( ft::ClockReader<CS::Boot>::result_type::satisfies<CS::Boot>);
static_assert(!ft::ClockReader<CS::Monotonic>::result_type::satisfies<CS::Boot>);

// ── the TSC single-core-pin gate (V-196) ────────────────────────────
using SinglePin = sf::CpuPinned<ml::AffinityMask::single(2), sf::PinningPosture::PinnedExplicit, int>;
static_assert( ft::IsSingletonCpuPin<SinglePin>);
static_assert(!ft::IsSingletonCpuPin<
    sf::CpuPinned<ml::AffinityMask::range(0, 3), sf::PinningPosture::PinnedExplicit, int>>);

// ── grant-tag routing ───────────────────────────────────────────────
static_assert(::crucible::fixy::grant::which_dim_v<gt::clock_read<CS::Boot>>
              == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim_v<gt::tsc_read<TM::Raw>>
              == ::crucible::fixy::dim::DimensionAxis::HwInstruction);

}  // namespace

int main() {
    return ::crucible::fixy::time::detail::v190_self_test::runtime_smoke_test() ? 0 : 1;
}
