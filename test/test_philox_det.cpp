// FOUND-G17: Philox.h DetSafe-pinned production surface.
//
// Verifies the `_det` wrappers added to `Philox` for the FOUND-G17
// production-call-site task.  Five claims:
//
//   1. Bit-equality with raw — `generate_det(a, b).peek() ==
//      generate(a, b)` for the (Ctr, Key) and (uint64, uint64)
//      overloads, AND for `to_uniform_det`, `to_uniform_d_det`,
//      `box_muller_det`, `op_key_det`.
//
//   2. Pinned tier — every `_det` return type carries the expected
//      DetSafeTier_v at the type level (PhiloxRng for the four RNG
//      surfaces; Pure for `op_key_det`).
//
//   3. Cipher write-fence simulation — a function with the
//      shape `requires DetSafe<...>::satisfies<PhiloxRng>` accepts
//      every `_det` result (PhiloxRng directly; Pure via
//      subsumption).
//
//   4. Negative witness — a `DetSafe<MonotonicClockRead, ...>`
//      cannot pass through the same fence (this is the load-bearing
//      rejection that makes the 8th axiom compile-time-fenced).
//
//   5. Layout invariant — `sizeof(DetSafe<Tier, T>) == sizeof(T)`
//      already proven in DetSafe.h's static_asserts; pinned here
//      again at the production-callsite-instantiated types so a
//      future regression in `Philox::DetSafePhiloxCtr` (or the
//      friendlier alias) gets caught at compile time.

#include <crucible/Philox.h>
#include <crucible/safety/DetSafe.h>

#include "test_assert.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>

using namespace crucible;
using safety::DetSafe;
using safety::DetSafeTier_v;

// ── 1. Bit-equality between raw and DetSafe-pinned variants ────────

static void test_generate_det_bit_equal_to_raw() {
    std::printf("  generate_det bit-equality with generate...\n");

    // Cover edge cases (all zeros, all ones, alternating, mixed).
    constexpr Philox::Ctr ctrs[] = {
        {0, 0, 0, 0},
        {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
        {0xDEADBEEFu, 0xCAFEBABEu, 0x55AA55AAu, 0x12345678u},
        {1, 0, 0, 0},
    };
    constexpr Philox::Key keys[] = {
        {0, 0},
        {0xFFFFFFFFu, 0xFFFFFFFFu},
        {0x9E3779B9u, 0xBB67AE85u},  // Weyl-style key
        {1, 0},
    };

    for (const auto& ctr : ctrs) {
        for (const auto& key : keys) {
            const auto raw = Philox::generate(ctr, key);
            const auto pinned = Philox::generate_det(ctr, key);
            const auto pinned_unwrapped = pinned.peek();

            assert(raw[0] == pinned_unwrapped[0]);
            assert(raw[1] == pinned_unwrapped[1]);
            assert(raw[2] == pinned_unwrapped[2]);
            assert(raw[3] == pinned_unwrapped[3]);
        }
    }

    // (uint64, uint64) overload.
    constexpr uint64_t offsets[] = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0xCAFEBABEDEADBEEFull};
    constexpr uint64_t key64s[]  = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0x9E3779B97F4A7C15ull};

    for (uint64_t offset : offsets) {
        for (uint64_t key : key64s) {
            const auto raw = Philox::generate(offset, key);
            const auto pinned = Philox::generate_det(offset, key);
            const auto pu = pinned.peek();
            assert(raw[0] == pu[0]);
            assert(raw[1] == pu[1]);
            assert(raw[2] == pu[2]);
            assert(raw[3] == pu[3]);
        }
    }
}

static void test_to_uniform_det_bit_equal() {
    std::printf("  to_uniform_det / to_uniform_d_det bit-equality...\n");

    constexpr uint32_t samples[] = {
        0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu,
        0xDEADBEEFu, 0xCAFEBABEu, 0x55AA55AAu,
    };
    for (uint32_t x : samples) {
        const float raw_f      = Philox::to_uniform(x);
        const float pinned_f   = Philox::to_uniform_det(x).peek();
        // Float bit-equality (DetSafe is the 8th axiom; bytes MUST match).
        assert(std::bit_cast<uint32_t>(raw_f) == std::bit_cast<uint32_t>(pinned_f));

        const double raw_d     = Philox::to_uniform_d(x);
        const double pinned_d  = Philox::to_uniform_d_det(x).peek();
        assert(std::bit_cast<uint64_t>(raw_d) == std::bit_cast<uint64_t>(pinned_d));
    }
}

static void test_box_muller_det_bit_equal() {
    std::printf("  box_muller_det bit-equality...\n");

    constexpr std::pair<uint32_t, uint32_t> pairs[] = {
        {1u, 2u},
        {0xDEADBEEFu, 0xCAFEBABEu},
        {0x55555555u, 0xAAAAAAAAu},
        {0x12345678u, 0x9ABCDEF0u},
    };
    for (auto [u1, u2] : pairs) {
        const auto raw = Philox::box_muller(u1, u2);
        const auto pinned_pair = Philox::box_muller_det(u1, u2).peek();
        // Float bit-equality.
        assert(std::bit_cast<uint32_t>(raw.first)   == std::bit_cast<uint32_t>(pinned_pair.first));
        assert(std::bit_cast<uint32_t>(raw.second)  == std::bit_cast<uint32_t>(pinned_pair.second));
    }
}

static void test_op_key_det_bit_equal() {
    std::printf("  op_key_det bit-equality...\n");

    constexpr uint64_t masters[] = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0xCAFEBABEDEADBEEFull};
    constexpr uint32_t op_indices[] = {0, 1, 0xFFFFFFFFu, 42u};
    const ContentHash content_hashes[] = {
        ContentHash{0},
        ContentHash{1},
        ContentHash{0xDEADBEEFCAFEBABEull},
    };
    for (uint64_t m : masters) {
        for (uint32_t op : op_indices) {
            for (auto ch : content_hashes) {
                const uint64_t raw   = Philox::op_key(m, op, ch);
                const uint64_t pinned = Philox::op_key_det(m, op, ch).peek();
                assert(raw == pinned);
            }
        }
    }
}

// ── 2. Pinned tier carries through the type system ─────────────────

static_assert(decltype(Philox::generate_det(Philox::Ctr{}, Philox::Key{}))::tier
              == DetSafeTier_v::PhiloxRng,
    "Philox::generate_det((Ctr, Key)) MUST return DetSafe<PhiloxRng, Ctr>.  "
    "FOUND-G17 production-callsite tier-pin gone.");

static_assert(decltype(Philox::generate_det(uint64_t{0}, uint64_t{0}))::tier
              == DetSafeTier_v::PhiloxRng,
    "Philox::generate_det((uint64, uint64)) MUST return DetSafe<PhiloxRng, Ctr>.");

static_assert(decltype(Philox::to_uniform_det(0u))::tier
              == DetSafeTier_v::PhiloxRng,
    "Philox::to_uniform_det MUST return DetSafe<PhiloxRng, float>.");

static_assert(decltype(Philox::to_uniform_d_det(0u))::tier
              == DetSafeTier_v::PhiloxRng,
    "Philox::to_uniform_d_det MUST return DetSafe<PhiloxRng, double>.");

static_assert(decltype(Philox::box_muller_det(0u, 0u))::tier
              == DetSafeTier_v::PhiloxRng,
    "Philox::box_muller_det MUST return DetSafe<PhiloxRng, pair<float,float>>.");

static_assert(decltype(Philox::op_key_det(0ull, 0u, ContentHash{0}))::tier
              == DetSafeTier_v::Pure,
    "Philox::op_key_det MUST return DetSafe<Pure, uint64_t> — its inputs "
    "(master_counter, op_index, content_hash) are all Pure-tier scalars, "
    "so the bit-mix output is itself Pure.  Without this, the chain "
    "compose op_key → relax<PhiloxRng> → generate would lose the type-"
    "level tier promotion.");

// ── 3. Cipher write-fence simulation — accept PhiloxRng-or-stronger ─

template <typename W>
concept admissible_at_cipher_fence =
    W::template satisfies<DetSafeTier_v::PhiloxRng>;

static_assert(admissible_at_cipher_fence<
        decltype(Philox::generate_det(uint64_t{0}, uint64_t{0}))>,
    "generate_det's PhiloxRng-pinned result MUST pass the Cipher write-"
    "fence (PhiloxRng = boundary).  If this fires, the 8th axiom is "
    "no longer compile-time-fenced at the Philox boundary.");

static_assert(admissible_at_cipher_fence<
        decltype(Philox::to_uniform_det(0u))>,
    "to_uniform_det's PhiloxRng-pinned result MUST pass the Cipher write-fence.");

static_assert(admissible_at_cipher_fence<
        decltype(Philox::to_uniform_d_det(0u))>,
    "to_uniform_d_det's PhiloxRng-pinned result MUST pass the Cipher write-fence.");

static_assert(admissible_at_cipher_fence<
        decltype(Philox::box_muller_det(0u, 0u))>,
    "box_muller_det's PhiloxRng-pinned result MUST pass the Cipher write-fence.");

// op_key_det is Pure → strictly stronger than PhiloxRng → admissible.
static_assert(admissible_at_cipher_fence<
        decltype(Philox::op_key_det(0ull, 0u, ContentHash{0}))>,
    "op_key_det's Pure-pinned result MUST pass the Cipher write-fence "
    "(Pure ⊒ PhiloxRng by lattice direction).  If this fires, the "
    "lattice subsumption-up direction has regressed.");

// ── 4. Negative witness — non-PhiloxRng tiers MUST be rejected ─────

static_assert(!admissible_at_cipher_fence<
        DetSafe<DetSafeTier_v::MonotonicClockRead, uint64_t>>,
    "DetSafe<MonotonicClockRead, uint64_t> MUST NOT pass the Cipher "
    "write-fence — this is THE LOAD-BEARING REJECTION.  If this "
    "fires, clock reads can flow into the replay log undetected and "
    "the 8th axiom is silently defeated.");

static_assert(!admissible_at_cipher_fence<
        DetSafe<DetSafeTier_v::WallClockRead, uint64_t>>,
    "DetSafe<WallClockRead, uint64_t> MUST NOT pass the Cipher write-fence.");

static_assert(!admissible_at_cipher_fence<
        DetSafe<DetSafeTier_v::EntropyRead, uint64_t>>,
    "DetSafe<EntropyRead, ...> MUST NOT pass the Cipher write-fence — "
    "/dev/urandom-derived bytes are trivially replay-unsafe.");

static_assert(!admissible_at_cipher_fence<
        DetSafe<DetSafeTier_v::FilesystemMtime, uint64_t>>,
    "DetSafe<FilesystemMtime, ...> MUST NOT pass the Cipher write-fence.");

static_assert(!admissible_at_cipher_fence<
        DetSafe<DetSafeTier_v::NonDeterministicSyscall, uint64_t>>,
    "DetSafe<NonDeterministicSyscall, ...> MUST NOT pass the Cipher write-fence.");

// ── 5. Layout invariant pinned at the production-instantiated types ─

static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>)
              == sizeof(Philox::Ctr),
    "FOUND-G17 zero-cost claim: DetSafe<PhiloxRng, Ctr> MUST be byte-"
    "equal to bare Ctr.  If this fires, the production wrapper has "
    "introduced a runtime cost that breaks the §XVI EBO-collapse rule.");

static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, float>) == sizeof(float));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, double>) == sizeof(double));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, std::pair<float, float>>)
              == sizeof(std::pair<float, float>));
static_assert(sizeof(DetSafe<DetSafeTier_v::Pure, uint64_t>) == sizeof(uint64_t));

// ── 6. End-to-end chain: op_key_det → relax → generate_det ─────────
//
// Demonstrates the production-call-site composition pattern: a Pure-
// tier key (from op_key_det) flows into generate_det as a
// PhiloxRng-tier consumer.  The chain is type-checked end-to-end.

static void test_chain_composition() {
    std::printf("  chain composition: op_key_det → generate_det...\n");

    // Pure-tier key from a Pure-tier mix.
    const auto key_pure = Philox::op_key_det(
        /*master=*/0xCAFEBABEDEADBEEFull,
        /*op_index=*/42u,
        ContentHash{0xDEADBEEFCAFEBABEull});
    static_assert(decltype(key_pure)::tier == DetSafeTier_v::Pure);

    // Subsumption check: Pure satisfies PhiloxRng (Pure ⊒ PhiloxRng).
    static_assert(decltype(key_pure)::satisfies<DetSafeTier_v::PhiloxRng>);

    // Reach into the key bytes (production code may .peek() the key
    // and pass it to generate_det's primitive overload).
    const uint64_t key_bytes = key_pure.peek();
    const auto rng = Philox::generate_det(/*offset=*/0u, key_bytes);
    static_assert(decltype(rng)::tier == DetSafeTier_v::PhiloxRng);

    // Cross-check raw equivalence — same key yields same bytes.
    const uint64_t key_bytes_raw = Philox::op_key(
        0xCAFEBABEDEADBEEFull, 42u,
        ContentHash{0xDEADBEEFCAFEBABEull});
    assert(key_bytes == key_bytes_raw);

    const auto rng_raw = Philox::generate(uint64_t{0}, key_bytes_raw);
    assert(rng.peek()[0] == rng_raw[0]);
    assert(rng.peek()[1] == rng_raw[1]);
    assert(rng.peek()[2] == rng_raw[2]);
    assert(rng.peek()[3] == rng_raw[3]);
}

// ── 7. constexpr surface — wrappers are constexpr-callable ─────────
//
// All non-box_muller `_det` wrappers MUST be constexpr.  Pin this so
// a future refactor that accidentally removes constexpr (e.g., by
// adding a non-constexpr DetSafe ctor path) is caught at compile
// time.  box_muller is excluded because std::sin/cos/log/sqrt are
// not C++26 constexpr.

inline constexpr auto kCheckGenerateDetConstexpr =
    Philox::generate_det(uint64_t{1}, uint64_t{2});
static_assert(kCheckGenerateDetConstexpr.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto kCheckUniformDetConstexpr = Philox::to_uniform_det(0xDEADBEEFu);
static_assert(kCheckUniformDetConstexpr.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto kCheckUniformDDetConstexpr = Philox::to_uniform_d_det(0xDEADBEEFu);
static_assert(kCheckUniformDDetConstexpr.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto kCheckOpKeyDetConstexpr =
    Philox::op_key_det(0ull, 0u, ContentHash{0});
static_assert(kCheckOpKeyDetConstexpr.tier == DetSafeTier_v::Pure);

// ── Main ───────────────────────────────────────────────────────────

int main() {
    std::printf("test_philox_det\n");

    test_generate_det_bit_equal_to_raw();
    test_to_uniform_det_bit_equal();
    test_box_muller_det_bit_equal();
    test_op_key_det_bit_equal();
    test_chain_composition();

    std::printf("PASS\n");
    return 0;
}
