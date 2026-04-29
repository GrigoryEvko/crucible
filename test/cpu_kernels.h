#pragma once

// Naive CPU reference kernels for testing Crucible's dispatch pipeline
// with actual computation. NOT for production — O(n^3) matmul, etc.
//
// All functions take raw float* pointers and integer dimensions.
// Row-major layout throughout (C contiguous).

#include <algorithm>
#include "test_assert.h"
#include <cmath>
#include <cstring>

namespace crucible::cpu {

// ─── Matrix multiply: C[M,N] = A[M,K] @ B[K,N] ─────────────────────
inline void mm(const float* A, const float* B, float* C,
               int M, int N, int K) {
    assert(A && B && C);
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++)
                sum += A[i * K + k] * B[k * N + j];
            C[i * N + j] = sum;
        }
    }
}

// ─── Elementwise add: C[n] = A[n] + B[n] ────────────────────────────
//
// Supports broadcasting: if B has fewer elements (bias_n), it's tiled.
// For full elementwise: bias_n == n. For bias add: bias_n == last_dim.
inline void add(const float* A, const float* B, float* C,
                int n, int bias_n = 0) {
    assert(A && B && C);
    if (bias_n <= 0) bias_n = n;
    for (int i = 0; i < n; i++)
        C[i] = A[i] + B[i % bias_n];
}

// ─── Elementwise sub: C[n] = A[n] - B[n] ────────────────────────────
inline void sub(const float* A, const float* B, float* C, int n) {
    assert(A && B && C);
    for (int i = 0; i < n; i++)
        C[i] = A[i] - B[i];
}

// ─── Elementwise mul: C[n] = A[n] * B[n] ────────────────────────────
inline void mul(const float* A, const float* B, float* C, int n) {
    assert(A && B && C);
    for (int i = 0; i < n; i++)
        C[i] = A[i] * B[i];
}

// ─── ReLU: C[n] = max(A[n], 0) ──────────────────────────────────────
inline void relu(const float* A, float* C, int n) {
    assert(A && C);
    for (int i = 0; i < n; i++)
        C[i] = A[i] > 0.0f ? A[i] : 0.0f;
}

// ─── ReLU backward: C[n] = grad[n] * (input[n] > 0 ? 1 : 0) ────────
inline void relu_bwd(const float* grad, const float* input,
                     float* C, int n) {
    assert(grad && input && C);
    for (int i = 0; i < n; i++)
        C[i] = input[i] > 0.0f ? grad[i] : 0.0f;
}

// ─── GELU: C[n] = A[n] * 0.5 * (1 + erf(A[n] / sqrt(2))) ──────────
inline void gelu(const float* A, float* C, int n) {
    assert(A && C);
    constexpr float k = 0.7071067811865476f; // 1/sqrt(2)
    for (int i = 0; i < n; i++)
        C[i] = A[i] * 0.5f * (1.0f + std::erf(A[i] * k));
}

// ─── Sigmoid: C[n] = 1 / (1 + exp(-A[n])) ──────────────────────────
inline void sigmoid(const float* A, float* C, int n) {
    assert(A && C);
    for (int i = 0; i < n; i++)
        C[i] = 1.0f / (1.0f + std::exp(-A[i]));
}

// ─── SiLU (Swish): C[n] = A[n] * sigmoid(A[n]) ─────────────────────
inline void silu(const float* A, float* C, int n) {
    assert(A && C);
    for (int i = 0; i < n; i++) {
        float s = 1.0f / (1.0f + std::exp(-A[i]));
        C[i] = A[i] * s;
    }
}

// ─── Fill: C[n] = value ─────────────────────────────────────────────
inline void fill(float* C, int n, float value) {
    assert(C);
    for (int i = 0; i < n; i++)
        C[i] = value;
}

// ─── Softmax: Y[B,D] = softmax(X[B,D]) over last dim ───────────────
//
// Numerically stable: subtract max before exp.
inline void softmax(const float* X, float* Y, int B, int D) {
    assert(X && Y);
    for (int b = 0; b < B; b++) {
        const float* x = X + b * D;
        float* y = Y + b * D;
        float mx = x[0];
        for (int d = 1; d < D; d++) mx = std::max(mx, x[d]);
        float sum = 0.0f;
        for (int d = 0; d < D; d++) {
            y[d] = std::exp(x[d] - mx);
            sum += y[d];
        }
        float inv = 1.0f / sum;
        for (int d = 0; d < D; d++) y[d] *= inv;
    }
}

// ─── Layer norm: Y[B,D] = (X - mean) / sqrt(var + eps) * γ + β ─────
inline void layer_norm(const float* X, const float* gamma,
                       const float* beta, float* Y,
                       int B, int D, float eps = 1e-5f) {
    assert(X && gamma && beta && Y);
    for (int b = 0; b < B; b++) {
        const float* x = X + b * D;
        float* y = Y + b * D;
        float mean = 0.0f;
        for (int d = 0; d < D; d++) mean += x[d];
        mean /= static_cast<float>(D);
        float var = 0.0f;
        for (int d = 0; d < D; d++) {
            float diff = x[d] - mean;
            var += diff * diff;
        }
        var /= static_cast<float>(D);
        float inv = 1.0f / std::sqrt(var + eps);
        for (int d = 0; d < D; d++)
            y[d] = (x[d] - mean) * inv * gamma[d] + beta[d];
    }
}

// ─── Batch norm: Y[N,C,HW] per-channel normalization ───────────────
//
// Computes per-channel mean and variance, normalizes.
// gamma[C], beta[C] are affine parameters.
inline void batch_norm(const float* X, const float* gamma,
                       const float* beta, float* Y,
                       int N, int C, int HW, float eps = 1e-5f) {
    assert(X && gamma && beta && Y);
    for (int c = 0; c < C; c++) {
        float mean = 0.0f;
        int count = N * HW;
        for (int n = 0; n < N; n++)
            for (int hw = 0; hw < HW; hw++)
                mean += X[(n * C + c) * HW + hw];
        mean /= static_cast<float>(count);
        float var = 0.0f;
        for (int n = 0; n < N; n++)
            for (int hw = 0; hw < HW; hw++) {
                float d = X[(n * C + c) * HW + hw] - mean;
                var += d * d;
            }
        var /= static_cast<float>(count);
        float inv = 1.0f / std::sqrt(var + eps);
        for (int n = 0; n < N; n++)
            for (int hw = 0; hw < HW; hw++) {
                int idx = (n * C + c) * HW + hw;
                Y[idx] = (X[idx] - mean) * inv * gamma[c] + beta[c];
            }
    }
}

// ─── Conv2D: Y[N,Co,Ho,Wo] = X[N,Ci,Hi,Wi] * W[Co,Ci,Kh,Kw] ──────
//
// No padding, stride=1. Ho = Hi - Kh + 1, Wo = Wi - Kw + 1.
inline void conv2d(const float* X, const float* W, float* Y,
                   int N, int Ci, int Hi, int Wi,
                   int Co, int Kh, int Kw) {
    assert(X && W && Y);
    int Ho = Hi - Kh + 1;
    int Wo = Wi - Kw + 1;
    std::memset(Y, 0, static_cast<size_t>(N * Co * Ho * Wo) * sizeof(float));
    for (int n = 0; n < N; n++)
        for (int co = 0; co < Co; co++)
            for (int ci = 0; ci < Ci; ci++)
                for (int ho = 0; ho < Ho; ho++)
                    for (int wo = 0; wo < Wo; wo++)
                        for (int kh = 0; kh < Kh; kh++)
                            for (int kw = 0; kw < Kw; kw++)
                                Y[((n * Co + co) * Ho + ho) * Wo + wo] +=
                                    X[((n * Ci + ci) * Hi + (ho + kh)) * Wi + (wo + kw)] *
                                    W[((co * Ci + ci) * Kh + kh) * Kw + kw];
}

// ─── Max pool 2D: Y[N,C,H/K,W/K] ───────────────────────────────────
inline void maxpool2d(const float* X, float* Y,
                      int N, int C, int H, int W, int K = 2) {
    assert(X && Y);
    int Ho = H / K, Wo = W / K;
    for (int n = 0; n < N; n++)
        for (int c = 0; c < C; c++)
            for (int ho = 0; ho < Ho; ho++)
                for (int wo = 0; wo < Wo; wo++) {
                    float mx = -INFINITY;
                    for (int kh = 0; kh < K; kh++)
                        for (int kw = 0; kw < K; kw++)
                            mx = std::max(mx,
                                X[((n * C + c) * H + ho * K + kh) * W + wo * K + kw]);
                    Y[((n * C + c) * Ho + ho) * Wo + wo] = mx;
                }
}

// ─── Global average pool: Y[N,C] = mean(X[N,C,HW]) ─────────────────
inline void avgpool_global(const float* X, float* Y,
                           int N, int C, int HW) {
    assert(X && Y);
    float inv = 1.0f / static_cast<float>(HW);
    for (int n = 0; n < N; n++)
        for (int c = 0; c < C; c++) {
            float sum = 0.0f;
            for (int hw = 0; hw < HW; hw++)
                sum += X[(n * C + c) * HW + hw];
            Y[n * C + c] = sum * inv;
        }
}

// ─── Scaled dot-product attention ────────────────────────────────────
//
// Q[B,S,D], K[B,S,D], V[B,S,D] → Y[B,S,D]
// Y = softmax(Q @ K^T / sqrt(D)) @ V
//
// Uses a stack-allocated temp buffer for small S (≤128). Falls back
// to heap for larger sequences (test-only, never on hot path).
inline void sdpa(const float* Q, const float* K, const float* V,
                 float* Y, int B, int S, int D) {
    assert(Q && K && V && Y);
    float scale = 1.0f / std::sqrt(static_cast<float>(D));

    // Temp attention weights [S,S] — stack-allocate for small S.
    float stack_attn[128 * 128];
    float* attn = (S <= 128) ? stack_attn
        : new float[static_cast<size_t>(S) * static_cast<size_t>(S)];

    for (int b = 0; b < B; b++) {
        const float* q = Q + b * S * D;
        const float* k = K + b * S * D;
        const float* v = V + b * S * D;
        float* y = Y + b * S * D;

        // QK^T / sqrt(D)
        for (int i = 0; i < S; i++)
            for (int j = 0; j < S; j++) {
                float dot = 0.0f;
                for (int d = 0; d < D; d++)
                    dot += q[i * D + d] * k[j * D + d];
                attn[i * S + j] = dot * scale;
            }

        // Softmax per query row
        for (int i = 0; i < S; i++) {
            float* row = attn + i * S;
            float mx = row[0];
            for (int j = 1; j < S; j++) mx = std::max(mx, row[j]);
            float sum = 0.0f;
            for (int j = 0; j < S; j++) {
                row[j] = std::exp(row[j] - mx);
                sum += row[j];
            }
            float inv = 1.0f / sum;
            for (int j = 0; j < S; j++) row[j] *= inv;
        }

        // Attention @ V
        for (int i = 0; i < S; i++)
            for (int d = 0; d < D; d++) {
                float sum = 0.0f;
                for (int j = 0; j < S; j++)
                    sum += attn[i * S + j] * v[j * D + d];
                y[i * D + d] = sum;
            }
    }

    if (S > 128) delete[] attn;
}

// ─── Cross-entropy loss: scalar = mean(-log(softmax(logits)[target])) ─
//
// logits[B,C], targets[B] (int32_t), probs[B,C] (output: softmax)
// Returns the loss scalar. probs is a work buffer.
inline float cross_entropy(const float* logits, const int* targets,
                           float* probs, int B, int C) {
    assert(logits && targets && probs);
    softmax(logits, probs, B, C);
    float loss = 0.0f;
    for (int b = 0; b < B; b++) {
        assert(targets[b] >= 0 && targets[b] < C);
        loss -= std::log(std::max(probs[b * C + targets[b]], 1e-7f));
    }
    return loss / static_cast<float>(B);
}

// ─── Index select (for CLS token extraction): Y[B,D] = X[B,idx,D] ──
inline void index_select(const float* X, float* Y,
                         int B, int S, int D, int idx) {
    assert(X && Y && idx >= 0 && idx < S);
    for (int b = 0; b < B; b++)
        std::memcpy(Y + b * D, X + (b * S + idx) * D,
                    static_cast<size_t>(D) * sizeof(float));
}

// ─── Transpose last two dims: Y[...,N,M] = X[...,M,N] ──────────────
inline void transpose_2d(const float* X, float* Y,
                         int batch, int M, int N) {
    assert(X && Y);
    for (int b = 0; b < batch; b++) {
        const float* x = X + b * M * N;
        float* y = Y + b * N * M;
        for (int i = 0; i < M; i++)
            for (int j = 0; j < N; j++)
                y[j * M + i] = x[i * N + j];
    }
}

} // namespace crucible::cpu
