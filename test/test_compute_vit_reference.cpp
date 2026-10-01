// The fixed-seed weights and the direct CPU reference pass of
// test_compute_vit.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <random>

using namespace crucible;

namespace test_compute_vit {

void fill_weights(Weights& weights) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);

    for (auto& v : weights.X)
        v = dist(rng);
    for (auto& v : weights.W_q)
        v = dist(rng);
    for (auto& v : weights.W_k)
        v = dist(rng);
    for (auto& v : weights.W_v)
        v = dist(rng);
    for (auto& v : weights.W_out)
        v = dist(rng);
    for (auto& v : weights.W_ff1)
        v = dist(rng);
    for (auto& v : weights.W_ff2)
        v = dist(rng);
    for (auto& v : weights.W_head)
        v = dist(rng);

    for (int i = 0; i < D; i++) {
        weights.gamma1[i] = 1.0f + dist(rng) * 0.1f;
        weights.beta1[i] = dist(rng) * 0.1f;
        weights.gamma2[i] = 1.0f + dist(rng) * 0.1f;
        weights.beta2[i] = dist(rng) * 0.1f;
    }
}

void compute_reference(const Weights& weights, Reference& reference) {
    float ref_norm1[B * S * D]{};
    float ref_Q[B * S * D]{};
    float ref_K[B * S * D]{};
    float ref_V[B * S * D]{};
    float ref_attn[B * S * D]{};
    float ref_proj[B * S * D]{};
    float ref_res1[B * S * D]{};
    float ref_norm2[B * S * D]{};
    float ref_ff1[B * S * D_FF]{};
    float ref_relu[B * S * D_FF]{};
    float ref_ff2[B * S * D]{};
    float ref_cls[B * D]{};
    float ref_logits[B * N_CLS]{};

    cpu::layer_norm(weights.X, weights.gamma1, weights.beta1, ref_norm1, B * S, D);
    cpu::mm(ref_norm1, weights.W_q, ref_Q, B * S, D, D);
    cpu::mm(ref_norm1, weights.W_k, ref_K, B * S, D, D);
    cpu::mm(ref_norm1, weights.W_v, ref_V, B * S, D, D);
    cpu::sdpa(ref_Q, ref_K, ref_V, ref_attn, B, S, D);
    cpu::mm(ref_attn, weights.W_out, ref_proj, B * S, D, D);
    cpu::add(ref_proj, weights.X, ref_res1, B * S * D);
    cpu::layer_norm(ref_res1, weights.gamma2, weights.beta2, ref_norm2, B * S, D);
    cpu::mm(ref_norm2, weights.W_ff1, ref_ff1, B * S, D_FF, D);
    cpu::relu(ref_ff1, ref_relu, B * S * D_FF);
    cpu::mm(ref_relu, weights.W_ff2, ref_ff2, B * S, D, D_FF);
    cpu::add(ref_ff2, ref_res1, reference.res2, B * S * D);
    cpu::index_select(reference.res2, ref_cls, B, S, D, 0);
    cpu::mm(ref_cls, weights.W_head, ref_logits, B, N_CLS, D);
    cpu::softmax(ref_logits, reference.probs, B, N_CLS);
}

}  // namespace test_compute_vit
