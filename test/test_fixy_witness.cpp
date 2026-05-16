// ── test_fixy_witness — FIXY-G9 positive test ─────────────────────────
//
// Exercises the four-tier witness lattice + Grant retrofit +
// FnWitnessAtLeast concept + WireGrade witness-opcode round-trip.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/Witness.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace sw = crucible::safety::witness;

namespace {

// ── Worked example shape 1: bare AllStrict (every grant Asserted) ──
using AllAssertedFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Worked example shape 2: Tested floor on Trust + Reentrancy ──
using TestedFloorFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for_e<cd::Trust, sw::Tested<11>>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cg::reentrant_e<sw::Tested<22>>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Worked example shape 3: CrossValidated on Trust + Provenance ──
using CrossValFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy_e<sw::CrossValidated<3>>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for_e<cd::Provenance, sw::CrossValidated<5>>,
    cf::accept_default_strict_for_e<cd::Trust, sw::CrossValidated<5>>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cg::reentrant_e<sw::Tested<22>>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ─── Compile-time lattice properties ────────────────────────────────

static_assert(sw::witness_tier_v<sw::Asserted<>>             == 1);
static_assert(sw::witness_tier_v<sw::Tested<0>>              == 2);
static_assert(sw::witness_tier_v<sw::CrossValidated<0>>      == 3);
static_assert(sw::witness_tier_v<sw::FormallyVerified<int>>  == 4);

static_assert(sw::witness_leq_v<sw::Asserted<>, sw::Tested<0>>);
static_assert(sw::witness_leq_v<sw::Tested<0>, sw::CrossValidated<0>>);
static_assert(!sw::witness_leq_v<sw::Tested<0>, sw::Asserted<>>);

static_assert(sw::IsWitness<sw::Asserted<>>);
static_assert(sw::IsWitness<sw::Tested<99>>);
static_assert(!sw::IsWitness<int>);

static_assert(sw::WitnessAtLeast<sw::Tested<0>, sw::Asserted<>>);
static_assert(sw::WitnessAtLeast<sw::CrossValidated<0>, sw::Tested<0>>);
static_assert(!sw::WitnessAtLeast<sw::Asserted<>, sw::Tested<0>>);

// ─── Grant retrofit ─────────────────────────────────────────────────

static_assert(std::is_same_v<typename cg::reentrant::witness_t, sw::DefaultWitness>);
static_assert(std::is_same_v<typename cg::reentrant_e<sw::Tested<7>>::witness_t,
                             sw::Tested<7>>);

// ─── FnWitnessAtLeast ───────────────────────────────────────────────

static_assert(std::is_same_v<cf::axis_witness_t<AllAssertedFn, cd::Reentrancy>,
                             sw::DefaultWitness>);
static_assert(std::is_same_v<cf::axis_witness_t<TestedFloorFn, cd::Reentrancy>,
                             sw::Tested<22>>);
static_assert(std::is_same_v<cf::axis_witness_t<TestedFloorFn, cd::Trust>,
                             sw::Tested<11>>);

static_assert(cf::FnWitnessAtLeast<AllAssertedFn, cd::Trust, sw::Asserted<>>);
static_assert(!cf::FnWitnessAtLeast<AllAssertedFn, cd::Trust, sw::Tested<0>>);
static_assert(cf::FnWitnessAtLeast<TestedFloorFn, cd::Trust, sw::Tested<0>>);
static_assert(cf::FnWitnessAtLeast<TestedFloorFn, cd::Reentrancy, sw::Tested<0>>);
static_assert(!cf::FnWitnessAtLeast<TestedFloorFn, cd::Trust,
                                    sw::CrossValidated<0>>);
static_assert(cf::FnWitnessAtLeast<CrossValFn, cd::Trust,
                                    sw::CrossValidated<0>>);

// ─── R016 ──────────────────────────────────────────────────────────

static_assert(!cr::R016_requires_witness_floor_v<AllAssertedFn>,
    "R016: bare Asserted-only Trust + Reentrancy must NOT pass.");
static_assert(cr::R016_requires_witness_floor_v<TestedFloorFn>,
    "R016: Tested floor on both Trust + Reentrancy MUST pass.");
static_assert(cr::R016_requires_witness_floor_v<CrossValFn>,
    "R016: CrossValidated on Trust + Tested on Reentrancy MUST pass.");

// ─── PlatformBounded fallback ───────────────────────────────────────

using AnyArch = sw::arch::current_arch_tag;
static_assert(sw::witness_tier_v<sw::PlatformBounded<sw::Tested<0>, AnyArch>>
              == 2);

#if defined(__x86_64__)
using OtherArch = sw::arch::AArch64;
#elif defined(__aarch64__)
using OtherArch = sw::arch::X86_64;
#else
using OtherArch = sw::arch::X86_64;
#endif
static_assert(sw::witness_tier_v<sw::PlatformBounded<sw::Tested<0>, OtherArch>>
              == 1, "PlatformBounded against non-current arch falls back to Asserted.");

// ─── EBO collapse pin ───────────────────────────────────────────────

static_assert(sizeof(cg::reentrant_e<sw::Tested<0>>) == 1);
static_assert(sizeof(cg::copy_e<sw::CrossValidated<0>>) == 1);
static_assert(sizeof(cf::accept_default_strict_for_e<cd::Trust, sw::Tested<0>>) == 1);

}  // namespace

int main() {
    // Runtime smoke — exercise the constexpr witness/grade surface
    // with non-constant inputs per the algebra-runtime-smoke-test
    // discipline.

    // Tier table runtime read.
    std::uint8_t tier_sum = 0;
    tier_sum = static_cast<std::uint8_t>(
        tier_sum + sw::witness_tier_v<sw::Asserted<>>);
    tier_sum = static_cast<std::uint8_t>(
        tier_sum + sw::witness_tier_v<sw::Tested<99>>);
    tier_sum = static_cast<std::uint8_t>(
        tier_sum + sw::witness_tier_v<sw::CrossValidated<99>>);
    tier_sum = static_cast<std::uint8_t>(
        tier_sum + sw::witness_tier_v<sw::FormallyVerified<int>>);
    if (tier_sum != 10) {
        std::fprintf(stderr, "tier_sum mismatch: %u (expected 10)\n",
                     static_cast<unsigned>(tier_sum));
        return 1;
    }

    // R016 runtime probe.
    bool bare_r016 = cr::R016_requires_witness_floor_v<AllAssertedFn>;
    bool tested_r016 = cr::R016_requires_witness_floor_v<TestedFloorFn>;
    bool crossval_r016 = cr::R016_requires_witness_floor_v<CrossValFn>;
    if (bare_r016 || !tested_r016 || !crossval_r016) {
        std::fprintf(stderr,
            "R016 mismatch: bare=%d tested=%d crossval=%d\n",
            int{bare_r016}, int{tested_r016}, int{crossval_r016});
        return 2;
    }

    // Wire round-trip: encode TestedFloorFn, decode it, expect OK.
    constexpr std::size_t kSize = cf::wire_grade_size_v<TestedFloorFn>;
    std::array<std::uint8_t, kSize> buf{};
    auto written = cf::wire_encode<TestedFloorFn>(buf);
    if (written == 0) {
        std::fprintf(stderr, "wire_encode wrote zero bytes\n");
        return 3;
    }
    auto r_self = cf::wire_decode<TestedFloorFn>(buf);
    if (!r_self.has_value()) {
        std::fprintf(stderr,
            "self-decode failed unexpectedly: %.*s\n",
            static_cast<int>(cf::wire_grade_error_name(r_self.error()).size()),
            cf::wire_grade_error_name(r_self.error()).data());
        return 4;
    }

    // Cross-witness decode: encode bare AllAssertedFn, decode against
    // TestedFloorFn — should reject with WitnessFloor (different witness
    // on Trust + Reentrancy).  Note: grade itself also differs (Trust
    // axis grant differs in opcode for evidenced strict-ack — but
    // current evidenced strict-ack reuses the SAME opcode as bare
    // strict-ack, so the failure is the witness opcode mismatch).
    constexpr std::size_t kBareSize = cf::wire_grade_size_v<AllAssertedFn>;
    std::array<std::uint8_t, kBareSize> bare_buf{};
    [[maybe_unused]] auto bare_written = cf::wire_encode<AllAssertedFn>(bare_buf);
    auto r_cross = cf::wire_decode<TestedFloorFn>(bare_buf);
    if (r_cross.has_value()) {
        std::fprintf(stderr,
            "cross-witness decode should have failed\n");
        return 5;
    }
    if (r_cross.error() != cf::WireGradeError::WitnessFloor) {
        std::fprintf(stderr,
            "cross-witness error %.*s, expected WitnessFloor\n",
            static_cast<int>(cf::wire_grade_error_name(r_cross.error()).size()),
            cf::wire_grade_error_name(r_cross.error()).data());
        return 6;
    }

    std::fputs("test_fixy_witness: OK\n", stdout);
    return 0;
}
