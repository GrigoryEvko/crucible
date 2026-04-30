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

// ═══════════════════════════════════════════════════════════════════
// FOUND-G17-AUDIT: rigor improvements — type identity, reflexive
// satisfies, move semantics, type-level chain composition,
// negative-tier-pin witnesses
// ═══════════════════════════════════════════════════════════════════

// ── 8. Type-identity static_asserts — value_type AND tier together ─
//
// The plain `tier == PhiloxRng` checks above don't catch a refactor
// that accidentally changes the inner value_type (e.g., returning
// `DetSafe<PhiloxRng, std::array<uint32_t, 2>>` instead of
// `DetSafe<PhiloxRng, Ctr>`).  Pin BOTH dimensions: tier AND
// value_type, then prove the full type-identity against the raw
// return type.

static_assert(std::is_same_v<
        decltype(Philox::generate_det(Philox::Ctr{}, Philox::Key{})),
        DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>>,
    "generate_det((Ctr, Key)) MUST return EXACTLY DetSafe<PhiloxRng, Ctr>. "
    "If this fires, the wrapper return type has drifted (e.g., a refactor "
    "renamed Ctr or changed the tier).");

static_assert(std::is_same_v<
        decltype(Philox::generate_det(uint64_t{0}, uint64_t{0})),
        DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>>,
    "generate_det((u64, u64)) MUST return EXACTLY DetSafe<PhiloxRng, Ctr>.");

static_assert(std::is_same_v<
        decltype(Philox::to_uniform_det(0u)),
        DetSafe<DetSafeTier_v::PhiloxRng, float>>,
    "to_uniform_det MUST return EXACTLY DetSafe<PhiloxRng, float>.");

static_assert(std::is_same_v<
        decltype(Philox::to_uniform_d_det(0u)),
        DetSafe<DetSafeTier_v::PhiloxRng, double>>,
    "to_uniform_d_det MUST return EXACTLY DetSafe<PhiloxRng, double>.");

static_assert(std::is_same_v<
        decltype(Philox::box_muller_det(0u, 0u)),
        DetSafe<DetSafeTier_v::PhiloxRng, std::pair<float, float>>>,
    "box_muller_det MUST return EXACTLY DetSafe<PhiloxRng, "
    "std::pair<float, float>>.  If a future refactor re-shapes the "
    "Box-Muller return (e.g., to std::array<float, 2>), this must be "
    "considered an API break and propagated downstream.");

static_assert(std::is_same_v<
        decltype(Philox::op_key_det(0ull, 0u, ContentHash{0})),
        DetSafe<DetSafeTier_v::Pure, uint64_t>>,
    "op_key_det MUST return EXACTLY DetSafe<Pure, uint64_t>.  The Pure "
    "tier is LOAD-BEARING: a refactor downgrading op_key_det to "
    "PhiloxRng would prevent it from satisfying a `requires Pure` gate "
    "downstream (Pure-strictly consumers in kernel-emit precondition "
    "code would break).");

// Public alias visibility — pinning the alias names ensures they are
// reachable from production callers and from the diagnostic surface.
static_assert(std::is_same_v<Philox::DetSafePhiloxCtr,
                             DetSafe<DetSafeTier_v::PhiloxRng, Philox::Ctr>>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloat,
                             DetSafe<DetSafeTier_v::PhiloxRng, float>>);
static_assert(std::is_same_v<Philox::DetSafePhiloxDouble,
                             DetSafe<DetSafeTier_v::PhiloxRng, double>>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloatPair,
                             DetSafe<DetSafeTier_v::PhiloxRng,
                                     std::pair<float, float>>>);
static_assert(std::is_same_v<Philox::DetSafePureKey,
                             DetSafe<DetSafeTier_v::Pure, uint64_t>>);

// value_type accessor — the Graded substrate exposes value_type, the
// wrapper forwards it.  Pin that the wrapper's value_type IS the raw
// inner type, so production code can write `decltype(rng)::value_type
// raw_bytes = std::move(rng).consume()` and have the type system
// prove the unwrapping shape.
static_assert(std::is_same_v<Philox::DetSafePhiloxCtr::value_type, Philox::Ctr>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloat::value_type, float>);
static_assert(std::is_same_v<Philox::DetSafePhiloxDouble::value_type, double>);
static_assert(std::is_same_v<Philox::DetSafePhiloxFloatPair::value_type,
                             std::pair<float, float>>);
static_assert(std::is_same_v<Philox::DetSafePureKey::value_type, uint64_t>);

// ── 9. Negative-tier-pin witnesses — load-bearing rejections ──────
//
// Pin that the wrappers do NOT accidentally claim the WRONG tier.
// A refactor that changes the tier from PhiloxRng → Pure would break
// these.  Symmetric to the positive identity assertions above; the
// specific bug class this catches is "I'll just bump generate_det's
// tier to Pure, that won't break anything" — an incorrect promotion
// that defeats the load-bearing PhiloxRng-boundary classification.

static_assert(!std::is_same_v<
        decltype(Philox::generate_det(uint64_t{0}, uint64_t{0})),
        DetSafe<DetSafeTier_v::Pure, Philox::Ctr>>,
    "generate_det MUST NOT claim Pure tier — Philox-derived bytes are "
    "PhiloxRng-tier, not Pure.  Promoting to Pure would silently lie "
    "about the source class.");

static_assert(!std::is_same_v<
        decltype(Philox::op_key_det(0ull, 0u, ContentHash{0})),
        DetSafe<DetSafeTier_v::PhiloxRng, uint64_t>>,
    "op_key_det MUST claim Pure tier (not PhiloxRng).  op_key is a "
    "pure bit-mix; downgrading the tier loses an information that "
    "downstream `requires Pure` gates depend on.");

// ── 10. Reflexive + cross-tier satisfies coverage ─────────────────

// op_key_det is Pure → satisfies every weaker tier (the entire chain).
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

// generate_det is PhiloxRng → satisfies PhiloxRng-or-weaker but NOT Pure.
using GenResult = decltype(Philox::generate_det(uint64_t{0}, uint64_t{0}));
static_assert( GenResult::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert( GenResult::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert( GenResult::satisfies<DetSafeTier_v::NonDeterministicSyscall>);
static_assert(!GenResult::satisfies<DetSafeTier_v::Pure>,
    "generate_det's PhiloxRng result MUST NOT satisfy Pure — claiming "
    "Pure would mean the bytes are pure-from-declared-inputs (no PRNG "
    "state observable), which is FALSE for Philox output.  This is the "
    "reflexive load-bearing rejection: a future refactor to relax UP "
    "(forbidden) would break this assertion.");

// to_uniform_det / to_uniform_d_det / box_muller_det are PhiloxRng →
// MUST NOT satisfy Pure.  Same load-bearing rejection class.
using UniformResult = decltype(Philox::to_uniform_det(0u));
static_assert(!UniformResult::satisfies<DetSafeTier_v::Pure>);
using UniformDResult = decltype(Philox::to_uniform_d_det(0u));
static_assert(!UniformDResult::satisfies<DetSafeTier_v::Pure>);
using BoxMullerResult = decltype(Philox::box_muller_det(0u, 0u));
static_assert(!BoxMullerResult::satisfies<DetSafeTier_v::Pure>);

// ── 11. Move-semantics witness — consume() on rvalue ──────────────
//
// Pin that `Philox::generate_det(...).consume()` works (rvalue overload
// of DetSafe::consume).  This is structurally important: production
// callers may need to move the value out of the wrapper into a
// destination buffer (e.g., std::array<uint32_t, 4> dst = std::move(
// rng).consume();) without an extra copy.

static void test_move_semantics_through_wrapper() {
    std::printf("  move-semantics through DetSafe wrapper...\n");

    auto rng = Philox::generate_det(uint64_t{1}, uint64_t{2});
    static_assert(std::is_same_v<
        decltype(std::move(rng).consume()), Philox::Ctr>,
        "DetSafe::consume() && MUST return T by value (move).");

    Philox::Ctr extracted = std::move(rng).consume();
    const auto raw = Philox::generate(uint64_t{1}, uint64_t{2});
    assert(extracted[0] == raw[0]);
    assert(extracted[1] == raw[1]);
    assert(extracted[2] == raw[2]);
    assert(extracted[3] == raw[3]);

    // Pure-tier op_key path moves through too.
    auto key_pure = Philox::op_key_det(0xCAFEull, 7u, ContentHash{0xBEEFull});
    uint64_t key_extracted = std::move(key_pure).consume();
    assert(key_extracted == Philox::op_key(0xCAFEull, 7u, ContentHash{0xBEEFull}));
}

// ── 12. Type-level chain composition — generate_det(uint64, DetSafe key) ─
//
// Exercises the third overload added in FOUND-G17-AUDIT.  The key's
// DetSafe-tier flows through the type system end-to-end: a Pure-
// tier key (from op_key_det) feeds a generate_det that REQUIRES
// PhiloxRng-or-stronger.  The compile error if the key tier is too
// weak is the load-bearing rejection.

static_assert(std::is_same_v<
        decltype(Philox::generate_det(uint64_t{0},
                     DetSafe<DetSafeTier_v::Pure, uint64_t>{0ull})),
        Philox::DetSafePhiloxCtr>,
    "generate_det(uint64, DetSafe<Pure, uint64>) MUST return "
    "DetSafePhiloxCtr.  The Pure key satisfies the PhiloxRng-or-stronger "
    "requires-clause.");

static_assert(std::is_same_v<
        decltype(Philox::generate_det(uint64_t{0},
                     DetSafe<DetSafeTier_v::PhiloxRng, uint64_t>{0ull})),
        Philox::DetSafePhiloxCtr>,
    "generate_det(uint64, DetSafe<PhiloxRng, uint64>) MUST return "
    "DetSafePhiloxCtr.  PhiloxRng key satisfies the gate at the boundary.");

// SFINAE detector — proves that the requires-clause REJECTS weaker
// tiers.  Without this, a future relaxation of the requires-clause
// (e.g., "all tiers admissible") would silently break the chain
// composition's load-bearing guarantee.
template <DetSafeTier_v KeyTier>
concept can_compose_chain =
    requires(uint64_t off, DetSafe<KeyTier, uint64_t> k) {
        { Philox::generate_det(off, std::move(k)) }
            -> std::same_as<Philox::DetSafePhiloxCtr>;
    };

static_assert( can_compose_chain<DetSafeTier_v::Pure>);
static_assert( can_compose_chain<DetSafeTier_v::PhiloxRng>);
static_assert(!can_compose_chain<DetSafeTier_v::MonotonicClockRead>,
    "generate_det MUST REJECT a MonotonicClockRead key — clock-read "
    "values cannot be Philox keys without defeating the cross-replay "
    "determinism contract.  THE LOAD-BEARING REJECTION at the chain "
    "composition surface.");
static_assert(!can_compose_chain<DetSafeTier_v::WallClockRead>);
static_assert(!can_compose_chain<DetSafeTier_v::EntropyRead>);
static_assert(!can_compose_chain<DetSafeTier_v::FilesystemMtime>);
static_assert(!can_compose_chain<DetSafeTier_v::NonDeterministicSyscall>);

// Constexpr witness for the type-level chain.
inline constexpr auto kChainConstexpr =
    Philox::generate_det(uint64_t{0},
        DetSafe<DetSafeTier_v::Pure, uint64_t>{42ull});
static_assert(kChainConstexpr.tier == DetSafeTier_v::PhiloxRng);

// Bit-equality of the type-level chain vs the .peek() form.  Both
// MUST produce the same bytes; the type-system version just adds
// compile-time checking, not runtime divergence.
static void test_typed_chain_bit_equal_to_peek_chain() {
    std::printf("  typed chain bit-equality with peek chain...\n");

    constexpr uint64_t offset = 0x1234567890ABCDEFull;
    constexpr uint64_t key_raw = 0xDEADBEEFCAFEBABEull;

    // Form 1: peek()-then-pass.
    auto key_form1 = DetSafe<DetSafeTier_v::Pure, uint64_t>{key_raw};
    auto rng_form1 = Philox::generate_det(offset, key_form1.peek());

    // Form 2: pass DetSafe directly.
    auto rng_form2 = Philox::generate_det(offset,
        DetSafe<DetSafeTier_v::Pure, uint64_t>{key_raw});

    // Form 3: PhiloxRng-tier key directly.
    auto rng_form3 = Philox::generate_det(offset,
        DetSafe<DetSafeTier_v::PhiloxRng, uint64_t>{key_raw});

    assert(rng_form1.peek()[0] == rng_form2.peek()[0]);
    assert(rng_form1.peek()[1] == rng_form2.peek()[1]);
    assert(rng_form1.peek()[2] == rng_form2.peek()[2]);
    assert(rng_form1.peek()[3] == rng_form2.peek()[3]);

    assert(rng_form1.peek()[0] == rng_form3.peek()[0]);
    assert(rng_form1.peek()[1] == rng_form3.peek()[1]);
    assert(rng_form1.peek()[2] == rng_form3.peek()[2]);
    assert(rng_form1.peek()[3] == rng_form3.peek()[3]);

    // And bit-equal to raw.
    auto raw = Philox::generate(offset, key_raw);
    assert(rng_form1.peek()[0] == raw[0]);
    assert(rng_form1.peek()[1] == raw[1]);
    assert(rng_form1.peek()[2] == raw[2]);
    assert(rng_form1.peek()[3] == raw[3]);
}

// ── 13. End-to-end production-style chain (op_key_det + relax + chain) ─
//
// Reformulates the chain test using the type-level overload, so the
// `.peek()`-the-tier-away pattern is replaced with type-system
// composition.

static void test_e2e_typed_chain() {
    std::printf("  end-to-end typed chain: op_key_det → relax → generate_det...\n");

    // Pure-tier key from a Pure-tier mix.
    auto key_pure = Philox::op_key_det(
        0xCAFEBABEDEADBEEFull, 42u,
        ContentHash{0xDEADBEEFCAFEBABEull});
    static_assert(decltype(key_pure)::tier == DetSafeTier_v::Pure);

    // Relax to PhiloxRng — same bytes, weaker tier promise.
    auto key_philox = std::move(key_pure).relax<DetSafeTier_v::PhiloxRng>();
    static_assert(decltype(key_philox)::tier == DetSafeTier_v::PhiloxRng);

    // Pass directly to generate_det's typed overload.  No .peek()
    // erases the tier; the type system verifies admissibility.
    auto rng = Philox::generate_det(/*offset=*/0u, std::move(key_philox));
    static_assert(decltype(rng)::tier == DetSafeTier_v::PhiloxRng);

    // Bit-equality cross-check.
    const uint64_t key_bytes_raw = Philox::op_key(
        0xCAFEBABEDEADBEEFull, 42u,
        ContentHash{0xDEADBEEFCAFEBABEull});
    auto rng_raw = Philox::generate(uint64_t{0}, key_bytes_raw);
    assert(rng.peek()[0] == rng_raw[0]);
    assert(rng.peek()[1] == rng_raw[1]);
    assert(rng.peek()[2] == rng_raw[2]);
    assert(rng.peek()[3] == rng_raw[3]);
}

// ── Main ───────────────────────────────────────────────────────────

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
