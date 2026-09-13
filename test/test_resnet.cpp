// A residual network of fifty layers, driven through the dispatch
// pipeline at production scale.  The shape is a stem, then four
// stages of three, four, six and three bottleneck blocks, then a
// classification head.  That comes to 175 operations and 25,557,032
// parameters, both of which are asserted below, and both of which
// match the reference implementation of this network.
//
// The pipeline under test is the whole of it: two recorded
// iterations, a detected boundary, then a thousand compiled ones.

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace crucible;

static constexpr SchemaHash H_CONV{0xB001}, H_BN{0xB002}, H_RELU{0xB003}, H_MAXPOOL{0xB004}, H_ADD{0xB005},
    H_AVGPOOL{0xB006}, H_MM{0xB007}, H_SOFTMAX{0xB008};

static constexpr uint16_t PARAM = 0x8000;

struct TRef {
    uint16_t ref = 0;
    uint8_t ndim = 0;
    int64_t d[4]{};
};

struct OpDef {
    SchemaHash schema;
    ShapeHash shape;
    uint8_t n_in = 0, n_out = 0;
    TRef t[6]{};
};

// Carries a tensor's shape through the builder so each layer can
// compute the next one.
struct Tens {
    uint16_t ref = 0;
    uint8_t ndim = 4;
    int ch = 0, h = 0, w = 0;
};

struct ResNet50 {
    std::vector<OpDef> ops;
    uint16_t np = 0, na = 0;
    uint64_t params = 0;
    int N = 0;

    TRef tref(Tens t) const {
        TRef r{};
        r.ref = t.ref;
        r.ndim = t.ndim;
        r.d[0] = N;
        r.d[1] = t.ch;
        if (t.ndim == 4) {
            r.d[2] = t.h;
            r.d[3] = t.w;
        }
        return r;
    }

    TRef pr(int ndim, int64_t d0, int64_t d1 = 1, int64_t d2 = 1, int64_t d3 = 1) {
        TRef r{};
        r.ref = uint16_t(PARAM | np++);
        r.ndim = uint8_t(ndim);
        r.d[0] = d0;
        r.d[1] = d1;
        r.d[2] = d2;
        r.d[3] = d3;
        auto n = static_cast<uint64_t>(d0) * static_cast<uint64_t>(d1);
        if (ndim >= 3) n *= static_cast<uint64_t>(d2);
        if (ndim >= 4) n *= static_cast<uint64_t>(d3);
        params += n;
        return r;
    }

    void emit(SchemaHash sch, int ni, int no, TRef t0 = {}, TRef t1 = {}, TRef t2 = {}, TRef t3 = {}) {
        OpDef op{};
        op.schema = sch;
        op.shape = ShapeHash{0xC000ULL + ops.size()};
        op.n_in = static_cast<uint8_t>(ni);
        op.n_out = static_cast<uint8_t>(no);
        op.t[0] = t0;
        op.t[1] = t1;
        op.t[2] = t2;
        op.t[3] = t3;
        ops.push_back(op);
    }

    Tens conv(Tens in, int co, int k, int s, int p) {
        int oh = (in.h + 2 * p - k) / s + 1;
        int ow = (in.w + 2 * p - k) / s + 1;
        Tens out{.ref = na++, .ndim = 4, .ch = co, .h = oh, .w = ow};
        emit(H_CONV, 2, 1, tref(in), pr(4, co, in.ch, k, k), tref(out));
        return out;
    }

    Tens bn(Tens in) {
        Tens out{.ref = na++, .ndim = in.ndim, .ch = in.ch, .h = in.h, .w = in.w};
        emit(H_BN, 3, 1, tref(in), pr(1, in.ch), pr(1, in.ch), tref(out));
        return out;
    }

    Tens relu(Tens in) {
        Tens out{.ref = na++, .ndim = in.ndim, .ch = in.ch, .h = in.h, .w = in.w};
        emit(H_RELU, 1, 1, tref(in), tref(out));
        return out;
    }

    Tens maxpool(Tens in, int k, int s, int p) {
        int oh = (in.h + 2 * p - k) / s + 1;
        int ow = (in.w + 2 * p - k) / s + 1;
        Tens out{.ref = na++, .ndim = 4, .ch = in.ch, .h = oh, .w = ow};
        emit(H_MAXPOOL, 1, 1, tref(in), tref(out));
        return out;
    }

    Tens add(Tens a, Tens b) {
        Tens out{.ref = na++, .ndim = a.ndim, .ch = a.ch, .h = a.h, .w = a.w};
        emit(H_ADD, 2, 1, tref(a), tref(b), tref(out));
        return out;
    }

    Tens avgpool(Tens in) {
        Tens out{.ref = na++, .ndim = 2, .ch = in.ch, .h = 0, .w = 0};
        emit(H_AVGPOOL, 1, 1, tref(in), tref(out));
        return out;
    }

    Tens linear(Tens in, int out_ch) {
        Tens out{.ref = na++, .ndim = 2, .ch = out_ch, .h = 0, .w = 0};
        emit(H_MM, 3, 1, tref(in), pr(2, out_ch, in.ch), pr(1, out_ch), tref(out));
        return out;
    }

    Tens softmax(Tens in) {
        Tens out{.ref = na++, .ndim = 2, .ch = in.ch, .h = 0, .w = 0};
        emit(H_SOFTMAX, 1, 1, tref(in), tref(out));
        return out;
    }

    // A bottleneck narrows, convolves, and widens again, with a skip
    // path around it.  The stride goes on the middle convolution,
    // which is the convention the reference implementation uses and
    // which decides where the spatial reduction happens.
    Tens bottleneck(Tens x, int mid, int out_ch, int stride) {
        auto a = relu(bn(conv(x, mid, 1, 1, 0)));
        a = relu(bn(conv(a, mid, 3, stride, 1)));
        a = bn(conv(a, out_ch, 1, 1, 0));

        auto skip = x;
        if (x.ch != out_ch || stride != 1) skip = bn(conv(x, out_ch, 1, stride, 0));

        return relu(add(a, skip));
    }

    void build(int batch) {
        N = batch;
        ops.clear();
        np = 0;
        na = 0;
        params = 0;

        // The input image is external, so it takes a reference but
        // contributes nothing to the parameter count.
        Tens x{.ref = uint16_t(PARAM | np++), .ndim = 4, .ch = 3, .h = 224, .w = 224};

        x = maxpool(relu(bn(conv(x, 64, 7, 2, 3))), 3, 2, 1);

        // The first stage keeps the spatial size; each later stage
        // halves it on its first block.
        for (int i = 0; i < 3; i++)
            x = bottleneck(x, 64, 256, 1);

        x = bottleneck(x, 128, 512, 2);
        for (int i = 0; i < 3; i++)
            x = bottleneck(x, 128, 512, 1);

        x = bottleneck(x, 256, 1024, 2);
        for (int i = 0; i < 5; i++)
            x = bottleneck(x, 256, 1024, 1);

        x = bottleneck(x, 512, 2048, 2);
        for (int i = 0; i < 2; i++)
            x = bottleneck(x, 512, 2048, 1);

        x = softmax(linear(avgpool(x), 1000));
    }
};

static void* param_ptr(uint16_t idx) { return std::bit_cast<void*>(static_cast<std::uintptr_t>(idx + 1) * 0x10000); }

static void* act_ptr(uint32_t iter, uint16_t idx) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>(iter + 1) * 0x10000000ULL
                                + static_cast<std::uintptr_t>(idx + 1) * 0x10000);
}

static TensorMeta make_meta(const TRef& s, uint32_t iter) {
    TensorMeta m{};
    m.data_ptr = external_data_ptr((s.ref & PARAM) ? param_ptr(uint16_t(s.ref & 0x7FFF)) : act_ptr(iter, s.ref));
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.ndim = s.ndim;
    for (uint8_t i = 0; i < s.ndim; i++)
        m.sizes[i] = ::crucible::tensor_dim(s.d[i]);
    if (s.ndim > 0) {
        const std::size_t ndim = static_cast<std::size_t>(s.ndim);
        m.strides[ndim - 1] = ::crucible::tensor_dim(1);
        for (std::size_t i = ndim - 1; i-- > 0;)
            m.strides[i] = ::crucible::tensor_dim(::crucible::raw_tensor_dim(m.strides[i + 1])
                                                  * ::crucible::raw_tensor_dim(m.sizes[i + 1]));
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

static void feed_iter(Vigil& v, const std::vector<OpDef>& ops, uint32_t iter) {
    for (const auto& op : ops) {
        auto p = build_pkt(op, iter);
        assert(v.record_op(crucible::vouch(p.entry), p.metas, p.n_metas));
    }
}

static void feed_trigger(Vigil& v, const std::vector<OpDef>& ops, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K && i < ops.size(); i++) {
        auto p = build_pkt(ops[i], iter);
        assert(v.record_op(crucible::vouch(p.entry), p.metas, p.n_metas));
    }
}

using test::flush_and_wait_compiled;

int main() {
    std::printf("test_resnet: ResNet-50 (He et al. 2015)\n");

    ResNet50 net;
    net.build(2);
    std::printf("  %zu ops, %llu params, %u param tensors, %u activations\n", net.ops.size(),
                static_cast<unsigned long long>(net.params), static_cast<uint32_t>(net.np - 1),
                static_cast<uint32_t>(net.na));

    assert(net.ops.size() == 175 && "the forward pass is 175 operations");
    assert(net.params == 25557032
           && "the parameter count must match the "
              "reference implementation");

    Vigil vigil;

    feed_iter(vigil, net.ops, 0);
    feed_iter(vigil, net.ops, 1);
    feed_trigger(vigil, net.ops, 2);
    flush_and_wait_compiled(vigil);

    const auto* region = vigil.active_region();
    assert(region && region->plan);
    std::printf("  region: %u ops, pool %llu B, %u slots (%u ext)\n", region->num_ops,
                static_cast<unsigned long long>(region->plan->pool_bytes), region->plan->num_slots,
                region->plan->num_external);

    // Compiled execution starts only at an iteration boundary, so the
    // first few operations of this iteration are still recorded while
    // the pipeline aligns.
    static constexpr uint32_t AK = Vigil::ALIGNMENT_K;
    for (uint32_t i = 0; i < AK; i++) {
        auto ap = build_pkt(net.ops[i], 3);
        auto ar = vigil.dispatch_op(crucible::vouch(ap.entry), ap.metas, ap.n_metas);
        assert(ar.action == DispatchResult::Action::RECORD);
    }
    assert(vigil.context().is_compiled());
    // The rest of the same iteration runs compiled.
    for (size_t i = AK; i < net.ops.size(); i++) {
        auto ap = build_pkt(net.ops[i], 3);
        auto ar = vigil.dispatch_op(crucible::vouch(ap.entry), ap.metas, ap.n_metas);
        assert(ar.action == DispatchResult::Action::COMPILED);
    }
    std::printf("  aligned (%u ops) + partial iteration compiled\n", AK);

    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t iter = 4; iter < 1004; iter++) {
        for (size_t i = 0; i < net.ops.size(); i++) {
            auto p = build_pkt(net.ops[i], iter);
            auto r = vigil.dispatch_op(crucible::vouch(p.entry), p.metas, p.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    uint64_t total = 1000ULL * net.ops.size();
    double ns_op = double(us) * 1000.0 / double(total);

    std::printf("  %llu dispatches in %lld us (%.1f ns/op)\n", static_cast<unsigned long long>(total),
                static_cast<long long>(us), ns_op);
    assert(vigil.compiled_iterations() == 1001);  // 1 partial + 1000 full
    assert(vigil.diverged_count() == 0);

    // The first operation's output slot must be the second
    // operation's input slot.  Writing a pattern through one and
    // reading it back through the other is what shows the slots were
    // planned to coincide.
    auto p0 = build_pkt(net.ops[0], 9999);
    auto r0 = vigil.dispatch_op(crucible::vouch(p0.entry), p0.metas, p0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(0), 0xAB, 64);

    auto p1 = build_pkt(net.ops[1], 9999);
    auto r1 = vigil.dispatch_op(crucible::vouch(p1.entry), p1.metas, p1.n_metas);
    assert(r1.action == DispatchResult::Action::COMPILED);

    auto* d = static_cast<uint8_t*>(vigil.input_ptr(0));
    bool flow_ok = true;
    for (int i = 0; i < 64; i++)
        if (d[i] != 0xAB) {
            flow_ok = false;
            break;
        }

    // Finish the iteration so the pipeline is left at a boundary.
    for (size_t i = 2; i < net.ops.size(); i++) {
        auto p = build_pkt(net.ops[i], 9999);
        (void)vigil.dispatch_op(crucible::vouch(p.entry), p.metas, p.n_metas);
    }

    std::printf("  data_flow: %s\n", flow_ok ? "VERIFIED" : "FAILED");
    assert(flow_ok);

    std::printf("test_resnet: PASSED\n");
    return 0;
}
