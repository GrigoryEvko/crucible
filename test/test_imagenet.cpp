// ImageNet ResNet training — 1000 compiled iterations through Crucible dispatch.
//
// Mini-ResNet architecture (1 residual block):
//   input[2,3,16,16] → conv(stem) → bn → relu → maxpool →
//   conv → bn → relu → conv → bn → add(skip) → relu →
//   avgpool → mm(fc) → add(bias) → cross_entropy
//   + 10 backward ops
//
// 25 ops per iteration. After 2 recording iterations + trigger,
// the BackgroundThread detects the boundary and builds a RegionNode
// with MemoryPlan. Then 1000 compiled iterations run through
// dispatch_op() at ~2-5ns per op.

#include <crucible/Vigil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace crucible;

// ═══════════════════════════════════════════════════════════════════
// Schema hashes — simulated ATen dispatch keys
// ═══════════════════════════════════════════════════════════════════

static constexpr uint64_t CONV = 0xB001, BN = 0xB002, RELU = 0xB003,
    MAXPOOL = 0xB004, ADD = 0xB005, AVGPOOL = 0xB006, MM = 0xB007,
    XENT = 0xB008, LOSS_BWD = 0xB009, MM_BWD = 0xB00A,
    POOL_BWD = 0xB00B, RELU_BWD = 0xB00C, BN_BWD = 0xB00D,
    CONV_BWD = 0xB00E, SGD = 0xB00F;

// ═══════════════════════════════════════════════════════════════════
// Tensor reference: one byte encodes parameter vs activation
//
//   ref & 0x80 → parameter, index = ref & 0x7F
//   ref < 0x80 → activation, index = ref
//
// Parameters have stable pointers across iterations (model weights).
// Activations get unique pointers per (iteration, index) pair.
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
// Op table — each row defines one ATen op with its I/O tensors
// ═══════════════════════════════════════════════════════════════════

struct OpDef {
    uint64_t schema, shape;
    uint8_t n_in, n_out;
    TSpec t[6]; // inputs[0..n_in-1], outputs[n_in..n_in+n_out-1]
};

// Mini-ResNet shapes
static constexpr int64_t N = 2, CH = 8, S = 8, Sp = 4, CL = 10;

// Parameters: P0=input[2,3,16,16] P1=stem_w[8,3,3,3] P2=bn1_w[8] P3=bn1_b[8]
//   P4=blk_c1_w[8,8,3,3] P5=bn2_w[8] P6=bn2_b[8] P7=blk_c2_w[8,8,3,3]
//   P8=bn3_w[8] P9=bn3_b[8] P10=fc_w[10,8] P11=fc_b[10]
//
// Activations: A0-A14 (forward), A15-A24 (backward)

static const OpDef OPS[] = {
    // ── Forward (15 ops) ─────────────────────────────────────────
    /*  0 */ {CONV,    0xC001, 2,1, {pr(0,4,N,3,16,16), pr(1,4,CH,3,3,3),
                                     ac(0,4,N,CH,S,S)}},
    /*  1 */ {BN,      0xC002, 3,1, {ac(0,4,N,CH,S,S), pr(2,1,CH), pr(3,1,CH),
                                     ac(1,4,N,CH,S,S)}},
    /*  2 */ {RELU,    0xC003, 1,1, {ac(1,4,N,CH,S,S), ac(2,4,N,CH,S,S)}},
    /*  3 */ {MAXPOOL, 0xC004, 1,1, {ac(2,4,N,CH,S,S), ac(3,4,N,CH,Sp,Sp)}},
    /*  4 */ {CONV,    0xC005, 2,1, {ac(3,4,N,CH,Sp,Sp), pr(4,4,CH,CH,3,3),
                                     ac(4,4,N,CH,Sp,Sp)}},
    /*  5 */ {BN,      0xC006, 3,1, {ac(4,4,N,CH,Sp,Sp), pr(5,1,CH), pr(6,1,CH),
                                     ac(5,4,N,CH,Sp,Sp)}},
    /*  6 */ {RELU,    0xC007, 1,1, {ac(5,4,N,CH,Sp,Sp), ac(6,4,N,CH,Sp,Sp)}},
    /*  7 */ {CONV,    0xC008, 2,1, {ac(6,4,N,CH,Sp,Sp), pr(7,4,CH,CH,3,3),
                                     ac(7,4,N,CH,Sp,Sp)}},
    /*  8 */ {BN,      0xC009, 3,1, {ac(7,4,N,CH,Sp,Sp), pr(8,1,CH), pr(9,1,CH),
                                     ac(8,4,N,CH,Sp,Sp)}},
    /*  9 */ {ADD,     0xC00A, 2,1, {ac(8,4,N,CH,Sp,Sp), ac(3,4,N,CH,Sp,Sp),
                                     ac(9,4,N,CH,Sp,Sp)}},   // residual
    /* 10 */ {RELU,    0xC00B, 1,1, {ac(9,4,N,CH,Sp,Sp), ac(10,4,N,CH,Sp,Sp)}},
    /* 11 */ {AVGPOOL, 0xC00C, 1,1, {ac(10,4,N,CH,Sp,Sp), ac(11,2,N,CH)}},
    /* 12 */ {MM,      0xC00D, 2,1, {ac(11,2,N,CH), pr(10,2,CL,CH),
                                     ac(12,2,N,CL)}},
    /* 13 */ {ADD,     0xC00E, 2,1, {ac(12,2,N,CL), pr(11,1,CL), ac(13,2,N,CL)}},
    /* 14 */ {XENT,    0xC00F, 1,1, {ac(13,2,N,CL), ac(14,2,N,CL)}},

    // ── Backward (10 ops) ────────────────────────────────────────
    /* 15 */ {LOSS_BWD, 0xC010, 1,1, {ac(14,2,N,CL), ac(15,2,N,CL)}},
    /* 16 */ {MM_BWD,   0xC011, 2,1, {ac(15,2,N,CL), pr(10,2,CL,CH),
                                      ac(16,2,N,CH)}},
    /* 17 */ {POOL_BWD, 0xC012, 1,1, {ac(16,2,N,CH), ac(17,4,N,CH,Sp,Sp)}},
    /* 18 */ {RELU_BWD, 0xC013, 2,1, {ac(17,4,N,CH,Sp,Sp), ac(10,4,N,CH,Sp,Sp),
                                      ac(18,4,N,CH,Sp,Sp)}},
    /* 19 */ {BN_BWD,   0xC014, 1,1, {ac(18,4,N,CH,Sp,Sp), ac(19,4,N,CH,Sp,Sp)}},
    /* 20 */ {CONV_BWD, 0xC015, 1,1, {ac(19,4,N,CH,Sp,Sp), ac(20,4,N,CH,Sp,Sp)}},
    /* 21 */ {RELU_BWD, 0xC016, 2,1, {ac(20,4,N,CH,Sp,Sp), ac(6,4,N,CH,Sp,Sp),
                                      ac(21,4,N,CH,Sp,Sp)}},
    /* 22 */ {BN_BWD,   0xC017, 1,1, {ac(21,4,N,CH,Sp,Sp), ac(22,4,N,CH,Sp,Sp)}},
    /* 23 */ {CONV_BWD, 0xC018, 1,1, {ac(22,4,N,CH,Sp,Sp), ac(23,4,N,CH,Sp,Sp)}},
    /* 24 */ {SGD,      0xC019, 2,1, {pr(1,4,CH,3,3,3), ac(23,4,N,CH,Sp,Sp),
                                      ac(24,4,CH,3,3,3)}},
};

static constexpr uint32_t NUM_OPS = sizeof(OPS) / sizeof(OPS[0]);
static_assert(NUM_OPS == 25, "ResNet: 15 forward + 10 backward");

// ═══════════════════════════════════════════════════════════════════
// Tensor construction from table
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
// Main: record → detect → activate → 1000 compiled iterations
// ═══════════════════════════════════════════════════════════════════

int main() {
    std::printf("═══ ImageNet ResNet Training ═══\n\n");
    std::printf("Mini-ResNet: stem → residual block → avgpool → fc(%lld)\n",
                (long long)CL);
    std::printf("Ops/iteration: %u (15 fwd + 10 bwd)\n\n", NUM_OPS);

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
                region->num_ops, (unsigned long long)plan->pool_bytes);
    std::printf("   %u slots: %u external + %u internal\n",
                plan->num_slots, plan->num_external,
                plan->num_slots - plan->num_external);

    // ── Phase 2: Activate ────────────────────────────────────────
    auto ap = build_op(0, 2);
    auto ar = vigil.dispatch_op(ap.entry, ap.metas, ap.n_metas);
    assert(ar.action == DispatchResult::Action::RECORD);
    assert(vigil.context().is_compiled());
    std::printf("   CrucibleContext: COMPILED\n\n");

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
                (unsigned long long)total_ops, (long long)us, ns_op);
    std::printf("   compiled_iterations=%u  diverged=%u\n",
                vigil.compiled_iterations(), vigil.diverged_count());

    // ── Phase 4: Data flow verification ──────────────────────────
    std::printf("\n── Data flow: conv(op0) → bn(op1) ──\n");

    auto p0 = build_op(0, 9999);
    auto r0 = vigil.dispatch_op(p0.entry, p0.metas, p0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(0), 0xAB, 64);

    auto p1 = build_op(1, 9999);
    auto r1 = vigil.dispatch_op(p1.entry, p1.metas, p1.n_metas);
    assert(r1.action == DispatchResult::Action::COMPILED);

    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
    bool ok = true;
    for (uint32_t i = 0; i < 64; i++)
        if (in_data[i] != 0xAB) { ok = false; break; }

    // Complete the iteration
    for (uint32_t i = 2; i < NUM_OPS; i++) {
        auto p = build_op(i, 9999);
        (void)vigil.dispatch_op(p.entry, p.metas, p.n_metas);
    }

    std::printf("   %s\n", ok ? "VERIFIED" : "FAILED");
    assert(ok);

    // ── Summary ──────────────────────────────────────────────────
    std::printf("\n═══ Summary ═══\n");
    std::printf("Pool: %llu bytes | Compiled: %u iters | %.1f ns/op\n",
                (unsigned long long)plan->pool_bytes,
                vigil.compiled_iterations(), ns_op);
    std::printf("\ntest_imagenet: PASSED\n");
    return 0;
}
