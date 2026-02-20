// ViT (Vision Transformer) training — 1000 compiled iterations through Crucible.
//
// Mini-ViT architecture (1 transformer layer):
//   input[2,3,8,8] → conv(patch_embed) → reshape → add(pos_embed) →
//   layer_norm → mm(Q) → mm(K) → mm(V) → sdpa → mm(out_proj) →
//   add(residual) → layer_norm → mm(mlp_fc1) → gelu → mm(mlp_fc2) →
//   add(residual) → layer_norm(final) → index(CLS) → mm(head) →
//   cross_entropy
//   + 11 backward ops (including sdpa_bwd with 4 inputs)
//
// 30 ops per iteration. Demonstrates:
//   - Multi-head self-attention (Q/K/V projections sharing input)
//   - Residual connections (add with skip from earlier activation)
//   - Activations saved for backward (Q/K/V live until sdpa_bwd at op 28)
//   - GELU activation (MLP block)
//   - CLS token extraction (index_select)

#include <crucible/Vigil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace crucible;

// ═══════════════════════════════════════════════════════════════════
// Schema hashes
// ═══════════════════════════════════════════════════════════════════

static constexpr SchemaHash CONV{0xD001}, RESHAPE{0xD002}, ADD{0xD003},
    LN{0xD004}, MM{0xD005}, SDPA{0xD006}, GELU{0xD007},
    INDEX{0xD008}, XENT{0xD009}, LOSS_BWD{0xD00A}, MM_BWD{0xD00B},
    SCATTER{0xD00C}, LN_BWD{0xD00D}, GELU_BWD{0xD00E},
    SDPA_BWD{0xD00F};

// ═══════════════════════════════════════════════════════════════════
// Tensor reference encoding (same as test_imagenet.cpp)
// ═══════════════════════════════════════════════════════════════════

static constexpr uint8_t P = 0x80;

struct TSpec {
    uint8_t ref, ndim;
    int64_t d[4];
};

static constexpr TSpec pr(uint8_t id, uint8_t n,
    int64_t a=0, int64_t b=0, int64_t c=0, int64_t e=0) {
    return {uint8_t(P | id), n, {a, b, c, e}};
}
static constexpr TSpec ac(uint8_t id, uint8_t n,
    int64_t a=0, int64_t b=0, int64_t c=0, int64_t e=0) {
    return {id, n, {a, b, c, e}};
}

// ═══════════════════════════════════════════════════════════════════
// Op table
// ═══════════════════════════════════════════════════════════════════

struct OpDef {
    SchemaHash schema;
    ShapeHash shape;
    uint8_t n_in, n_out;
    TSpec t[6]; // max: sdpa_bwd has 4 inputs + 1 output = 5
};

// Mini-ViT shapes: batch=2, patches=4, hidden=16, mlp_dim=32, classes=10
static constexpr int64_t B = 2, SEQ = 4, D = 16, MLP = 32, CL = 10;

// Parameters (16):
//   P0=input[2,3,8,8]  P1=patch_w[16,3,4,4]  P2=pos_embed[1,4,16]
//   P3=ln1_w[16]  P4=ln1_b[16]  P5=wq[16,16]  P6=wk[16,16]  P7=wv[16,16]
//   P8=wo[16,16]  P9=ln2_w[16]  P10=ln2_b[16]  P11=mlp_w1[16,32]
//   P12=mlp_w2[32,16]  P13=ln_f_w[16]  P14=ln_f_b[16]  P15=head_w[10,16]
//
// Activations (30): A0-A18 (forward), A19-A29 (backward)
//   Key lifetimes: A4(Q), A5(K), A6(V) live from op 4-6 until op 28 (sdpa_bwd)

static const OpDef OPS[] = {
    // ── Forward (19 ops) ─────────────────────────────────────────
    //   Patch embedding
    /*  0 */ {CONV,    ShapeHash{0xD101}, 2,1, {pr(0,4,B,3,8,8), pr(1,4,D,3,4,4),
                                                ac(0,4,B,D,2,2)}},
    /*  1 */ {RESHAPE, ShapeHash{0xD102}, 1,1, {ac(0,4,B,D,2,2), ac(1,3,B,SEQ,D)}},
    /*  2 */ {ADD,     ShapeHash{0xD103}, 2,1, {ac(1,3,B,SEQ,D), pr(2,3,1,SEQ,D),
                                                ac(2,3,B,SEQ,D)}},

    //   Transformer layer: self-attention
    /*  3 */ {LN,      ShapeHash{0xD104}, 3,1, {ac(2,3,B,SEQ,D), pr(3,1,D), pr(4,1,D),
                                                ac(3,3,B,SEQ,D)}},
    /*  4 */ {MM,      ShapeHash{0xD105}, 2,1, {ac(3,3,B,SEQ,D), pr(5,2,D,D),
                                                ac(4,3,B,SEQ,D)}},   // Q
    /*  5 */ {MM,      ShapeHash{0xD106}, 2,1, {ac(3,3,B,SEQ,D), pr(6,2,D,D),
                                                ac(5,3,B,SEQ,D)}},   // K
    /*  6 */ {MM,      ShapeHash{0xD107}, 2,1, {ac(3,3,B,SEQ,D), pr(7,2,D,D),
                                                ac(6,3,B,SEQ,D)}},   // V
    /*  7 */ {SDPA,    ShapeHash{0xD108}, 3,1, {ac(4,3,B,SEQ,D), ac(5,3,B,SEQ,D),
                                                ac(6,3,B,SEQ,D), ac(7,3,B,SEQ,D)}},
    /*  8 */ {MM,      ShapeHash{0xD109}, 2,1, {ac(7,3,B,SEQ,D), pr(8,2,D,D),
                                                ac(8,3,B,SEQ,D)}},   // out proj
    /*  9 */ {ADD,     ShapeHash{0xD10A}, 2,1, {ac(8,3,B,SEQ,D), ac(2,3,B,SEQ,D),
                                                ac(9,3,B,SEQ,D)}},   // residual 1

    //   Transformer layer: MLP
    /* 10 */ {LN,      ShapeHash{0xD10B}, 3,1, {ac(9,3,B,SEQ,D), pr(9,1,D), pr(10,1,D),
                                                ac(10,3,B,SEQ,D)}},
    /* 11 */ {MM,      ShapeHash{0xD10C}, 2,1, {ac(10,3,B,SEQ,D), pr(11,2,D,MLP),
                                                ac(11,3,B,SEQ,MLP)}}, // expand
    /* 12 */ {GELU,    ShapeHash{0xD10D}, 1,1, {ac(11,3,B,SEQ,MLP), ac(12,3,B,SEQ,MLP)}},
    /* 13 */ {MM,      ShapeHash{0xD10E}, 2,1, {ac(12,3,B,SEQ,MLP), pr(12,2,MLP,D),
                                                ac(13,3,B,SEQ,D)}},   // contract
    /* 14 */ {ADD,     ShapeHash{0xD10F}, 2,1, {ac(13,3,B,SEQ,D), ac(9,3,B,SEQ,D),
                                                ac(14,3,B,SEQ,D)}},   // residual 2

    //   Classification head
    /* 15 */ {LN,      ShapeHash{0xD110}, 3,1, {ac(14,3,B,SEQ,D), pr(13,1,D), pr(14,1,D),
                                                ac(15,3,B,SEQ,D)}},
    /* 16 */ {INDEX,   ShapeHash{0xD111}, 1,1, {ac(15,3,B,SEQ,D), ac(16,2,B,D)}},
    /* 17 */ {MM,      ShapeHash{0xD112}, 2,1, {ac(16,2,B,D), pr(15,2,CL,D),
                                                ac(17,2,B,CL)}},
    /* 18 */ {XENT,    ShapeHash{0xD113}, 1,1, {ac(17,2,B,CL), ac(18,2,B,CL)}},

    // ── Backward (11 ops) ────────────────────────────────────────
    /* 19 */ {LOSS_BWD, ShapeHash{0xD114}, 1,1, {ac(18,2,B,CL), ac(19,2,B,CL)}},
    /* 20 */ {MM_BWD,   ShapeHash{0xD115}, 2,1, {ac(19,2,B,CL), pr(15,2,CL,D),
                                                  ac(20,2,B,D)}},
    /* 21 */ {SCATTER,  ShapeHash{0xD116}, 1,1, {ac(20,2,B,D), ac(21,3,B,SEQ,D)}},
    /* 22 */ {LN_BWD,   ShapeHash{0xD117}, 1,1, {ac(21,3,B,SEQ,D), ac(22,3,B,SEQ,D)}},
    /* 23 */ {MM_BWD,   ShapeHash{0xD118}, 2,1, {ac(22,3,B,SEQ,D), pr(12,2,MLP,D),
                                                  ac(23,3,B,SEQ,MLP)}},
    /* 24 */ {GELU_BWD, ShapeHash{0xD119}, 2,1, {ac(23,3,B,SEQ,MLP), ac(11,3,B,SEQ,MLP),
                                                  ac(24,3,B,SEQ,MLP)}},
    /* 25 */ {MM_BWD,   ShapeHash{0xD11A}, 2,1, {ac(24,3,B,SEQ,MLP), pr(11,2,D,MLP),
                                                  ac(25,3,B,SEQ,D)}},
    /* 26 */ {LN_BWD,   ShapeHash{0xD11B}, 1,1, {ac(25,3,B,SEQ,D), ac(26,3,B,SEQ,D)}},
    /* 27 */ {MM_BWD,   ShapeHash{0xD11C}, 2,1, {ac(26,3,B,SEQ,D), pr(8,2,D,D),
                                                  ac(27,3,B,SEQ,D)}},
    /* 28 */ {SDPA_BWD, ShapeHash{0xD11D}, 4,1, {ac(27,3,B,SEQ,D), ac(4,3,B,SEQ,D),
                                                  ac(5,3,B,SEQ,D), ac(6,3,B,SEQ,D),
                                                  ac(28,3,B,SEQ,D)}},
    /* 29 */ {LN_BWD,   ShapeHash{0xD11E}, 1,1, {ac(28,3,B,SEQ,D), ac(29,3,B,SEQ,D)}},
};

static constexpr uint32_t NUM_OPS = sizeof(OPS) / sizeof(OPS[0]);
static_assert(NUM_OPS == 30, "ViT: 19 forward + 11 backward");

// ═══════════════════════════════════════════════════════════════════
// Tensor construction
// ═══════════════════════════════════════════════════════════════════

static void* param_ptr(uint8_t idx) {
    return reinterpret_cast<void*>(uintptr_t((idx + 1) * 0x10000));
}

static void* act_ptr(uint32_t iter, uint8_t idx) {
    return reinterpret_cast<void*>(
        uintptr_t((iter + 1) * 0x1000000ULL + (idx + 1) * 0x10000));
}

static TensorMeta make_meta(const TSpec& s, uint32_t iter) {
    TensorMeta m{};
    m.data_ptr = (s.ref & P) ? param_ptr(s.ref & 0x7F) : act_ptr(iter, s.ref);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.ndim = s.ndim;
    for (uint8_t i = 0; i < s.ndim; i++) m.sizes[i] = s.d[i];
    if (s.ndim > 0) {
        m.strides[s.ndim - 1] = 1;
        for (int i = s.ndim - 2; i >= 0; i--)
            m.strides[i] = m.strides[i + 1] * m.sizes[i + 1];
    }
    return m;
}

struct OpPacket {
    TraceRing::Entry entry{};
    TensorMeta metas[6]{};
    uint16_t n_metas = 0;
};

static OpPacket build_op(uint32_t op_idx, uint32_t iter) {
    OpPacket pkt;
    const auto& op = OPS[op_idx];
    pkt.entry.schema_hash = op.schema;
    pkt.entry.shape_hash = op.shape;
    pkt.entry.num_inputs = op.n_in;
    pkt.entry.num_outputs = op.n_out;
    pkt.n_metas = op.n_in + op.n_out;
    for (uint8_t i = 0; i < pkt.n_metas; i++)
        pkt.metas[i] = make_meta(op.t[i], iter);
    return pkt;
}

// ═══════════════════════════════════════════════════════════════════
// Feed helpers
// ═══════════════════════════════════════════════════════════════════

static void feed_iteration(Vigil& v, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto p = build_op(i, iter);
        assert(v.record_op(p.entry, p.metas, p.n_metas));
    }
}

static void feed_trigger(Vigil& v, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto p = build_op(i, iter);
        assert(v.record_op(p.entry, p.metas, p.n_metas));
    }
}

static void wait_compiled(Vigil& v) {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!v.is_compiled()) {
        assert(std::chrono::steady_clock::now() < dl && "timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ═══════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════

int main() {
    std::printf("═══ ViT Training Simulation ═══\n\n");
    std::printf("Mini-ViT: patch_embed → 1 transformer layer → cls_head\n");
    std::printf("  batch=%lld  seq=%lld  hidden=%lld  mlp=%lld  classes=%lld\n",
                static_cast<long long>(B), static_cast<long long>(SEQ), static_cast<long long>(D),
                static_cast<long long>(MLP), static_cast<long long>(CL));
    std::printf("Ops/iteration: %u (19 fwd + 11 bwd)\n\n", NUM_OPS);

    Vigil vigil;

    // ── Phase 1: Recording ───────────────────────────────────────
    feed_iteration(vigil, 0);
    feed_iteration(vigil, 1);
    feed_trigger(vigil, 2);
    vigil.flush();
    wait_compiled(vigil);

    const auto* region = vigil.active_region();
    assert(region && region->plan);
    const auto* plan = region->plan;

    std::printf("── Region ──\n");
    std::printf("   %u ops | pool %llu bytes\n",
                region->num_ops, static_cast<unsigned long long>(plan->pool_bytes));
    std::printf("   %u slots: %u external + %u internal\n",
                plan->num_slots, plan->num_external,
                plan->num_slots - plan->num_external);

    // Print activation lifetimes for Q/K/V (saved for backward)
    std::printf("\n   Key lifetimes (saved for backward):\n");
    for (uint32_t s = 0; s < plan->num_slots; s++) {
        const auto& sl = plan->slots[s];
        // Show slots that live long (death_op - birth_op > 10 → saved for bwd)
        if (!sl.is_external && sl.death_op.raw() - sl.birth_op.raw() > 10)
            std::printf("     slot %u: birth=op%u death=op%u (%llu bytes)\n",
                        s, sl.birth_op.raw(), sl.death_op.raw(),
                        static_cast<unsigned long long>(sl.nbytes));
    }

    // ── Phase 2: Activate ────────────────────────────────────────
    auto ap = build_op(0, 2);
    auto ar = vigil.dispatch_op(ap.entry, ap.metas, ap.n_metas);
    assert(ar.action == DispatchResult::Action::RECORD);
    assert(vigil.context().is_compiled());
    std::printf("\n   CrucibleContext: COMPILED\n\n");

    // ── Phase 3: 1000 compiled iterations ────────────────────────
    std::printf("── 1000 compiled iterations ──\n");

    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t iter = 3; iter < 1003; iter++) {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto p = build_op(i, iter);
            auto r = vigil.dispatch_op(p.entry, p.metas, p.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    uint64_t total_ops = 1000ULL * NUM_OPS;
    double ns_op = double(us) * 1000.0 / double(total_ops);

    std::printf("   %llu dispatches in %lld μs (%.1f ns/op)\n",
                static_cast<unsigned long long>(total_ops), static_cast<long long>(us), ns_op);
    std::printf("   compiled_iterations=%u  diverged=%u\n",
                vigil.compiled_iterations(), vigil.diverged_count());

    // ── Phase 4: Data flow — verify attention output → out_proj ──
    std::printf("\n── Data flow: sdpa(op7) → out_proj(op8) ──\n");

    // Dispatch ops 0-7 to reach SDPA output
    for (uint32_t i = 0; i < 7; i++) {
        auto p = build_op(i, 9999);
        auto r = vigil.dispatch_op(p.entry, p.metas, p.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
    // Op 7: SDPA — write pattern to output
    auto p7 = build_op(7, 9999);
    auto r7 = vigil.dispatch_op(p7.entry, p7.metas, p7.n_metas);
    assert(r7.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(0), 0xCD, 64);

    // Op 8: out_proj — verify input is SDPA output
    auto p8 = build_op(8, 9999);
    auto r8 = vigil.dispatch_op(p8.entry, p8.metas, p8.n_metas);
    assert(r8.action == DispatchResult::Action::COMPILED);

    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
    bool ok = true;
    for (uint32_t i = 0; i < 64; i++)
        if (in_data[i] != 0xCD) { ok = false; break; }

    // Complete the iteration
    for (uint32_t i = 9; i < NUM_OPS; i++) {
        auto p = build_op(i, 9999);
        (void)vigil.dispatch_op(p.entry, p.metas, p.n_metas);
    }

    std::printf("   %s\n", ok ? "VERIFIED" : "FAILED");
    assert(ok);

    // ── Summary ──────────────────────────────────────────────────
    std::printf("\n═══ Summary ═══\n");
    std::printf("Pool: %llu bytes | Compiled: %u iters | %.1f ns/op\n",
                static_cast<unsigned long long>(plan->pool_bytes),
                vigil.compiled_iterations(), ns_op);
    std::printf("\ntest_vit: PASSED\n");
    return 0;
}
