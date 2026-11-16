// The match of the reductions library and its nets, part three: the bits of
// the sum scans.  The inclusive and the exclusive sum scans, with the lanes
// of the target and with 4 and 16 lanes, give the bits of the evaluation of
// the net of the scan plan at each output, on Philox draws.  The sum scans
// run on SIMD lanes at run time, so main runs these tests.  The nets come
// from reductions_net.h.

#include <foundation/core/Array.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>

#include "../test_assert.h"
#include "philox_stream.h"
#include "reductions_net.h"

#include <cstddef>
#include <cstdint>

namespace {

using namespace ::crucible::test::reductions_net;

// The sum scan of Lanes lanes, or of the lanes of the target for Lanes 0.
template <int Lanes, core::ScanKind Kind, class T, class Out>
[[nodiscard]] bool scan_sums_in(core::View<T const> in, Out out) noexcept {
    if constexpr (Lanes == 0 && Kind == core::ScanKind::Inclusive) {
        return core::scan_inclusive_sums(in, std::move(out)).is_ok();
    } else if constexpr (Lanes == 0) {
        return core::scan_exclusive_sums(in, std::move(out)).is_ok();
    } else if constexpr (Kind == core::ScanKind::Inclusive) {
        return core::scan_inclusive_sums_in_lanes<Lanes>(in, std::move(out)).is_ok();
    } else {
        return core::scan_exclusive_sums_in_lanes<Lanes>(in, std::move(out)).is_ok();
    }
}

// The sum scan of Lanes lanes gives the bits of the net of the scan plan at
// each output.  The empty value of an exclusive sum scan is +0.
template <int Lanes, class T, std::size_t Count, core::ScanKind Kind>
[[nodiscard]] bool does_sum_scan_follow(std::uint64_t seed) noexcept {
    auto const leaves = draw_values<T, Count>(seed);
    auto const from_net = evaluate_net<Count>(scan_net_of<Count, Kind>, leaves, T{}, AddValues{}, core::Ascending{});
    core::FixedArray<T, Count> scanned{};
    bool is_written = false;
    scanned = core::with_mut(std::move(scanned), [&](auto out) noexcept {
        is_written = scan_sums_in<Lanes, Kind>(core::View<T const>{leaves.view()}, std::move(out));
    });
    std::size_t position = 0;
    for (T const sum : scanned) {
        is_written = is_written && has_same_bits(sum, slot_at(from_net, position));
        ++position;
    }
    return is_written;
}

// Each count of lanes follows the net, on 16 draws.
template <class T, std::size_t Count, core::ScanKind Kind>
[[nodiscard]] bool does_each_sum_scan_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        std::uint64_t const draw = seeds.next_wide();
        is_each = is_each && does_sum_scan_follow<0, T, Count, Kind>(draw)
               && does_sum_scan_follow<4, T, Count, Kind>(draw) && does_sum_scan_follow<16, T, Count, Kind>(draw);
    }
    return is_each;
}

}  // namespace

int main() {
    bool const is_inclusive = does_each_sum_scan_follow<float, 13, core::ScanKind::Inclusive>(0x7E570201u)
                           && does_each_sum_scan_follow<float, 37, core::ScanKind::Inclusive>(0x7E570202u)
                           && does_each_sum_scan_follow<double, 37, core::ScanKind::Inclusive>(0x7E570203u);
    bool const is_exclusive = does_each_sum_scan_follow<float, 13, core::ScanKind::Exclusive>(0x7E570301u)
                           && does_each_sum_scan_follow<float, 37, core::ScanKind::Exclusive>(0x7E570302u)
                           && does_each_sum_scan_follow<double, 37, core::ScanKind::Exclusive>(0x7E570303u);
    assert(is_inclusive);
    assert(is_exclusive);
    return 0;
}
