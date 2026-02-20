#pragma once

// Philox4x32-10: counter-based PRNG for deterministic computation.
//
// From "Parallel Random Numbers: As Easy as 1, 2, 3" (Salmon et al., 2011).
// Properties that matter for Crucible:
//   - Stateless: same (counter, key) → same output on any hardware
//   - Fast: ~10 integer ops per round, 4 outputs per call (~2.5 ops/output)
//   - Parallelizable: independent per-element, no sequential state
//   - Cryptographically inspired: 10 rounds of S-box-like mixing
//
// Usage in the Crucible pipeline:
//   uint64_t key = op_key(master_counter, op_index, content_hash);
//   auto [r0, r1, r2, r3] = Philox::generate(element_offset, key);
//   float u = Philox::to_uniform(r0);        // [0, 1)
//   auto [n0, n1] = Philox::box_muller(r0, r1);  // N(0,1)
//
// Master counter increments per iteration (from Cipher).
// op_key() mixes op identity into the key space.
// element_offset is the flat index into the output tensor.
// Result: deterministic per-element, per-op, per-iteration randomness
// that reproduces identically across CPU/CUDA/ROCm/XLA.

#include <crucible/Platform.h>
#include <crucible/Types.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace crucible {

// ═══════════════════════════════════════════════════════════════════
// Philox4x32-10 core
// ═══════════════════════════════════════════════════════════════════

struct Philox {
    // Weyl sequence constants (golden ratio–derived).
    static constexpr uint32_t W0 = 0x9E3779B9;
    static constexpr uint32_t W1 = 0xBB67AE85;

    // Philox S-box multiplier constants.
    static constexpr uint32_t M0 = 0xD2511F53;
    static constexpr uint32_t M1 = 0xCD9E8D57;

    using Ctr = std::array<uint32_t, 4>;
    using Key = std::array<uint32_t, 2>;

    // ── Core bijection: 10 rounds ──────────────────────────────────

    [[nodiscard]] static constexpr Ctr generate(Ctr ctr, Key key) {
        for (int round = 0; round < 10; round++) {
            // Single round: two parallel multiply-xor-swap operations.
            uint32_t hi0 = mulhi_(M0, ctr[0]);
            uint32_t lo0 = ctr[0] * M0;
            uint32_t hi1 = mulhi_(M1, ctr[2]);
            uint32_t lo1 = ctr[2] * M1;

            ctr = {
                hi1 ^ ctr[1] ^ key[0],
                lo1,
                hi0 ^ ctr[3] ^ key[1],
                lo0,
            };

            // Key schedule: Weyl sequence bump.
            key[0] += W0;
            key[1] += W1;
        }
        return ctr;
    }

    // ── Convenience: 64-bit offset + 64-bit key ────────────────────
    //
    // Splits the 64-bit values into the 4×32 counter and 2×32 key.
    // Produces 4 independent uint32 random values per call.

    [[nodiscard]] static constexpr Ctr generate(uint64_t offset, uint64_t key) {
        Ctr ctr = {
            static_cast<uint32_t>(offset),
            static_cast<uint32_t>(offset >> 32),
            0, 0,
        };
        Key k = {
            static_cast<uint32_t>(key),
            static_cast<uint32_t>(key >> 32),
        };
        return generate(ctr, k);
    }

    // ── Float conversions ──────────────────────────────────────────

    // Uniform float in [0, 1). Maps full uint32 range to [0, 1).
    // 2^-32 = 2.3283064365386963e-10
    [[nodiscard]] static constexpr float to_uniform(uint32_t x) {
        return static_cast<float>(x) * 2.3283064365386963e-10f;
    }

    // Uniform double in [0, 1). Higher precision.
    [[nodiscard]] static constexpr double to_uniform_d(uint32_t x) {
        return static_cast<double>(x) * 2.3283064365386963e-10;
    }

    // Box-Muller transform: two uniform → two normal N(0,1).
    // Consumes 2 uint32 values, produces 2 floats.
    // Uses the polar form for better numerical stability.
    [[nodiscard]] static std::pair<float, float>
    box_muller(uint32_t u1_raw, uint32_t u2_raw) {
        // Map to (0, 1] to avoid log(0). Use (x+1) * 2^-32.
        float u1 = (static_cast<float>(u1_raw) + 1.0f) * 2.3283064365386963e-10f;
        float u2 = (static_cast<float>(u2_raw) + 1.0f) * 2.3283064365386963e-10f;

        float r = std::sqrt(-2.0f * std::log(u1));
        float theta = 6.2831853071795864f * u2;  // 2π

        return {r * std::cos(theta), r * std::sin(theta)};
    }

    // ── Per-op key derivation ──────────────────────────────────────
    //
    // Mixes master counter + op identity into a 64-bit key.
    // Same (master, op_index, content_hash) → same key → same sequence.
    // Different ops or iterations → statistically independent.

    [[nodiscard]] static constexpr uint64_t
    op_key(uint64_t master_counter, uint32_t op_index, ContentHash content_hash) {
        // FNV-1a–style mixing. Not cryptographic, but sufficient
        // for decorrelating Philox streams across ops.
        uint64_t h = 0xcbf29ce484222325ULL;
        h = fnv_mix_(h, master_counter);
        h = fnv_mix_(h, static_cast<uint64_t>(op_index));
        h = fnv_mix_(h, content_hash.raw());
        return h;
    }

 private:
    // High 32 bits of a 32×32→64 multiply.
    [[nodiscard]] static constexpr uint32_t mulhi_(uint32_t a, uint32_t b) {
        return static_cast<uint32_t>(
            (static_cast<uint64_t>(a) * static_cast<uint64_t>(b)) >> 32);
    }

    // FNV-1a mix of a 64-bit value into hash state.
    [[nodiscard]] static constexpr uint64_t fnv_mix_(uint64_t h, uint64_t v) {
        for (int i = 0; i < 8; i++) {
            h ^= static_cast<uint64_t>((v >> (i * 8)) & 0xFF);
            h *= 0x100000001b3ULL;
        }
        return h;
    }
};

} // namespace crucible
