// The fixed map of the guarded memory door (THE FIXED MAP of
// foundation/os/GuardedRegion.h).
//
// The range of a region that ended maps again at the same address, and its
// first usable byte reads zero.  A range that a live region holds gives
// eexist, and the live region keeps its byte.  An address below the guard
// gives einval with no call, an address that is not the first byte of a page
// gives the einval of the kernel, and a count that the rounding cannot hold
// gives enomem.  The test has one thread, so no other map takes a range
// between the end of a region and the fixed map.

#include <foundation/os/GuardedRegion.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace os = ::foundation::os;

using MapResult = ::foundation::core::Result<os::GuardedRegion, os::Errno>;

// The usable bytes of each region of the test, a multiple of kGuardBytes.
constexpr ::fixy::ByteCount kUsableBytes{std::size_t{1} << 20U};

// The bytes of the smallest page of x86_64 and of aarch64.  Each page size of
// the two is a multiple of it.
constexpr std::uint64_t kSmallestPageBytes = 4096;

// The error of a refused map, or eio for a map that the door gave.
[[nodiscard]] os::Errno error_of(MapResult&& mapped) noexcept {
    return std::move(mapped).err().value_or(os::Errno::eio);
}

// The address of the first usable byte of a region that maps and then ends.
[[nodiscard]] os::RegionAddress address_of_an_ended_region() noexcept {
    os::GuardedRegion const region = os::map_guarded_region(kUsableBytes).expect("a guarded region maps");
    return region.address();
}

// The address of a region is the first byte of a page.
void test_a_region_gives_its_address(::fixy::TestRunnerCtx const& test) {
    os::GuardedRegion const region = os::map_guarded_region(kUsableBytes).expect("a guarded region maps");
    std::uint64_t const address = static_cast<std::uint64_t>(region.address());
    CRUCIBLE_TEST_ASSERT(test, address != 0);
    CRUCIBLE_TEST_ASSERT(test, address % kSmallestPageBytes == 0);
}

// The range of a region that ended maps again at its address, with the same
// usable bytes, and its first usable byte reads zero.
void test_an_ended_range_maps_again(::fixy::TestRunnerCtx const& test) {
    os::RegionAddress const address = address_of_an_ended_region();
    MapResult again = os::map_guarded_region_at(address, kUsableBytes);
    CRUCIBLE_TEST_ASSERT(test, again.is_ok());
    os::GuardedRegion region = std::move(again).expect("the fixed map of an ended range maps");
    CRUCIBLE_TEST_ASSERT(test, region.address() == address);
    CRUCIBLE_TEST_ASSERT(test, region.usable_bytes() == kUsableBytes.raw());
    CRUCIBLE_TEST_ASSERT(test, region.data_mut()[0] == 0);
    region.data_mut()[0] = 7;
    CRUCIBLE_TEST_ASSERT(test, region.data_mut()[0] == 7);
}

// A fixed map of a range that a live region holds gives eexist, and the live
// region keeps its byte.  A range that holds only the usable pages of the
// live region also gives eexist.
void test_a_live_range_gives_eexist(::fixy::TestRunnerCtx const& test) {
    os::GuardedRegion live = os::map_guarded_region(kUsableBytes).expect("a guarded region maps");
    live.data_mut()[0] = 5;
    CRUCIBLE_TEST_ASSERT(test, error_of(os::map_guarded_region_at(live.address(), kUsableBytes)) == os::Errno::eexist);
    os::RegionAddress const inside{static_cast<std::uint64_t>(live.address()) + os::kGuardBytes};
    CRUCIBLE_TEST_ASSERT(test, error_of(os::map_guarded_region_at(inside, kUsableBytes)) == os::Errno::eexist);
    CRUCIBLE_TEST_ASSERT(test, live.data_mut()[0] == 5);
}

// An address below the guard leaves no room for it, and the door refuses it
// with no call.  An address that is not the first byte of a page gives the
// einval of the kernel.  A count that the rounding cannot hold gives enomem.
void test_a_refused_request_gives_its_error(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test,
                         error_of(os::map_guarded_region_at(os::RegionAddress{}, kUsableBytes)) == os::Errno::einval);
    os::RegionAddress const below{os::kGuardBytes - kSmallestPageBytes};
    CRUCIBLE_TEST_ASSERT(test, error_of(os::map_guarded_region_at(below, kUsableBytes)) == os::Errno::einval);
    os::RegionAddress const ended = address_of_an_ended_region();
    os::RegionAddress const inside_a_page{static_cast<std::uint64_t>(ended) + 1};
    CRUCIBLE_TEST_ASSERT(test, error_of(os::map_guarded_region_at(inside_a_page, kUsableBytes)) == os::Errno::einval);
    CRUCIBLE_TEST_ASSERT(test, error_of(os::map_guarded_region_at(ended, ::fixy::ByteCount{~std::size_t{0}}))
                                   == os::Errno::enomem);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_a_region_gives_its_address(test);
    test_an_ended_range_maps_again(test);
    test_a_live_range_gives_eexist(test);
    test_a_refused_request_gives_its_error(test);
    ::fixy::check::pass(test, "test_os_guarded_region: an ended range maps again, and a live range gives eexist\n");
    return 0;
}
