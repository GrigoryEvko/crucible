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

[[nodiscard]] static constexpr uint64_t reference_fnv_mix(uint64_t h, uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFFULL;
        h *= 0x100000001b3ULL;
    }
    return h;
}

[[nodiscard]] static constexpr uint64_t reference_op_key(uint64_t master_counter, uint32_t op_index,
                                                         ContentHash content_hash) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL;
    h = reference_fnv_mix(h, master_counter);
    h = reference_fnv_mix(h, static_cast<uint64_t>(op_index));
    h = reference_fnv_mix(h, content_hash.raw());
    return h;
}

static void test_generate_det_bit_equal_to_raw() {
    std::printf("  generate_det bit-equality with generate...\n");

    // All zeros, all ones, alternating bits, and a mixed pattern.
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

    constexpr uint64_t offsets[] = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0xCAFEBABEDEADBEEFull};
    constexpr uint64_t key64s[] = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0x9E3779B97F4A7C15ull};

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
        0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0xDEADBEEFu, 0xCAFEBABEu, 0x55AA55AAu,
    };
    for (uint32_t x : samples) {
        const float raw_f = Philox::to_uniform(x);
        const float pinned_f = Philox::to_uniform_det(x).peek();
        // Compare the bytes, not the values.  A determinism claim is a
        // claim about bits, and two floats can compare equal without
        // carrying the same bit pattern.
        assert(std::bit_cast<uint32_t>(raw_f) == std::bit_cast<uint32_t>(pinned_f));

        const double raw_d = Philox::to_uniform_d(x);
        const double pinned_d = Philox::to_uniform_d_det(x).peek();
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
        assert(std::bit_cast<uint32_t>(raw.first) == std::bit_cast<uint32_t>(pinned_pair.first));
        assert(std::bit_cast<uint32_t>(raw.second) == std::bit_cast<uint32_t>(pinned_pair.second));
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
                const uint64_t raw = reference_op_key(m, op, ch);
                const uint64_t pinned = Philox::op_key_det(m, op, ch).peek();
                assert(raw == pinned);
            }
        }
    }
}

static_assert(decltype(Philox::generate_det(Philox::Ctr{}, Philox::Key{}))::tier == DetSafeTier_v::PhiloxRng,
              "Philox::generate_det((Ctr, Key)) MUST return DetSafe<PhiloxRng, Ctr>.");

static_assert(decltype(Philox::generate_det(uint64_t{0}, uint64_t{0}))::tier == DetSafeTier_v::PhiloxRng,
              "Philox::generate_det((uint64, uint64)) MUST return DetSafe<PhiloxRng, Ctr>.");

static_assert(decltype(Philox::to_uniform_det(0u))::tier == DetSafeTier_v::PhiloxRng,
              "Philox::to_uniform_det MUST return DetSafe<PhiloxRng, float>.");

static_assert(decltype(Philox::to_uniform_d_det(0u))::tier == DetSafeTier_v::PhiloxRng,
              "Philox::to_uniform_d_det MUST return DetSafe<PhiloxRng, double>.");

static_assert(decltype(Philox::box_muller_det(0u, 0u))::tier == DetSafeTier_v::MonotonicClockRead,
              "Philox::box_muller_det MUST return DetSafe<MonotonicClockRead, "
              "pair<float,float>> — the C library transcendentals (sin/cos/log) "
              "drift by 1-3 ULPs between implementations, so the bytes are NOT "
              "cross-platform bit-equal.");

static_assert(decltype(Philox::box_muller_polynomial_det(0u, 0u))::tier == DetSafeTier_v::PhiloxRng,
              "Philox::box_muller_polynomial_det MUST return DetSafe<PhiloxRng, "
              "pair<float,float>> — it uses IEEE 754 polynomial sin/cos/log and a "
              "correctly-rounded square root, so its bytes are cross-platform "
              "bit-equal.");

static_assert(decltype(Philox::op_key_det(0ull, 0u, ContentHash{0}))::tier == DetSafeTier_v::Pure,
              "Philox::op_key_det MUST return DetSafe<Pure, uint64_t> — its inputs "
              "(master_counter, op_index, content_hash) are all Pure-tier scalars, "
              "so the bit-mix output is itself Pure.  Without this, the chain "
              "compose op_key → relax<PhiloxRng> → generate would lose the type-"
              "level tier promotion.");

// The concept below stands in for the fence a persisted-state writer
// puts in front of every value it records.
template <typename W>
concept admissible_at_cipher_fence = W::template satisfies<DetSafeTier_v::PhiloxRng>;

static_assert(admissible_at_cipher_fence<decltype(Philox::generate_det(uint64_t{0}, uint64_t{0}))>,
              "generate_det's PhiloxRng-pinned result MUST pass the Cipher write-"
              "fence (PhiloxRng = boundary).  If this fires, determinism is no "
              "longer fenced at compile time on the Philox boundary.");

static_assert(admissible_at_cipher_fence<decltype(Philox::to_uniform_det(0u))>,
              "to_uniform_det's PhiloxRng-pinned result MUST pass the Cipher write-fence.");

static_assert(admissible_at_cipher_fence<decltype(Philox::to_uniform_d_det(0u))>,
              "to_uniform_d_det's PhiloxRng-pinned result MUST pass the Cipher write-fence.");

// The rejection below is deliberate.  It guards against a promotion
// of the tier back to PhiloxRng that leaves the C library dependency
// in place.
static_assert(!admissible_at_cipher_fence<decltype(Philox::box_muller_det(0u, 0u))>,
              "box_muller_det's MonotonicClockRead-pinned result MUST NOT pass "
              "the Cipher write-fence — the C library sin/cos/log it calls is "
              "not cross-platform bit-stable.");

static_assert(admissible_at_cipher_fence<decltype(Philox::box_muller_polynomial_det(0u, 0u))>,
              "box_muller_polynomial_det's PhiloxRng-pinned result MUST pass the "
              "Cipher write-fence — polynomial transcendentals are cross-platform "
              "bit-equal.");

static_assert(admissible_at_cipher_fence<decltype(Philox::op_key_det(0ull, 0u, ContentHash{0}))>,
              "op_key_det's Pure-pinned result MUST pass the Cipher write-fence "
              "(Pure ⊒ PhiloxRng by lattice direction).  If this fires, the "
              "lattice subsumption-up direction has regressed.");

static_assert(!admissible_at_cipher_fence<DetSafe<DetSafeTier_v::MonotonicClockRead, uint64_t>>,
              "DetSafe<MonotonicClockRead, uint64_t> MUST NOT pass the Cipher "
              "write-fence.  If this fires, a clock read can reach the replay "
              "log undetected and replay stops being deterministic.");

static_assert(!admissible_at_cipher_fence<DetSafe<DetSafeTier_v::WallClockRead, uint64_t>>,
              "DetSafe<WallClockRead, uint64_t> MUST NOT pass the Cipher write-fence.");

static_assert(!admissible_at_cipher_fence<DetSafe<DetSafeTier_v::EntropyRead, uint64_t>>,
              "DetSafe<EntropyRead, ...> MUST NOT pass the Cipher write-fence — "
              "/dev/urandom-derived bytes are trivially replay-unsafe.");

static_assert(!admissible_at_cipher_fence<DetSafe<DetSafeTier_v::FilesystemMtime, uint64_t>>,
              "DetSafe<FilesystemMtime, ...> MUST NOT pass the Cipher write-fence.");

static_assert(!admissible_at_cipher_fence<DetSafe<DetSafeTier_v::NonDeterministicSyscall, uint64_t>>,
              "DetSafe<NonDeterministicSyscall, ...> MUST NOT pass the Cipher write-fence.");

static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>) == sizeof(Philox::Ctr),
              "DetSafe<PhiloxRng, Ctr> MUST be byte-equal to bare Ctr.  If this "
              "fires, the wrapper has acquired storage of its own and is no "
              "longer free.");

static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, float>) == sizeof(float));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, double>) == sizeof(double));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, std::pair<float, float>>) == sizeof(std::pair<float, float>));
static_assert(sizeof(DetSafe<DetSafeTier_v::Pure, uint64_t>) == sizeof(uint64_t));

static void test_chain_composition() {
    std::printf("  chain composition: op_key_det → generate_det...\n");

    const auto key_pure = Philox::op_key_det(
        /*master=*/0xCAFEBABEDEADBEEFull,
        /*op_index=*/42u, ContentHash{0xDEADBEEFCAFEBABEull});
    static_assert(decltype(key_pure)::tier == DetSafeTier_v::Pure);

    // Pure sits above PhiloxRng, so it satisfies it.
    static_assert(decltype(key_pure)::satisfies<DetSafeTier_v::PhiloxRng>);

    // A caller that reaches the primitive overload has to peek the
    // bytes out first, which drops the tier.
    const uint64_t key_bytes = key_pure.peek();
    const auto rng = Philox::generate_det(/*offset=*/0u, key_bytes);
    static_assert(decltype(rng)::tier == DetSafeTier_v::PhiloxRng);

    const uint64_t key_bytes_raw = reference_op_key(0xCAFEBABEDEADBEEFull, 42u, ContentHash{0xDEADBEEFCAFEBABEull});
    assert(key_bytes == key_bytes_raw);

    const auto rng_raw = Philox::generate(uint64_t{0}, key_bytes_raw);
    assert(rng.peek()[0] == rng_raw[0]);
    assert(rng.peek()[1] == rng_raw[1]);
    assert(rng.peek()[2] == rng_raw[2]);
    assert(rng.peek()[3] == rng_raw[3]);
}

// Every wrapper here is constexpr-callable, and these variables force
// that at compile time.  Box-Muller is absent because the standard
// library transcendentals it calls are not constexpr.

inline constexpr auto kCheckGenerateDetConstexpr = Philox::generate_det(uint64_t{1}, uint64_t{2});
static_assert(kCheckGenerateDetConstexpr.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto kCheckUniformDetConstexpr = Philox::to_uniform_det(0xDEADBEEFu);
static_assert(kCheckUniformDetConstexpr.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto kCheckUniformDDetConstexpr = Philox::to_uniform_d_det(0xDEADBEEFu);
static_assert(kCheckUniformDDetConstexpr.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto kCheckOpKeyDetConstexpr = Philox::op_key_det(0ull, 0u, ContentHash{0});
static_assert(kCheckOpKeyDetConstexpr.tier == DetSafeTier_v::Pure);

// The tier checks above say nothing about the wrapped type, so a
// refactor that keeps the tier and changes the inner type slips past
// them.  The assertions below pin the whole return type.

static_assert(std::is_same_v<decltype(Philox::generate_det(Philox::Ctr{}, Philox::Key{})),
                             DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>>,
              "generate_det((Ctr, Key)) MUST return EXACTLY DetSafe<PhiloxRng, Ctr>. "
              "If this fires, the wrapper return type has drifted (e.g., a refactor "
              "renamed Ctr or changed the tier).");

static_assert(std::is_same_v<decltype(Philox::generate_det(uint64_t{0}, uint64_t{0})),
                             DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>>,
              "generate_det((u64, u64)) MUST return EXACTLY DetSafe<PhiloxRng, Ctr>.");

static_assert(std::is_same_v<decltype(Philox::to_uniform_det(0u)), DetSafe<DetSafeTier_v::PhiloxRng, float>>,
              "to_uniform_det MUST return EXACTLY DetSafe<PhiloxRng, float>.");

static_assert(std::is_same_v<decltype(Philox::to_uniform_d_det(0u)), DetSafe<DetSafeTier_v::PhiloxRng, double>>,
              "to_uniform_d_det MUST return EXACTLY DetSafe<PhiloxRng, double>.");

static_assert(std::is_same_v<decltype(Philox::box_muller_det(0u, 0u)),
                             DetSafe<DetSafeTier_v::MonotonicClockRead, std::pair<float, float>>>,
              "box_muller_det MUST return EXACTLY DetSafe<MonotonicClockRead, "
              "std::pair<float, float>>.  Re-shaping the Box-Muller return (for "
              "example to std::array<float, 2>) is an API break and has to be "
              "propagated downstream.");

static_assert(std::is_same_v<decltype(Philox::box_muller_polynomial_det(0u, 0u)),
                             DetSafe<DetSafeTier_v::PhiloxRng, std::pair<float, float>>>,
              "box_muller_polynomial_det MUST return EXACTLY DetSafe<PhiloxRng, "
              "std::pair<float, float>> — it is cross-platform bit-stable through "
              "IEEE 754 polynomial sin/cos/log.");

static_assert(
    std::is_same_v<decltype(Philox::op_key_det(0ull, 0u, ContentHash{0})), DetSafe<DetSafeTier_v::Pure, uint64_t>>,
    "op_key_det MUST return EXACTLY DetSafe<Pure, uint64_t>.  The Pure "
    "tier is LOAD-BEARING: a refactor downgrading op_key_det to "
    "PhiloxRng would prevent it from satisfying a `requires Pure` gate "
    "downstream (Pure-strictly consumers in kernel-emit precondition "
    "code would break).");

// Naming the public aliases here keeps them reachable from callers
// and from the diagnostic surface.
static_assert(std::is_same_v<Philox::DetSafePhiloxCtr, DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloat, DetSafe<DetSafeTier_v::PhiloxRng, float>>);
static_assert(std::is_same_v<Philox::DetSafePhiloxDouble, DetSafe<DetSafeTier_v::PhiloxRng, double>>);
static_assert(
    std::is_same_v<Philox::DetSafePhiloxFloatPair, DetSafe<DetSafeTier_v::PhiloxRng, std::pair<float, float>>>);
static_assert(std::is_same_v<Philox::DetSafePureKey, DetSafe<DetSafeTier_v::Pure, uint64_t>>);

// A caller writes `decltype(rng)::value_type bytes = std::move(rng)
// .consume()` and expects the declared type to be the unwrapped one.
static_assert(std::is_same_v<Philox::DetSafePhiloxCtr::value_type, Philox::Ctr>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloat::value_type, float>);
static_assert(std::is_same_v<Philox::DetSafePhiloxDouble::value_type, double>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloatPair::value_type, std::pair<float, float>>);
static_assert(std::is_same_v<Philox::DetSafePureKey::value_type, uint64_t>);

// The positive assertions above prove a wrapper claims the tier it
// should.  These prove it does not claim a stronger one.  The bug
// they catch is a promotion to Pure made because it seemed harmless,
// which quietly erases the boundary the PhiloxRng tier marks.

static_assert(!std::is_same_v<decltype(Philox::generate_det(uint64_t{0}, uint64_t{0})),
                              DetSafe<DetSafeTier_v::Pure, Philox::Ctr>>,
              "generate_det MUST NOT claim Pure tier — Philox-derived bytes are "
              "PhiloxRng-tier, not Pure.  Promoting to Pure would silently lie "
              "about the source class.");

static_assert(!std::is_same_v<decltype(Philox::op_key_det(0ull, 0u, ContentHash{0})),
                              DetSafe<DetSafeTier_v::PhiloxRng, uint64_t>>,
              "op_key_det MUST claim Pure tier (not PhiloxRng).  op_key is a "
              "pure bit-mix; downgrading the tier loses an information that "
              "downstream `requires Pure` gates depend on.");

// A Pure result satisfies every tier below it, down to the weakest.
using OpKeyResult = decltype(Philox::op_key_det(0ull, 0u, ContentHash{0}));
static_assert(OpKeyResult::satisfies<DetSafeTier_v::Pure>,
              "op_key_det's Pure result MUST satisfy Pure (reflexivity at top).");
static_assert(OpKeyResult::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(OpKeyResult::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert(OpKeyResult::satisfies<DetSafeTier_v::WallClockRead>);
static_assert(OpKeyResult::satisfies<DetSafeTier_v::EntropyRead>);
static_assert(OpKeyResult::satisfies<DetSafeTier_v::FilesystemMtime>);
static_assert(OpKeyResult::satisfies<DetSafeTier_v::NonDeterministicSyscall>,
              "op_key_det's Pure result MUST satisfy NDS (the bottom of the chain). "
              "Pure-tier bytes are admissible at every consumer including the "
              "weakest one.");

// A PhiloxRng result satisfies its own tier and everything weaker,
// and stops there.
using GenResult = decltype(Philox::generate_det(uint64_t{0}, uint64_t{0}));
static_assert(GenResult::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(GenResult::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert(GenResult::satisfies<DetSafeTier_v::NonDeterministicSyscall>);
static_assert(!GenResult::satisfies<DetSafeTier_v::Pure>,
              "generate_det's PhiloxRng result MUST NOT satisfy Pure — claiming "
              "Pure would mean the bytes are pure-from-declared-inputs (no PRNG "
              "state observable), which is FALSE for Philox output.  This is the "
              "reflexive load-bearing rejection: a future refactor to relax UP "
              "(forbidden) would break this assertion.");

using UniformResult = decltype(Philox::to_uniform_det(0u));
static_assert(!UniformResult::satisfies<DetSafeTier_v::Pure>);
using UniformDResult = decltype(Philox::to_uniform_d_det(0u));
static_assert(!UniformDResult::satisfies<DetSafeTier_v::Pure>);
using BoxMullerResult = decltype(Philox::box_muller_det(0u, 0u));
static_assert(!BoxMullerResult::satisfies<DetSafeTier_v::Pure>);

// A caller emptying the wrapper into a destination buffer must not
// pay for a copy on the way out.

static void test_move_semantics_through_wrapper() {
    std::printf("  move-semantics through DetSafe wrapper...\n");

    auto rng = Philox::generate_det(uint64_t{1}, uint64_t{2});
    static_assert(std::is_same_v<decltype(std::move(rng).consume()), Philox::Ctr>,
                  "DetSafe::consume() && MUST return T by value (move).");

    Philox::Ctr extracted = std::move(rng).consume();
    const auto raw = Philox::generate(uint64_t{1}, uint64_t{2});
    assert(extracted[0] == raw[0]);
    assert(extracted[1] == raw[1]);
    assert(extracted[2] == raw[2]);
    assert(extracted[3] == raw[3]);

    auto key_pure = Philox::op_key_det(0xCAFEull, 7u, ContentHash{0xBEEFull});
    uint64_t key_extracted = std::move(key_pure).consume();
    assert(key_extracted == reference_op_key(0xCAFEull, 7u, ContentHash{0xBEEFull}));
}

// One overload takes the key still wrapped, so the key's tier reaches
// the generator's requires-clause instead of being peeked away.  A key
// too weak to be admitted is then a compile error rather than a silent
// pass.

static_assert(std::is_same_v<decltype(Philox::generate_det(uint64_t{0}, DetSafe<DetSafeTier_v::Pure, uint64_t>{0ull})),
                             Philox::DetSafePhiloxCtr>,
              "generate_det(uint64, DetSafe<Pure, uint64>) MUST return "
              "DetSafePhiloxCtr.  The Pure key satisfies the PhiloxRng-or-stronger "
              "requires-clause.");

static_assert(
    std::is_same_v<decltype(Philox::generate_det(uint64_t{0}, DetSafe<DetSafeTier_v::PhiloxRng, uint64_t>{0ull})),
                   Philox::DetSafePhiloxCtr>,
    "generate_det(uint64, DetSafe<PhiloxRng, uint64>) MUST return "
    "DetSafePhiloxCtr.  PhiloxRng key satisfies the gate at the boundary.");

// A requires-clause that admits everything compiles just as happily
// as one that admits the right things, so the rejections need their
// own witness.  This concept turns each one into a testable value.
template <DetSafeTier_v KeyTier>
concept can_compose_chain = requires(uint64_t off, DetSafe<KeyTier, uint64_t> k) {
    { Philox::generate_det(off, std::move(k)) } -> std::same_as<Philox::DetSafePhiloxCtr>;
};

static_assert(can_compose_chain<DetSafeTier_v::Pure>);
static_assert(can_compose_chain<DetSafeTier_v::PhiloxRng>);
static_assert(!can_compose_chain<DetSafeTier_v::MonotonicClockRead>,
              "generate_det MUST REJECT a MonotonicClockRead key — a clock read "
              "used as a Philox key defeats the cross-replay determinism "
              "contract.");
static_assert(!can_compose_chain<DetSafeTier_v::WallClockRead>);
static_assert(!can_compose_chain<DetSafeTier_v::EntropyRead>);
static_assert(!can_compose_chain<DetSafeTier_v::FilesystemMtime>);
static_assert(!can_compose_chain<DetSafeTier_v::NonDeterministicSyscall>);

inline constexpr auto kChainConstexpr =
    Philox::generate_det(uint64_t{0}, DetSafe<DetSafeTier_v::Pure, uint64_t>{42ull});
static_assert(kChainConstexpr.tier == DetSafeTier_v::PhiloxRng);

// Passing the key wrapped buys compile-time checking and nothing
// else.  The bytes it produces are the bytes the peeked form
// produces.
static void test_typed_chain_bit_equal_to_peek_chain() {
    std::printf("  typed chain bit-equality with peek chain...\n");

    constexpr uint64_t offset = 0x1234567890ABCDEFull;
    constexpr uint64_t key_raw = 0xDEADBEEFCAFEBABEull;

    auto key_form1 = DetSafe<DetSafeTier_v::Pure, uint64_t>{key_raw};
    auto rng_form1 = Philox::generate_det(offset, key_form1.peek());

    auto rng_form2 = Philox::generate_det(offset, DetSafe<DetSafeTier_v::Pure, uint64_t>{key_raw});

    auto rng_form3 = Philox::generate_det(offset, DetSafe<DetSafeTier_v::PhiloxRng, uint64_t>{key_raw});

    assert(rng_form1.peek()[0] == rng_form2.peek()[0]);
    assert(rng_form1.peek()[1] == rng_form2.peek()[1]);
    assert(rng_form1.peek()[2] == rng_form2.peek()[2]);
    assert(rng_form1.peek()[3] == rng_form2.peek()[3]);

    assert(rng_form1.peek()[0] == rng_form3.peek()[0]);
    assert(rng_form1.peek()[1] == rng_form3.peek()[1]);
    assert(rng_form1.peek()[2] == rng_form3.peek()[2]);
    assert(rng_form1.peek()[3] == rng_form3.peek()[3]);

    auto raw = Philox::generate(offset, key_raw);
    assert(rng_form1.peek()[0] == raw[0]);
    assert(rng_form1.peek()[1] == raw[1]);
    assert(rng_form1.peek()[2] == raw[2]);
    assert(rng_form1.peek()[3] == raw[3]);
}

// The same chain again, with the wrapped-key overload in place of the
// peeked one, so no step in it drops the tier.

static void test_e2e_typed_chain() {
    std::printf("  end-to-end typed chain: op_key_det → relax → generate_det...\n");

    auto key_pure = Philox::op_key_det(0xCAFEBABEDEADBEEFull, 42u, ContentHash{0xDEADBEEFCAFEBABEull});
    static_assert(decltype(key_pure)::tier == DetSafeTier_v::Pure);

    // Relaxing keeps the bytes and weakens only the promise.
    auto key_philox = std::move(key_pure).relax<DetSafeTier_v::PhiloxRng>();
    static_assert(decltype(key_philox)::tier == DetSafeTier_v::PhiloxRng);

    auto rng = Philox::generate_det(/*offset=*/0u, std::move(key_philox));
    static_assert(decltype(rng)::tier == DetSafeTier_v::PhiloxRng);

    const uint64_t key_bytes_raw = reference_op_key(0xCAFEBABEDEADBEEFull, 42u, ContentHash{0xDEADBEEFCAFEBABEull});
    auto rng_raw = Philox::generate(uint64_t{0}, key_bytes_raw);
    assert(rng.peek()[0] == rng_raw[0]);
    assert(rng.peek()[1] == rng_raw[1]);
    assert(rng.peek()[2] == rng_raw[2]);
    assert(rng.peek()[3] == rng_raw[3]);
}

int main() {
    std::printf("test_philox_det\n");

    test_generate_det_bit_equal_to_raw();
    test_to_uniform_det_bit_equal();
    test_box_muller_det_bit_equal();
    test_op_key_det_bit_equal();
    test_chain_composition();
    test_move_semantics_through_wrapper();
    test_typed_chain_bit_equal_to_peek_chain();
    test_e2e_typed_chain();

    std::printf("PASS\n");
    return 0;
}
