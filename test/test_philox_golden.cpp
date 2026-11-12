// The golden words of the Philox4x32-10 generator.
//
// Each check holds an offset, a key and the four words that the generator
// gave for them before the generator moved into the base.  The move changes
// no word, so the golden words stay the same after it.  The two conversions
// to a uniform value have golden bits too.  The checks read each block
// through fixy::bit_cast, so they name no type of the counter.  Each check
// runs at compile time and at run time.

#include <crucible/Philox.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>

#include <cstdint>

namespace {

// The four words of one block, in the sequence of the counter.
struct Words final {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
    std::uint32_t fourth = 0;

    friend constexpr bool operator==(Words const&, Words const&) noexcept = default;
};

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

[[nodiscard]] constexpr Words block_of(std::uint64_t offset, std::uint64_t key) noexcept {
    return fixy::bit_cast<Words>(crucible::Philox::generate(offset, key));
}

// The block of the full counter and key: no word of either is zero.
[[nodiscard]] constexpr Words block_of_full_counter() noexcept {
    return fixy::bit_cast<Words>(
        crucible::Philox::generate({0x01234567u, 0x89ABCDEFu, 0xFEDCBA98u, 0x76543210u}, {0xDEADBEEFu, 0xCAFEBABEu}));
}

static_assert(block_of(0, 0) == Words{0x6627E8D5u, 0xE169C58Du, 0xBC57AC4Cu, 0x9B00DBD8u});
static_assert(block_of(0x243F6A8885A308D3u, 0x13198A2E03707344u)
              == Words{0x330A00AEu, 0x74C34C92u, 0xC3B086F1u, 0x58A09CB4u});
static_assert(block_of_full_counter() == Words{0xC6E55E9Au, 0x1B21B854u, 0xFD0D7B2Fu, 0xA4C2DD7Au});
static_assert(fixy::bit_cast<std::uint32_t>(crucible::Philox::to_uniform(0x80000000u)) == 0x3F000000u);
static_assert(fixy::bit_cast<std::uint64_t>(crucible::Philox::to_uniform_d(0xFFFFFFFFu)) == 0x3FEFFFFFFFE00000u);

void check_block(std::uint64_t offset, std::uint64_t key, Words expected) {
    CRUCIBLE_FATAL_INVARIANT(block_of(opaque(offset), opaque(key)) == expected);
}

// Each offset and key below gives one golden block.  The rows hold the
// zero counter, one word of the counter, one word of the key, each bit set,
// a high word of each, two rows of the digits of pi, and two small values.
void test_golden_blocks() {
    check_block(0x0000000000000000u, 0x0000000000000000u, Words{0x6627E8D5u, 0xE169C58Du, 0xBC57AC4Cu, 0x9B00DBD8u});
    check_block(0x0000000000000001u, 0x0000000000000000u, Words{0xF8E4CCA4u, 0x5CB200DBu, 0xB1A574EBu, 0x097EFF67u});
    check_block(0x0000000000000000u, 0x0000000000000001u, Words{0xE3E80670u, 0xE50A0EBCu, 0x95F222C0u, 0xB615AA27u});
    check_block(0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu, Words{0x4D18D7D2u, 0x430AC65Au, 0xBD2FA22Au, 0x0AFAE10Du});
    check_block(0x0000000100000000u, 0x0000000100000001u, Words{0xD3374657u, 0x974CBE2Cu, 0x2EB6E2BAu, 0x4B3B59B8u});
    check_block(0x243F6A8885A308D3u, 0x13198A2E03707344u, Words{0x330A00AEu, 0x74C34C92u, 0xC3B086F1u, 0x58A09CB4u});
    check_block(0xA4093822299F31D0u, 0x082EFA98EC4E6C89u, Words{0x9A6A4557u, 0xE490D537u, 0x514CC838u, 0xB05E0928u});
    check_block(0x0000000000003039u, 0x0000000000010932u, Words{0xCB58A734u, 0xB5734E3Au, 0xB8F5CF58u, 0x3087B55Fu});
    Words const full_counter_block{0xC6E55E9Au, 0x1B21B854u, 0xFD0D7B2Fu, 0xA4C2DD7Au};
    CRUCIBLE_FATAL_INVARIANT(opaque(block_of_full_counter()) == full_counter_block);
}

// 2^31 times 2^-32 is 0.5, and the highest word gives the largest double
// below 1 that the conversion reaches.
void test_golden_uniform_bits() {
    CRUCIBLE_FATAL_INVARIANT(fixy::bit_cast<std::uint32_t>(crucible::Philox::to_uniform(opaque(0x80000000u)))
                             == 0x3F000000u);
    CRUCIBLE_FATAL_INVARIANT(fixy::bit_cast<std::uint64_t>(crucible::Philox::to_uniform_d(opaque(0xFFFFFFFFu)))
                             == 0x3FEFFFFFFFE00000u);
}

}  // namespace

int main() {
    test_golden_blocks();
    test_golden_uniform_bits();
    return 0;
}
