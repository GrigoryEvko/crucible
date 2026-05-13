#pragma once

// ═══════════════════════════════════════════════════════════════════
// random_input.h — Crucible-typed input generators for property tests.
//
// Each function takes an Rng& and produces a fully-formed Crucible
// type with bounded random fields.  Generators are tuned to:
//
//   - Stay within structural bounds (ndim ≤ 8, num_inputs ≤ 16, etc.)
//   - Hit edge values frequently (zero, max, sentinel)
//   - Avoid pathological combos that other layers would reject
//     anyway (the goal is to stress the LOGIC, not the contracts)
//
// All generators are pure functions of the Rng state.  Same Rng
// state → same output.  No external dependencies.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Types.h>

#include <cstdint>

namespace crucible::fuzz::prop {

// ─── ScalarType ────────────────────────────────────────────────────
//
// Enumerate the FP-leaning subset (most ML-relevant); skip Undefined
// to avoid the byte-0 sentinel collision class.  Edge sampling: bias
// toward Float / Half / BFloat16 since those dominate real workloads.
[[nodiscard]] inline ScalarType random_scalar_type(Rng& rng) noexcept {
    static constexpr ScalarType kPool[] = {
        ScalarType::Float, ScalarType::Half, ScalarType::BFloat16,
        ScalarType::Float8_e4m3fn, ScalarType::Float8_e5m2,
        ScalarType::Double, ScalarType::Int, ScalarType::Long,
        ScalarType::Bool, ScalarType::Byte,
    };
    return kPool[rng.next_below(sizeof(kPool) / sizeof(kPool[0]))];
}

// ─── NumericalRecipe ───────────────────────────────────────────────
//
// Eight semantic enum/byte fields.  Generates the full cross-product
// space: ~5 × 5 × 4 × 4 × 6 × 4 × 4 × 256 ≈ 12M distinct recipes.
// Most randomly-generated combinations are NONSENSICAL semantically
// (e.g., scale_policy=PER_BLOCK_MX with non-FP8 dtypes) but the hash
// function and pool intern logic must handle them all without crash
// or collision.  This is the point of stress testing.
[[nodiscard]] inline NumericalRecipe random_recipe(Rng& rng) noexcept {
    NumericalRecipe r{};
    r.accum_dtype  = random_scalar_type(rng);
    r.out_dtype    = random_scalar_type(rng);
    r.reduction_algo = static_cast<ReductionAlgo>(rng.next_below(4));
    r.rounding     = static_cast<RoundingMode>(rng.next_below(4));
    r.scale_policy = static_cast<ScalePolicy>(rng.next_below(6));
    r.softmax      = static_cast<SoftmaxRecurrence>(rng.next_below(4));
    r.determinism  = static_cast<ReductionDeterminism>(rng.next_below(4));
    r.flags        = static_cast<uint8_t>(rng.next32() & 0xFF);
    // hash field intentionally left default-zero — caller invokes
    // hashed() or compute_recipe_hash to populate.
    return r;
}

// ─── FeedbackEdge ──────────────────────────────────────────────────
//
// Two u16 fields.  Random bias toward small values (real loops have
// few feedback paths, single-digit indices).
[[nodiscard]] inline FeedbackEdge random_feedback_edge(Rng& rng) noexcept {
    FeedbackEdge e{};
    e.output_idx = static_cast<uint16_t>(rng.next_below(64));
    e.input_idx  = static_cast<uint16_t>(rng.next_below(64));
    return e;
}

// ─── LoopTermKind ──────────────────────────────────────────────────
[[nodiscard]] inline LoopTermKind random_loop_term_kind(Rng& rng) noexcept {
    return static_cast<LoopTermKind>(rng.next_below(2));
}

// ─── TensorMeta ────────────────────────────────────────────────────
//
// A 144-byte tensor descriptor.  ndim ≤ 8, sizes/strides random
// non-negative.  data_ptr left null (we're hashing metadata, not
// dereferencing).
[[nodiscard]] inline TensorMeta random_tensor_meta(Rng& rng) noexcept {
    TensorMeta m{};
    m.ndim = static_cast<uint8_t>(rng.next_below(9));  // [0, 8]
    for (uint8_t d = 0; d < m.ndim; ++d) {
        // Bias toward small dimension sizes (1..1024) to match
        // real ML tensor shapes.
        m.sizes[d]   = static_cast<int64_t>(rng.next_below(1024) + 1);
        m.strides[d] = static_cast<int64_t>(rng.next64() & 0x7FFFFFFFFFFFFFFFLL);
    }
    m.dtype       = random_scalar_type(rng);
    // device_type values map to c10 ordinals; restrict to common set.
    static constexpr DeviceType kDevices[] = {
        DeviceType::CPU, DeviceType::CUDA, DeviceType::MPS, DeviceType::Meta,
    };
    m.device_type = kDevices[rng.next_below(4)];
    m.device_idx  = static_cast<int8_t>(rng.next_below(8));
    return m;
}

}  // namespace crucible::fuzz::prop
