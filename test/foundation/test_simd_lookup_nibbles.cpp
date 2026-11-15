// lookup_nibbles of foundation/Simd.h against a scalar loop.
//
// The scalar loop reads the entry of the table at the low four bits of each
// index byte.  Each round puts a different index byte in each lane, and 256
// rounds put each of the 256 index bytes in each lane.  So each lane reads
// each entry of the table, and each high nibble, which plays no part.  Four
// tables with different entries show that no lane reads a stale table.  The
// widths of 16, 32 and 64 lanes run the arm that the build selects: the
// generic shuffle of GCC in the Debug build, and vpshufb of AVX2 and of
// AVX-512BW in the Release build on a host that has them.

#include <foundation/Platform.h>
#include <foundation/Simd.h>

#include <cstdint>

namespace {

namespace simd = ::foundation::simd;

// The entry at position of the table with the number table.  Each table
// and each position gives a different byte.
[[nodiscard]] constexpr std::uint8_t entry_of(int table, int position) noexcept {
    return static_cast<std::uint8_t>(table * 97 + position * 29 + 5);
}

// The index byte of lane in round: each round gives each lane a different
// low nibble, and the rounds from 0 to 255 give each lane each byte.
[[nodiscard]] constexpr std::uint8_t index_of(int round, int lane) noexcept {
    return static_cast<std::uint8_t>(round + lane * 23);
}

// True when lookup_nibbles of Lanes lanes gives the entry of the scalar loop
// in each lane of each round, for the table with the number table.
template <int Lanes>
[[nodiscard]] bool agrees_with_scalar_loop(int table) noexcept {
    using Table = simd::vec<std::uint8_t, 16>;
    using Indices = simd::vec<std::uint8_t, Lanes>;
    typename Table::raw_type entry_lanes{};
    for (int position = 0; position < 16; ++position)
        entry_lanes[position] = entry_of(table, position);
    Table const entries{entry_lanes};
    for (int round = 0; round < 256; ++round) {
        typename Indices::raw_type index_lanes{};
        for (int lane = 0; lane < Lanes; ++lane)
            index_lanes[lane] = index_of(round, lane);
        Indices const looked_up = simd::lookup_nibbles(entries, Indices{index_lanes});
        for (int lane = 0; lane < Lanes; ++lane) {
            int const position = index_of(round, lane) & 0x0f;
            if (looked_up[lane] != entry_of(table, position)) return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    for (int table = 0; table < 4; ++table) {
        CRUCIBLE_FATAL_INVARIANT(agrees_with_scalar_loop<16>(table));
        CRUCIBLE_FATAL_INVARIANT(agrees_with_scalar_loop<32>(table));
        CRUCIBLE_FATAL_INVARIANT(agrees_with_scalar_loop<64>(table));
    }
    return 0;
}
