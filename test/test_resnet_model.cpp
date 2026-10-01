// The network builder and the op packets of test_resnet.

#include "resnet.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace crucible;

namespace test_resnet {

namespace {

constexpr SchemaHash H_CONV{0xB001}, H_BN{0xB002}, H_RELU{0xB003}, H_MAXPOOL{0xB004}, H_ADD{0xB005}, H_AVGPOOL{0xB006},
    H_MM{0xB007}, H_SOFTMAX{0xB008};

void* param_ptr(uint16_t idx) { return std::bit_cast<void*>(static_cast<std::uintptr_t>(idx + 1) * 0x10000); }

void* act_ptr(uint32_t iter, uint16_t idx) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>(iter + 1) * 0x10000000ULL
                                + static_cast<std::uintptr_t>(idx + 1) * 0x10000);
}

TensorMeta make_meta(const TRef& s, uint32_t iter) {
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

}  // namespace

TRef ResNet50::tref(Tens t) const {
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

TRef ResNet50::pr(int ndim, int64_t d0, int64_t d1, int64_t d2, int64_t d3) {
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

void ResNet50::emit(SchemaHash sch, int ni, int no, TRef t0, TRef t1, TRef t2, TRef t3) {
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

Tens ResNet50::conv(Tens in, int co, int k, int s, int p) {
    int oh = (in.h + 2 * p - k) / s + 1;
    int ow = (in.w + 2 * p - k) / s + 1;
    Tens out{.ref = na++, .ndim = 4, .ch = co, .h = oh, .w = ow};
    emit(H_CONV, 2, 1, tref(in), pr(4, co, in.ch, k, k), tref(out));
    return out;
}

Tens ResNet50::bn(Tens in) {
    Tens out{.ref = na++, .ndim = in.ndim, .ch = in.ch, .h = in.h, .w = in.w};
    emit(H_BN, 3, 1, tref(in), pr(1, in.ch), pr(1, in.ch), tref(out));
    return out;
}

Tens ResNet50::relu(Tens in) {
    Tens out{.ref = na++, .ndim = in.ndim, .ch = in.ch, .h = in.h, .w = in.w};
    emit(H_RELU, 1, 1, tref(in), tref(out));
    return out;
}

Tens ResNet50::maxpool(Tens in, int k, int s, int p) {
    int oh = (in.h + 2 * p - k) / s + 1;
    int ow = (in.w + 2 * p - k) / s + 1;
    Tens out{.ref = na++, .ndim = 4, .ch = in.ch, .h = oh, .w = ow};
    emit(H_MAXPOOL, 1, 1, tref(in), tref(out));
    return out;
}

Tens ResNet50::add(Tens a, Tens b) {
    Tens out{.ref = na++, .ndim = a.ndim, .ch = a.ch, .h = a.h, .w = a.w};
    emit(H_ADD, 2, 1, tref(a), tref(b), tref(out));
    return out;
}

Tens ResNet50::avgpool(Tens in) {
    Tens out{.ref = na++, .ndim = 2, .ch = in.ch, .h = 0, .w = 0};
    emit(H_AVGPOOL, 1, 1, tref(in), tref(out));
    return out;
}

Tens ResNet50::linear(Tens in, int out_ch) {
    Tens out{.ref = na++, .ndim = 2, .ch = out_ch, .h = 0, .w = 0};
    emit(H_MM, 3, 1, tref(in), pr(2, out_ch, in.ch), pr(1, out_ch), tref(out));
    return out;
}

Tens ResNet50::softmax(Tens in) {
    Tens out{.ref = na++, .ndim = 2, .ch = in.ch, .h = 0, .w = 0};
    emit(H_SOFTMAX, 1, 1, tref(in), tref(out));
    return out;
}

Tens ResNet50::bottleneck(Tens x, int mid, int out_ch, int stride) {
    auto a = relu(bn(conv(x, mid, 1, 1, 0)));
    a = relu(bn(conv(a, mid, 3, stride, 1)));
    a = bn(conv(a, out_ch, 1, 1, 0));

    auto skip = x;
    if (x.ch != out_ch || stride != 1) skip = bn(conv(x, out_ch, 1, stride, 0));

    return relu(add(a, skip));
}

void ResNet50::build(int batch) {
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

OpPacket build_pkt(const OpDef& op, uint32_t iter) {
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

void feed_iter(Vigil& v, const std::vector<OpDef>& ops, uint32_t iter) {
    for (const auto& op : ops) {
        auto p = build_pkt(op, iter);
        assert(v.record_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas));
    }
}

void feed_trigger(Vigil& v, const std::vector<OpDef>& ops, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K && i < ops.size(); i++) {
        auto p = build_pkt(ops[i], iter);
        assert(v.record_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas));
    }
}

}  // namespace test_resnet
