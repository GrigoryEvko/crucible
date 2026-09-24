// Sentinel TU: compiles the header under the project warning flags so its
// static_asserts run.

#include <crucible/fixy/_Time.h>
#include <crucible/effects/_ExecCtx.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace ft = ::crucible::fixy::time;
namespace gt = ::crucible::fixy::grant::time;
namespace sf = ::crucible::safety;
namespace eff = ::crucible::effects;
namespace ml = ::crucible::algebra::lattices;
using CS = ft::ClockSource_v;
using TM = ft::TscMode;

static_assert(std::is_same_v<decltype(ft::mint_clock_reader<CS::Boot>(std::declval<eff::ColdInitCtx const&>())),
                             ft::ClockReader<CS::Boot>>);
static_assert(std::is_same_v<decltype(ft::mint_bounded_sleep<4096>(std::declval<eff::BgDrainCtx const&>())),
                             ft::BoundedSleeper<4096>>);

static_assert(std::is_same_v<ft::ClockReader<CS::Boot>::result_type, sf::BootClockBytes<std::uint64_t>>);
static_assert(std::is_same_v<ft::ClockReader<CS::Monotonic>::result_type, sf::MonotonicClockBytes<std::uint64_t>>);
// A Boot read satisfies the KeepsTicking watchdog floor.  A Monotonic read does
// not.
static_assert(ft::ClockReader<CS::Boot>::result_type::satisfies<CS::Boot>);
static_assert(!ft::ClockReader<CS::Monotonic>::result_type::satisfies<CS::Boot>);

using SinglePin = sf::CpuPinned<ml::AffinityMask::single(2), sf::PinningPosture::PinnedExplicit, int>;
static_assert(ft::IsSingletonCpuPin<SinglePin>);
static_assert(
    !ft::IsSingletonCpuPin<sf::CpuPinned<ml::AffinityMask::range(0, 3), sf::PinningPosture::PinnedExplicit, int>>);

static_assert(::crucible::fixy::grant::which_dim_v<gt::clock_read<CS::Boot>>
              == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim_v<gt::tsc_read<TM::Raw>>
              == ::crucible::fixy::dim::DimensionAxis::HwInstruction);

}  // namespace

int main() { return ::crucible::fixy::time::detail::v190_self_test::runtime_smoke_test() ? 0 : 1; }
