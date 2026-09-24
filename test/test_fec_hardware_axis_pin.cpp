// Sentinel TU for the hardware pin of cntp/Fec.h.  The header restates
// its compile-time-selected kernel arm as fixy atoms: the ISA the kernels
// were emitted for and the instruction class they issue.  This file
// compiles the header under the project warning flags, so its own
// static_asserts run, and it pins the atoms that the active build selects.

#include <crucible/cntp/Fec.h>

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Simd.h>

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace {

namespace fh = ::crucible::cntp::detail::fec_hw;
namespace fas = ::fixy::atom::simd;
namespace fah = ::fixy::atom::hw;

static_assert(::fixy::atom::IsAtom<fh::ActiveSimdIsa> && fh::ActiveSimdIsa::axis == ::fixy::Axis::SimdIsa);
static_assert(::fixy::atom::IsAtom<fh::InstructionTier> && fh::InstructionTier::axis == ::fixy::Axis::HwInstruction);

// The kernel strides are in bytes and the register width is in bits.
#if defined(__AVX2__)
static_assert(std::is_same_v<fh::ActiveSimdIsa, fas::avx2>);
static_assert(std::is_same_v<fh::InstructionTier, fah::vectorizable>);
static_assert(fh::kKernelStrideBytes * 8U == fas::register_bits_v<fas::SimdIsa::Avx2>);
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
static_assert(std::is_same_v<fh::ActiveSimdIsa, fas::neon>);
static_assert(std::is_same_v<fh::InstructionTier, fah::vectorizable>);
static_assert(fh::kKernelStrideBytes * 8U == fas::register_bits_v<fas::SimdIsa::Neon>);
#else
static_assert(std::is_same_v<fh::ActiveSimdIsa, fas::scalar>, "the portable FEC arm declares the scalar ISA.");
static_assert(std::is_same_v<fh::InstructionTier, fah::scalar>, "the portable FEC arm is the scalar tier.");
static_assert(fh::kKernelStrideBytes == 1, "the portable FEC kernels step one byte per iteration.");
#endif

}  // namespace

int main() {
    // The encode round trip ODR-uses the active arm's kernel, so the pins
    // are checked against code that runs.
    namespace ci = ::crucible::cntp;
    ci::ReedSolomon<4, 2> codec{};

    constexpr std::size_t kPayload = 64;
    std::array<std::byte, kPayload> payload{};
    for (std::size_t i = 0; i < kPayload; ++i)
        payload[i] = static_cast<std::byte>(i & 0xFFU);

    std::array<std::byte, ci::ReedSolomon<4, 2>::encoded_size_for(kPayload)> encoded{};
    const auto enc = codec.encode(std::span<const std::byte>{payload}, encoded);
    if (!enc.has_value()) return 1;
    return 0;
}
