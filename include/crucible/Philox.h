#pragma once

// Two surfaces, bit-equal by construction: the raw primitives return plain
// values, and each `_det` wrapper returns the same bits carrying a
// determinism tier in its type. Production code takes the `_det` form so the
// tier survives into whatever consumes the bytes. Test and bench code takes
// the raw form because bit-level inspection is the point there.

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/fixy/fp/Polynomial.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace crucible {

struct Philox {
    // These four constants and the ten-round count are fixed by the
    // Philox4x32-10 specification. Changing any of them changes every stream
    // the runtime has ever produced, so replay of older state breaks.
    //
    // The first pair are the Weyl increments applied to the key between
    // rounds. The second pair are the round multipliers.
    static constexpr uint32_t W0 = 0x9E3779B9;
    static constexpr uint32_t W1 = 0xBB67AE85;

    static constexpr uint32_t M0 = 0xD2511F53;
    static constexpr uint32_t M1 = 0xCD9E8D57;

    using Ctr = std::array<uint32_t, 4>;
    using Key = std::array<uint32_t, 2>;

    [[nodiscard]] static constexpr Ctr generate(Ctr ctr, Key key) {
        for (int round = 0; round < 10; round++) {
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

            key[0] += W0;
            key[1] += W1;
        }
        return ctr;
    }

    [[nodiscard]] static constexpr Ctr generate(uint64_t offset, uint64_t key) {
        Ctr ctr = {
            static_cast<uint32_t>(offset),
            static_cast<uint32_t>(offset >> 32),
            0,
            0,
        };
        Key k = {
            static_cast<uint32_t>(key),
            static_cast<uint32_t>(key >> 32),
        };
        return generate(ctr, k);
    }

    // The multiplier is 2^-32, which carries the whole uint32 range onto
    // [0, 1). One IEEE 754 multiplication is bit-stable everywhere, so both
    // conversions keep the tier of the value they are given.
    [[nodiscard]] static constexpr float to_uniform(uint32_t x) {
        return static_cast<float>(x) * 2.3283064365386963e-10f;
    }

    [[nodiscard]] static constexpr double to_uniform_d(uint32_t x) {
        return static_cast<double>(x) * 2.3283064365386963e-10;
    }

    [[nodiscard]] static std::pair<float, float> box_muller(uint32_t u1_raw, uint32_t u2_raw) {
        // The bias to (0, 1] is what keeps a zero input out of log().
        float u1 = (static_cast<float>(u1_raw) + 1.0f) * 2.3283064365386963e-10f;
        float u2 = (static_cast<float>(u2_raw) + 1.0f) * 2.3283064365386963e-10f;

        float r = std::sqrt(-2.0f * std::log(u1));
        float theta = 6.2831853071795864f * u2;  // 2π

        return {r * std::cos(theta), r * std::sin(theta)};
    }

    using DetSafePhiloxCtr = crucible::fixy::wrap::DetSafe<crucible::fixy::wrap::DetSafeTier_v::PhiloxRng, Ctr>;
    using DetSafePhiloxFloat = crucible::fixy::wrap::DetSafe<crucible::fixy::wrap::DetSafeTier_v::PhiloxRng, float>;
    using DetSafePhiloxDouble = crucible::fixy::wrap::DetSafe<crucible::fixy::wrap::DetSafeTier_v::PhiloxRng, double>;
    using DetSafeMonoClockFloatPair =
        crucible::fixy::wrap::DetSafe<crucible::fixy::wrap::DetSafeTier_v::MonotonicClockRead, std::pair<float, float>>;
    using DetSafePhiloxFloatPair =
        crucible::fixy::wrap::DetSafe<crucible::fixy::wrap::DetSafeTier_v::PhiloxRng, std::pair<float, float>>;
    using DetSafePureKey = crucible::fixy::wrap::DetSafe<crucible::fixy::wrap::DetSafeTier_v::Pure, uint64_t>;

    [[nodiscard]] static constexpr DetSafePhiloxCtr generate_det(Ctr ctr, Key key) {
        return DetSafePhiloxCtr{generate(ctr, key)};
    }

    [[nodiscard]] static constexpr DetSafePhiloxCtr generate_det(uint64_t offset, uint64_t key) {
        return DetSafePhiloxCtr{generate(offset, key)};
    }

    // Taking the key as a tier-carrying type lets a caller chain a key
    // straight in. The alternative, peeking the key out first, discards the
    // very promise this overload checks.
    template <crucible::fixy::wrap::DetSafeTier_v KeyTier>
        requires(crucible::fixy::wrap::DetSafeLattice::leq(crucible::fixy::wrap::DetSafeTier_v::PhiloxRng, KeyTier))
    [[nodiscard]] static constexpr DetSafePhiloxCtr generate_det(uint64_t offset,
                                                                 crucible::fixy::wrap::DetSafe<KeyTier, uint64_t> key) {
        return DetSafePhiloxCtr{generate(offset, std::move(key).consume())};
    }

    [[nodiscard]] static constexpr DetSafePhiloxFloat to_uniform_det(uint32_t x) {
        return DetSafePhiloxFloat{to_uniform(x)};
    }

    [[nodiscard]] static constexpr DetSafePhiloxDouble to_uniform_d_det(uint32_t x) {
        return DetSafePhiloxDouble{to_uniform_d(x)};
    }

    // The bytes are Philox-derived, but the transform reaches sin, cos and
    // log in the platform math library, and those agree only to within a few
    // units in the last place across implementations. The tier says so: this
    // result replays on the machine that produced it and nowhere else.
    [[nodiscard]] static DetSafeMonoClockFloatPair box_muller_det(uint32_t u1_raw, uint32_t u2_raw) {
        return DetSafeMonoClockFloatPair{box_muller(u1_raw, u2_raw)};
    }

    // Same transform with in-tree polynomial sin, cos and log and a correctly
    // rounded square root. No platform math library, so the result is
    // bit-identical everywhere and carries the stronger tier.
    [[nodiscard]] static DetSafePhiloxFloatPair box_muller_polynomial_det(uint32_t u1_raw, uint32_t u2_raw) {
        return DetSafePhiloxFloatPair{crucible::fixy::fp::box_muller_polynomial(u1_raw, u2_raw)};
    }

    [[nodiscard]] static constexpr DetSafePureKey op_key_det(uint64_t master_counter, uint32_t op_index,
                                                             ContentHash content_hash) {
        return DetSafePureKey{op_key_bytes_(master_counter, op_index, content_hash)};
    }

private:
    [[nodiscard]] static constexpr uint32_t mulhi_(uint32_t a, uint32_t b) {
        return static_cast<uint32_t>((static_cast<uint64_t>(a) * static_cast<uint64_t>(b)) >> 32);
    }

    [[nodiscard]] static constexpr uint64_t fnv_mix_(uint64_t h, uint64_t v) {
        for (int i = 0; i < 8; i++) {
            h ^= (v >> (i * 8)) & 0xFFULL;
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    // Private so that a caller cannot obtain these bytes without the tier
    // that op_key_det attaches to them.
    [[nodiscard]] static constexpr uint64_t op_key_bytes_(uint64_t master_counter, uint32_t op_index,
                                                          ContentHash content_hash) {
        // The mix is not cryptographic. It only has to decorrelate the
        // streams of two different operations.
        uint64_t h = 0xcbf29ce484222325ULL;
        h = fnv_mix_(h, master_counter);
        h = fnv_mix_(h, static_cast<uint64_t>(op_index));
        h = fnv_mix_(h, content_hash.raw());
        return h;
    }
};

}  // namespace crucible
