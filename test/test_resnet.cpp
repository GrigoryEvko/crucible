// Real ResNet-50 (He et al. 2015) — 175-op forward pass through Crucible dispatch.
//
// Architecture: stem → layer1(3) → layer2(4) → layer3(6) → layer4(3) → head
// 25,557,032 parameters (verified against torchvision.models.resnet50).
// Ops: 53 conv, 53 BN, 49 ReLU, 16 add, 1 maxpool, 1 avgpool, 1 linear, 1 softmax.
//
// Tests the Vigil dispatch pipeline with a production-scale model:
// record 2 iterations → BackgroundThread detects boundary → 1000 compiled iters.

#include <crucible/Vigil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace crucible;

static constexpr SchemaHash H_CONV{0xB001}, H_BN{0xB002}, H_RELU{0xB003},
    H_MAXPOOL{0xB004}, H_ADD{0xB005}, H_AVGPOOL{0xB006},
    H_MM{0xB007}, H_SOFTMAX{0xB008};

static constexpr uint16_t PARAM = 0x8000;

// ═══════════════════════════════════════════════════════════════════
// Data types for the op table
// ═══════════════════════════════════════════════════════════════════

struct TRef { uint16_t ref = 0; uint8_t ndim = 0; int64_t d[4]{}; };

struct OpDef {
    SchemaHash schema;
    ShapeHash shape;
    uint8_t n_in = 0, n_out = 0;
    TRef t[6]{};
};

// Spatial tensor handle — tracks shapes through the network
struct Tens { uint16_t ref = 0; uint8_t ndim = 4; int ch = 0, h = 0, w = 0; };

// ═══════════════════════════════════════════════════════════════════
// ResNet-50 builder — generates 175 ops programmatically
// ═══════════════════════════════════════════════════════════════════

struct ResNet50 {
    std::vector<OpDef> ops;
    uint16_t np = 0, na = 0;
    uint64_t params = 0;
    int N = 0;

    TRef tref(Tens t) const {
        TRef r{}; r.ref = t.ref; r.ndim = t.ndim;
        r.d[0] = N; r.d[1] = t.ch;
        if (t.ndim == 4) { r.d[2] = t.h; r.d[3] = t.w; }
        return r;
    }

    TRef pr(int ndim, int64_t d0, int64_t d1 = 1,
            int64_t d2 = 1, int64_t d3 = 1) {
        TRef r{}; r.ref = uint16_t(PARAM | np++); r.ndim = uint8_t(ndim);
        r.d[0] = d0; r.d[1] = d1; r.d[2] = d2; r.d[3] = d3;
        auto n = static_cast<uint64_t>(d0) * static_cast<uint64_t>(d1);
        if (ndim >= 3) n *= static_cast<uint64_t>(d2);
        if (ndim >= 4) n *= static_cast<uint64_t>(d3);
        params += n;
        return r;
    }

    void emit(SchemaHash sch, int ni, int no,
              TRef t0 = {}, TRef t1 = {}, TRef t2 = {}, TRef t3 = {}) {
        OpDef op{};
        op.schema = sch;
        op.shape = ShapeHash{static_cast<uint64_t>(0xC000 + ops.size())};
        op.n_in = static_cast<uint8_t>(ni);
        op.n_out = static_cast<uint8_t>(no);
        op.t[0] = t0; op.t[1] = t1; op.t[2] = t2; op.t[3] = t3;
        ops.push_back(op);
    }

    // ── Op builders ───────────────────────────────────────────

    Tens conv(Tens in, int co, int k, int s, int p) {
        int oh = (in.h + 2 * p - k) / s + 1;
        int ow = (in.w + 2 * p - k) / s + 1;
        Tens out{na++, 4, co, oh, ow};
        emit(H_CONV, 2, 1, tref(in), pr(4, co, in.ch, k, k), tref(out));
        return out;
    }

    Tens bn(Tens in) {
        Tens out{na++, in.ndim, in.ch, in.h, in.w};
        emit(H_BN, 3, 1, tref(in), pr(1, in.ch), pr(1, in.ch), tref(out));
        return out;
    }

    Tens relu(Tens in) {
        Tens out{na++, in.ndim, in.ch, in.h, in.w};
        emit(H_RELU, 1, 1, tref(in), tref(out));
        return out;
    }

    Tens maxpool(Tens in, int k, int s, int p) {
        int oh = (in.h + 2 * p - k) / s + 1;
        int ow = (in.w + 2 * p - k) / s + 1;
        Tens out{na++, 4, in.ch, oh, ow};
        emit(H_MAXPOOL, 1, 1, tref(in), tref(out));
        return out;
    }

    Tens add(Tens a, Tens b) {
        Tens out{na++, a.ndim, a.ch, a.h, a.w};
        emit(H_ADD, 2, 1, tref(a), tref(b), tref(out));
        return out;
    }

    Tens avgpool(Tens in) {
        Tens out{na++, 2, in.ch, 0, 0};
        emit(H_AVGPOOL, 1, 1, tref(in), tref(out));
        return out;
    }

    Tens linear(Tens in, int out_ch) {
        Tens out{na++, 2, out_ch, 0, 0};
        emit(H_MM, 3, 1, tref(in), pr(2, out_ch, in.ch),
             pr(1, out_ch), tref(out));
        return out;
    }

    Tens softmax(Tens in) {
        Tens out{na++, 2, in.ch, 0, 0};
        emit(H_SOFTMAX, 1, 1, tref(in), tref(out));
        return out;
    }

    // ── Bottleneck block (torchvision convention: stride on 3×3) ──

    Tens bottleneck(Tens x, int mid, int out_ch, int stride) {
        auto a = relu(bn(conv(x, mid, 1, 1, 0)));
        a = relu(bn(conv(a, mid, 3, stride, 1)));
        a = bn(conv(a, out_ch, 1, 1, 0));

        auto skip = x;
        if (x.ch != out_ch || stride != 1)
            skip = bn(conv(x, out_ch, 1, stride, 0));

        return relu(add(a, skip));
    }

    // ── Build full ResNet-50 ──────────────────────────────────

    void build(int batch) {
        N = batch; ops.clear(); np = 0; na = 0; params = 0;

        // External input [N,3,224,224] — not counted in model params
        Tens x{uint16_t(PARAM | np++), 4, 3, 224, 224};

        // Stem: conv7×7/s2 → BN → ReLU → maxpool3×3/s2
        x = maxpool(relu(bn(conv(x, 64, 7, 2, 3))), 3, 2, 1);

        // Layer 1: 3 bottleneck blocks, 64→256 (no spatial reduction)
        for (int i = 0; i < 3; i++)
            x = bottleneck(x, 64, 256, 1);

        // Layer 2: 4 bottleneck blocks, 128→512 (stride=2 on first)
        x = bottleneck(x, 128, 512, 2);
        for (int i = 0; i < 3; i++)
            x = bottleneck(x, 128, 512, 1);

        // Layer 3: 6 bottleneck blocks, 256→1024 (stride=2 on first)
        x = bottleneck(x, 256, 1024, 2);
        for (int i = 0; i < 5; i++)
            x = bottleneck(x, 256, 1024, 1);

        // Layer 4: 3 bottleneck blocks, 512→2048 (stride=2 on first)
        x = bottleneck(x, 512, 2048, 2);
        for (int i = 0; i < 2; i++)
            x = bottleneck(x, 512, 2048, 1);

        // Head: avgpool → fc(1000) → softmax
        x = softmax(linear(avgpool(x), 1000));
    }
};

// ═══════════════════════════════════════════════════════════════════
// TensorMeta construction and feed helpers
// ═══════════════════════════════════════════════════════════════════

static void* param_ptr(uint16_t idx) {
    return reinterpret_cast<void*>(uintptr_t(uint64_t(idx + 1) * 0x10000));
}

static void* act_ptr(uint32_t iter, uint16_t idx) {
    return reinterpret_cast<void*>(
        uintptr_t(uint64_t(iter + 1) * 0x10000000ULL +
                  uint64_t(idx + 1) * 0x10000));
}

static TensorMeta make_meta(const TRef& s, uint32_t iter) {
    TensorMeta m{};
    m.data_ptr = (s.ref & PARAM) ? param_ptr(uint16_t(s.ref & 0x7FFF))
                                 : act_ptr(iter, s.ref);
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

static OpPacket build_pkt(const OpDef& op, uint32_t iter) {
    OpPacket pkt{};
    pkt.entry.schema_hash = op.schema;
    pkt.entry.shape_hash = op.shape;
    pkt.entry.num_inputs = op.n_in;
    pkt.entry.num_outputs = op.n_out;
    pkt.n_metas = uint16_t(op.n_in + op.n_out);
    for (uint8_t i = 0; i < pkt.n_metas; i++)
        pkt.metas[i] = make_meta(op.t[i], iter);
    return pkt;
}

static void feed_iter(Vigil& v, const std::vector<OpDef>& ops,
                      uint32_t iter) {
    for (const auto& op : ops) {
        auto p = build_pkt(op, iter);
        assert(v.record_op(p.entry, p.metas, p.n_metas));
    }
}

static void feed_trigger(Vigil& v, const std::vector<OpDef>& ops,
                         uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K && i < ops.size(); i++) {
        auto p = build_pkt(ops[i], iter);
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

int main() {
    std::printf("test_resnet: ResNet-50 (He et al. 2015)\n");

    ResNet50 net;
    net.build(2);
    std::printf("  %zu ops, %llu params, %u param tensors, %u activations\n",
                net.ops.size(),
                static_cast<unsigned long long>(net.params),
                static_cast<uint32_t>(net.np - 1),
                static_cast<uint32_t>(net.na));

    assert(net.ops.size() == 175 && "ResNet-50 forward = 175 ops");
    assert(net.params == 25557032 && "must match torchvision resnet50");

    Vigil vigil;

    // ── Record 2 iterations + trigger ──
    feed_iter(vigil, net.ops, 0);
    feed_iter(vigil, net.ops, 1);
    feed_trigger(vigil, net.ops, 2);
    vigil.flush();
    wait_compiled(vigil);

    const auto* region = vigil.active_region();
    assert(region && region->plan);
    std::printf("  region: %u ops, pool %llu B, %u slots (%u ext)\n",
                region->num_ops,
                static_cast<unsigned long long>(region->plan->pool_bytes),
                region->plan->num_slots, region->plan->num_external);

    // ── Activate via K-op alignment ──
    static constexpr uint32_t AK = Vigil::ALIGNMENT_K;
    for (uint32_t i = 0; i < AK; i++) {
        auto ap = build_pkt(net.ops[i], 3);
        auto ar = vigil.dispatch_op(ap.entry, ap.metas, ap.n_metas);
        assert(ar.action == DispatchResult::Action::RECORD);
    }
    assert(vigil.context().is_compiled());
    // Complete partial iteration (ops AK..N-1).
    for (size_t i = AK; i < net.ops.size(); i++) {
        auto ap = build_pkt(net.ops[i], 3);
        auto ar = vigil.dispatch_op(ap.entry, ap.metas, ap.n_metas);
        assert(ar.action == DispatchResult::Action::COMPILED);
    }
    std::printf("  aligned (%u ops) + partial iteration compiled\n", AK);

    // ── 1000 compiled iterations ──
    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t iter = 4; iter < 1004; iter++) {
        for (size_t i = 0; i < net.ops.size(); i++) {
            auto p = build_pkt(net.ops[i], iter);
            auto r = vigil.dispatch_op(p.entry, p.metas, p.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        t1 - t0).count();
    uint64_t total = 1000ULL * net.ops.size();
    double ns_op = double(us) * 1000.0 / double(total);

    std::printf("  %llu dispatches in %lld us (%.1f ns/op)\n",
                static_cast<unsigned long long>(total),
                static_cast<long long>(us), ns_op);
    assert(vigil.compiled_iterations() == 1001);  // 1 partial + 1000 full
    assert(vigil.diverged_count() == 0);

    // ── Data flow: stem conv(op 0) output → stem bn(op 1) input ──
    auto p0 = build_pkt(net.ops[0], 9999);
    auto r0 = vigil.dispatch_op(p0.entry, p0.metas, p0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(0), 0xAB, 64);

    auto p1 = build_pkt(net.ops[1], 9999);
    auto r1 = vigil.dispatch_op(p1.entry, p1.metas, p1.n_metas);
    assert(r1.action == DispatchResult::Action::COMPILED);

    auto* d = static_cast<uint8_t*>(vigil.input_ptr(0));
    bool flow_ok = true;
    for (int i = 0; i < 64; i++)
        if (d[i] != 0xAB) { flow_ok = false; break; }

    // Complete the partial iteration
    for (size_t i = 2; i < net.ops.size(); i++) {
        auto p = build_pkt(net.ops[i], 9999);
        (void)vigil.dispatch_op(p.entry, p.metas, p.n_metas);
    }

    std::printf("  data_flow: %s\n", flow_ok ? "VERIFIED" : "FAILED");
    assert(flow_ok);

    std::printf("test_resnet: PASSED\n");
    return 0;
}
