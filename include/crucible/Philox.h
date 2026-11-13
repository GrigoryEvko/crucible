#pragma once

// The determinism tiers of the Philox streams.  fixy::Philox4x32::generate of
// the Scalar family gives the block of the generator.  Each `_det` wrapper here
// returns the same bits with a determinism tier in its type.  Production code
// takes the `_det` form so the tier survives into whatever consumes the bytes.
// Test and bench code takes the plain block, because bit-level inspection is
// the point there.

#include <crucible/Types.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/fp/Polynomial.h>
#include <foundation/Platform.h>

#include <cstdint>
#include <utility>

namespace crucible {

struct Philox {
    // Two independent samples of the standard normal distribution.
    using NormalPair = ::fixy::Record<float, float>;

    // The multiplier is 2^-32, which carries the whole uint32 range onto
    // [0, 1). One IEEE 754 multiplication is bit-stable everywhere, so both
    // conversions keep the tier of the value they are given.
    [[nodiscard]] static constexpr float to_uniform(uint32_t x) { return static_cast<float>(x) * kTwoToMinus32f_; }

    [[nodiscard]] static constexpr double to_uniform_d(uint32_t x) { return static_cast<double>(x) * kTwoToMinus32_; }

    using DetSafePhiloxCounter = ::fixy::det_safe::PhiloxRng<::fixy::Philox4x32::Counter>;
    using DetSafePhiloxFloat = ::fixy::det_safe::PhiloxRng<float>;
    using DetSafePhiloxDouble = ::fixy::det_safe::PhiloxRng<double>;
    using DetSafePhiloxFloatPair = ::fixy::det_safe::PhiloxRng<NormalPair>;
    using DetSafePureKey = ::fixy::det_safe::Pure<uint64_t>;

    [[nodiscard]] static constexpr DetSafePhiloxCounter generate_det(::fixy::Philox4x32::Counter counter,
                                                                     ::fixy::Philox4x32::Key key) {
        return ::fixy::mint_band<DetSafePhiloxCounter>(::fixy::Philox4x32::generate(counter, key));
    }

    [[nodiscard]] static constexpr DetSafePhiloxCounter generate_det(uint64_t offset, uint64_t key) {
        return ::fixy::mint_band<DetSafePhiloxCounter>(::fixy::Philox4x32::generate(offset, key));
    }

    // Taking the key as a tier-carrying type lets a caller chain a key
    // straight in. The alternative, peeking the key out first, discards the
    // very promise this overload checks.
    template <::fixy::DetSafeTier_v KeyTier>
        requires(::fixy::DetSafeLattice::leq(::fixy::DetSafeTier_v::PhiloxRng, KeyTier))
    [[nodiscard]] static constexpr DetSafePhiloxCounter generate_det(uint64_t offset,
                                                                     ::fixy::DetSafe<KeyTier, uint64_t> key) {
        return ::fixy::mint_band<DetSafePhiloxCounter>(::fixy::Philox4x32::generate(offset, std::move(key).consume()));
    }

    [[nodiscard]] static constexpr DetSafePhiloxFloat to_uniform_det(uint32_t x) {
        return ::fixy::mint_band<DetSafePhiloxFloat>(to_uniform(x));
    }

    [[nodiscard]] static constexpr DetSafePhiloxDouble to_uniform_d_det(uint32_t x) {
        return ::fixy::mint_band<DetSafePhiloxDouble>(to_uniform_d(x));
    }

    // The Box-Muller transform with in-tree polynomial sin, cos and log and a
    // correctly rounded square root. It calls no platform math library, so
    // the result is the same on each platform, bit for bit.
    [[nodiscard]] static DetSafePhiloxFloatPair box_muller_polynomial_det(uint32_t u1_raw, uint32_t u2_raw) {
        return ::fixy::mint_band<DetSafePhiloxFloatPair>(::fixy::fp::box_muller_polynomial(u1_raw, u2_raw));
    }

    [[nodiscard]] static constexpr DetSafePureKey op_key_det(uint64_t master_counter, uint32_t op_index,
                                                             ContentHash content_hash) {
        return ::fixy::mint_band<DetSafePureKey>(op_key_bytes_(master_counter, op_index, content_hash));
    }

private:
    // 2^-32, exact in both formats.
    static constexpr float kTwoToMinus32f_ = 0x1p-32f;
    static constexpr double kTwoToMinus32_ = 0x1p-32;

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
